#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "contiki.h"
#include "lib/random.h"
#include "net/rime/rime.h"
#include "leds.h"
#include "net/netstack.h"
#include "core/net/linkaddr.h"
#include "my_collect.h"
#include "routing_table.h"
#include "topology_report.h"

/* ------------------------------------ LOG Tags / Helper ------------------------------------ */
#define TAG_BEACON "BEACON"
#define TAG_COLLECT "COLLECT"
#define TAG_UC "UC"
#define TAG_TOPO "TOPO"
#define TAG_PIGGY "PIGGY"
#define TAG_SRDCP "SRDCP"
#define TAG_UL "UL"
/* ===== Logging toggle for my_collect.c ===== */
#ifndef LOG_COLLECT
#define LOG_COLLECT 0 /* 1: bật log; 0: tắt log */
#endif

#if LOG_COLLECT
#define LOG(tag, fmt, ...) printf(tag ": " fmt "\n", ##__VA_ARGS__)
#else
#define LOG(tag, fmt, ...) \
        do                 \
        {                  \
        } while (0)
#endif

// #define LOG(tag, fmt, ...) printf(tag ": " fmt "\n", ##__VA_ARGS__)
const linkaddr_t sink_addr = {{0x01, 0x00}};
/*--------------------------------------------------------------------------------------*/
/* Forward declarations (for clean initialization order) */
void beacon_timer_cb(void *ptr);
void send_beacon(struct my_collect_conn *conn);
void bc_recv(struct broadcast_conn *bc_conn, const linkaddr_t *sender);
void uc_recv(struct unicast_conn *uc_conn, const linkaddr_t *sender);

int sr_send(struct my_collect_conn *conn, const linkaddr_t *dest);
int my_collect_send(struct my_collect_conn *conn);

bool check_address_in_piggyback_block(uint8_t piggy_len, linkaddr_t node);
void forward_upward_data(struct my_collect_conn *conn, const linkaddr_t *sender);
void forward_downward_data(struct my_collect_conn *conn, const linkaddr_t *sender);

/*--------------------------------------------------------------------------------------*/
/* Callback structures */
static struct broadcast_callbacks bc_cb = {.recv = bc_recv};
static struct unicast_callbacks uc_cb = {.recv = uc_recv};

/* ------------------------------------------------------------------------------------- */

void my_collect_open(struct my_collect_conn *conn, uint16_t channels,
                     bool is_sink, const struct my_collect_callbacks *callbacks)
{
        linkaddr_copy(&conn->parent, &linkaddr_null);
        conn->metric = 65535; /* not connected yet */
        conn->beacon_seqn = 0;
        conn->callbacks = callbacks;
        conn->treport_hold = 0;
        conn->is_sink = is_sink ? 1 : 0;

        broadcast_open(&conn->bc, channels, &bc_cb);
        unicast_open(&conn->uc, channels + 1, &uc_cb);

        if (conn->is_sink)
        {
                conn->metric = 0;
                conn->routing_table.len = 0;
                ctimer_set(&conn->beacon_timer, CLOCK_SECOND, beacon_timer_cb, conn);
        }
}

/* ------------------------------------ BEACON Management ------------------------------------ */

void beacon_timer_cb(void *ptr)
{
        struct my_collect_conn *conn = ptr;
        send_beacon(conn);
        if (conn->is_sink == 1)
        {
                ctimer_set(&conn->beacon_timer, BEACON_INTERVAL, beacon_timer_cb, conn);
                conn->beacon_seqn = conn->beacon_seqn + 1;
        }
}

void send_beacon(struct my_collect_conn *conn)
{
        struct beacon_msg beacon = {.seqn = conn->beacon_seqn, .metric = conn->metric};

        packetbuf_clear();
        packetbuf_copyfrom(&beacon, sizeof(beacon));
        LOG(TAG_BEACON, "send seq=%u metric=%u", (unsigned)conn->beacon_seqn, (unsigned)conn->metric);
        broadcast_send(&conn->bc);
}

void bc_recv(struct broadcast_conn *bc_conn, const linkaddr_t *sender)
{
        struct beacon_msg beacon;
        int8_t rssi;
        uint8_t lqi;

        struct my_collect_conn *conn =
            (struct my_collect_conn *)(((uint8_t *)bc_conn) - offsetof(struct my_collect_conn, bc));

        if (packetbuf_datalen() != sizeof(struct beacon_msg))
        {
                LOG(TAG_BEACON, "drop (unexpected size=%u)", (unsigned)packetbuf_datalen());
                return;
        }
        memcpy(&beacon, packetbuf_dataptr(), sizeof(struct beacon_msg));
        rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
        lqi = packetbuf_attr(PACKETBUF_ATTR_LINK_QUALITY);

        LOG(TAG_BEACON, "rx from=%02x:%02x seq=%u metric=%u rssi=%d lqi=%u",
            sender->u8[0], sender->u8[1],
            (unsigned)beacon.seqn, (unsigned)beacon.metric, (int)rssi, (unsigned)lqi);

        /* Application hook: implemented in example-runicast-srdcp.c */
        srdcp_app_beacon_observed(sender, beacon.metric, rssi, lqi);

        if (rssi < RSSI_THRESHOLD)
        {
                LOG(TAG_BEACON, "drop (rssi=%d < thr=%d)", (int)rssi, (int)RSSI_THRESHOLD);
                return;
        }

        if (conn->beacon_seqn < beacon.seqn)
        {
                conn->beacon_seqn = beacon.seqn; /* new tree */
        }
        else
        {
                if (conn->metric <= beacon.metric + 1)
                {
                        LOG(TAG_COLLECT, "ignore beacon (my_metric=%u, neigh_metric=%u)",
                            (unsigned)conn->metric, (unsigned)beacon.metric);
                        return;
                }
        }

        /* update metric (hop count) */
        conn->metric = beacon.metric + 1;

        /* update parent */
        if (!linkaddr_cmp(&conn->parent, sender))
        {
                linkaddr_copy(&conn->parent, sender);
                LOG(TAG_COLLECT, "parent set to %02x:%02x (new_metric=%u)",
                    conn->parent.u8[0], conn->parent.u8[1], (unsigned)conn->metric);

                if (TOPOLOGY_REPORT)
                {
                        conn->treport_hold = 1;
                        ctimer_stop(&conn->treport_hold_timer);
                        ctimer_set(&conn->treport_hold_timer, TOPOLOGY_REPORT_HOLD_TIME,
                                   topology_report_hold_cb, conn);
                }
        }

        /* forward beacon after a small random delay */
        ctimer_set(&conn->beacon_timer, BEACON_FORWARD_DELAY, beacon_timer_cb, conn);
        LOG(TAG_COLLECT, "schedule beacon forward after %u ticks", (unsigned)BEACON_FORWARD_DELAY);
}

/* ------------------------------------ Send / Receive ------------------------------------ */

int my_collect_send(struct my_collect_conn *conn)
{
        uint8_t piggy_len = 0;
        tree_connection tc = {.node = linkaddr_node_addr, .parent = conn->parent};

        if (PIGGYBACKING == 1)
                piggy_len = 1;

        struct upward_data_packet_header hdr = {
            .source = linkaddr_node_addr,
            .hops = 0,
            .piggy_len = piggy_len};
        enum packet_type pt = upward_data_packet;

        if (linkaddr_cmp(&conn->parent, &linkaddr_null))
        {
                LOG(TAG_UL, "drop (no parent)");
                return 0; /* no parent */
        }

        if (PIGGYBACKING == 1)
        {
                packetbuf_hdralloc(sizeof(enum packet_type) +
                                   sizeof(upward_data_packet_header) +
                                   sizeof(tree_connection));
                memcpy(packetbuf_hdrptr(), &pt, sizeof(enum packet_type));
                memcpy(packetbuf_hdrptr() + sizeof(enum packet_type),
                       &hdr, sizeof(upward_data_packet_header));
                memcpy(packetbuf_hdrptr() + sizeof(enum packet_type) + sizeof(upward_data_packet_header),
                       &tc, sizeof(tree_connection));
        }
        else
        {
                packetbuf_hdralloc(sizeof(enum packet_type) +
                                   sizeof(upward_data_packet_header));
                memcpy(packetbuf_hdrptr(), &pt, sizeof(enum packet_type));
                memcpy(packetbuf_hdrptr() + sizeof(enum packet_type),
                       &hdr, sizeof(upward_data_packet_header));
        }
        return unicast_send(&conn->uc, &conn->parent);
}

int sr_send(struct my_collect_conn *conn, const linkaddr_t *dest)
{
        if (!conn->is_sink)
                return 0;

        int path_len = find_route(conn, dest);
        print_route(conn, path_len, dest);
        if (path_len == 0)
        {
                LOG(TAG_SRDCP, "no route to %02x:%02x (downlink dropped)", dest->u8[0], dest->u8[1]);
                return 0;
        }

        enum packet_type pt = downward_data_packet;
        downward_data_packet_header hdr = {.hops = 0, .path_len = path_len};

        packetbuf_hdralloc(sizeof(enum packet_type) +
                           sizeof(downward_data_packet_header) +
                           sizeof(linkaddr_t) * path_len);
        memcpy(packetbuf_hdrptr(), &pt, sizeof(enum packet_type));
        memcpy(packetbuf_hdrptr() + sizeof(enum packet_type),
               &hdr, sizeof(downward_data_packet_header));

        int i;
        for (i = path_len - 1; i >= 0; i--)
        {
                memcpy(packetbuf_hdrptr() + sizeof(enum packet_type) +
                           sizeof(downward_data_packet_header) +
                           sizeof(linkaddr_t) * (path_len - (i + 1)),
                       &conn->routing_table.tree_path[i],
                       sizeof(linkaddr_t));
        }
        return unicast_send(&conn->uc, &conn->routing_table.tree_path[path_len - 1]);
}

void uc_recv(struct unicast_conn *uc_conn, const linkaddr_t *sender)
{
        struct my_collect_conn *conn =
            (struct my_collect_conn *)(((uint8_t *)uc_conn) - offsetof(struct my_collect_conn, uc));

        enum packet_type pt;
        memcpy(&pt, packetbuf_dataptr(), sizeof(enum packet_type));

        LOG(TAG_UC, "rx type=%d from=%02x:%02x", (int)pt, sender->u8[0], sender->u8[1]);

        switch (pt)
        {
        case upward_data_packet:
                LOG(TAG_UC, "data rx");
                forward_upward_data(conn, sender);
                break;

        case topology_report:
                if (TOPOLOGY_REPORT == 0)
                {
                        LOG(TAG_TOPO, "drop (feature disabled)");
                }
                else
                {
                        LOG(TAG_UC, "topology rx");
                        if (conn->is_sink)
                        {
                                deliver_topology_report_to_sink(conn);
                        }
                        else
                        {
                                send_topology_report(conn, 1); /* forwarding */
                        }
                }
                break;

        case downward_data_packet:
                LOG(TAG_UC, "sr rx");
                forward_downward_data(conn, sender);
                break;

        default:
                LOG(TAG_UC, "drop (unknown type=%d size=%u)", (int)pt, (unsigned)packetbuf_datalen());
        }
}

/* ----------------------------- Helpers for upward/downward ----------------------------- */

bool check_address_in_piggyback_block(uint8_t piggy_len, linkaddr_t node)
{
        LOG(TAG_PIGGY, "check addr %02x:%02x", node.u8[0], node.u8[1]);
        tree_connection tc;
        uint8_t i;
        for (i = 0; i < piggy_len; i++)
        {
                memcpy(&tc,
                       packetbuf_dataptr() +
                           sizeof(enum packet_type) +
                           sizeof(upward_data_packet_header) +
                           (sizeof(tree_connection) * i),
                       sizeof(tree_connection));
                if (linkaddr_cmp(&tc.node, &node))
                {
                        LOG(TAG_PIGGY, "duplicate addr in header: %02x:%02x", node.u8[0], node.u8[1]);
                        return true;
                }
        }
        return false;
}

void forward_upward_data(struct my_collect_conn *conn, const linkaddr_t *sender)
{
        (void)sender;
        upward_data_packet_header hdr;
        memcpy(&hdr, packetbuf_dataptr() + sizeof(enum packet_type),
               sizeof(upward_data_packet_header));

        if (conn->is_sink == 1)
        {
                packetbuf_hdrreduce(sizeof(enum packet_type) + sizeof(upward_data_packet_header));
                if (PIGGYBACKING == 1)
                {
                        tree_connection tc;
                        if (hdr.piggy_len > MAX_PATH_LENGTH)
                        {
                                LOG(TAG_PIGGY, "drop (len=%u > max=%u)", (unsigned)hdr.piggy_len, (unsigned)MAX_PATH_LENGTH);
                        }
                        if (hdr.piggy_len > 0)
                        {
                                LOG(TAG_PIGGY, "apply %u entries at sink", (unsigned)hdr.piggy_len);
                        }
                        uint8_t i;
                        for (i = 0; i < hdr.piggy_len; i++)
                        {
                                memcpy(&tc, packetbuf_dataptr() + sizeof(tree_connection) * i, sizeof(tree_connection));
                                dict_add(&conn->routing_table, tc.node, tc.parent);
                        }
                        packetbuf_hdrreduce(sizeof(tree_connection) * hdr.piggy_len);
                }
                conn->callbacks->recv(&hdr.source, hdr.hops + 1);
        }
        else
        {
                hdr.hops++;

                if (PIGGYBACKING == 1 &&
                    !check_address_in_piggyback_block(hdr.piggy_len, linkaddr_node_addr))
                {

                        if (hdr.piggy_len < MAX_PATH_LENGTH)
                        {
                                tree_connection tc = {.node = linkaddr_node_addr, .parent = conn->parent};

                                /* Layout hiện tại: [pt][hdr][piggy...]; chèn thêm 1 entry ngay sau hdr */
                                const size_t off_pt = 0;
                                const size_t off_hdr = off_pt + sizeof(enum packet_type);
                                const size_t off_piggy0 = off_hdr + sizeof(upward_data_packet_header);
                                const size_t old_bytes = (size_t)hdr.piggy_len * sizeof(tree_connection);

                                /* Mở rộng header và đảm bảo liên tục trước khi dịch */
                                packetbuf_hdralloc(sizeof(tree_connection));
                                packetbuf_compact();

                                uint8_t *hptr = packetbuf_hdrptr();

                                /* Dịch block piggy cũ sang phải để chừa chỗ cho entry mới */
                                memmove(hptr + off_piggy0 + sizeof(tree_connection),
                                        hptr + off_piggy0,
                                        old_bytes);

                                /* Ghi pt + hdr (hdr với piggy_len mới) */
                                enum packet_type pt = upward_data_packet;
                                hdr.piggy_len++;
                                memcpy(hptr + off_pt, &pt, sizeof(pt));
                                memcpy(hptr + off_hdr, &hdr, sizeof(hdr));

                                /* Ghi entry piggy mới ngay vị trí vừa chèn */
                                memcpy(hptr + off_piggy0, &tc, sizeof(tc));

                                LOG(TAG_PIGGY, "append (node=%02x:%02x parent=%02x:%02x) len=%u",
                                    tc.node.u8[0], tc.node.u8[1], tc.parent.u8[0], tc.parent.u8[1], hdr.piggy_len);
                        }
                        else
                        {
                                LOG(TAG_PIGGY, "skip append (len=%u >= max=%u)",
                                    (unsigned)hdr.piggy_len, (unsigned)MAX_PATH_LENGTH);
                                memcpy(packetbuf_dataptr() + sizeof(enum packet_type), &hdr, sizeof(hdr));
                        }
                }
                else
                {
                        /* Không piggy hoặc trùng -> chỉ cập nhật hops */
                        memcpy(packetbuf_dataptr() + sizeof(enum packet_type), &hdr, sizeof(hdr));
                }

                unicast_send(&conn->uc, &conn->parent);
        }
}

void forward_downward_data(struct my_collect_conn *conn, const linkaddr_t *sender)
{
        (void)sender;
        linkaddr_t addr;
        downward_data_packet_header hdr;

        memcpy(&hdr, packetbuf_dataptr() + sizeof(enum packet_type),
               sizeof(downward_data_packet_header));
        memcpy(&addr, packetbuf_dataptr() + sizeof(enum packet_type) + sizeof(downward_data_packet_header),
               sizeof(linkaddr_t));

        if (linkaddr_cmp(&addr, &linkaddr_node_addr))
        {
                if (hdr.path_len == 1)
                {
                        LOG(TAG_SRDCP, "path complete at %02x:%02x; deliver",
                            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
                        packetbuf_hdrreduce(sizeof(enum packet_type) +
                                            sizeof(downward_data_packet_header) +
                                            sizeof(linkaddr_t));
                        conn->callbacks->sr_recv(conn, hdr.hops + 1);
                }
                else
                {
                        packetbuf_hdrreduce(sizeof(linkaddr_t));
                        hdr.path_len = hdr.path_len - 1;
                        enum packet_type pt = downward_data_packet;
                        memcpy(packetbuf_dataptr(), &pt, sizeof(enum packet_type));
                        memcpy(packetbuf_dataptr() + sizeof(enum packet_type),
                               &hdr, sizeof(downward_data_packet_header));
                        memcpy(&addr, packetbuf_dataptr() + sizeof(enum packet_type) + sizeof(downward_data_packet_header),
                               sizeof(linkaddr_t));
                        unicast_send(&conn->uc, &addr);
                }
        }
        else
        {
                LOG(TAG_SRDCP, "drop (for=%02x:%02x; I'm=%02x:%02x)",
                    addr.u8[0], addr.u8[1], linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
        }
}
