#include <stdbool.h>
#include <stddef.h>
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

/*--------------------------------------------------------------------------------------*/
/* Callback structures */
struct broadcast_callbacks bc_cb = {.recv = bc_recv};
struct unicast_callbacks uc_cb = {.recv = uc_recv};

/* ------------------------------------------------------------------------------------- */

void my_collect_open(struct my_collect_conn *conn, uint16_t channels,
                     bool is_sink, const struct my_collect_callbacks *callbacks)
{
        linkaddr_copy(&conn->parent, &linkaddr_null);
        conn->metric = 65535; // not connected yet
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
        printf("my_collect: sending beacon: seqn %d metric %d\n", conn->beacon_seqn, conn->metric);
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
                printf("my_collect: broadcast of wrong size (not a beacon)\n");
                return;
        }
        memcpy(&beacon, packetbuf_dataptr(), sizeof(struct beacon_msg));
        rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
        lqi = packetbuf_attr(PACKETBUF_ATTR_LINK_QUALITY);

        printf("my_collect: recv beacon from %02x:%02x seqn %u metric %u rssi %d\n",
               sender->u8[0], sender->u8[1], beacon.seqn, beacon.metric, (int)rssi);

        /* Application hook: implemented in example-runicast-srdcp.c */
        srdcp_app_beacon_observed(sender, beacon.metric, rssi, lqi);

        if (rssi < RSSI_THRESHOLD)
        {
                printf("packet rejected due to low rssi\n");
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
                        printf("my_collect: return. conn->metric: %u, beacon.metric: %u\n",
                               conn->metric, beacon.metric);
                        return;
                }
        }

        /* update metric (hop count) */
        conn->metric = beacon.metric + 1;

        /* update parent */
        if (!linkaddr_cmp(&conn->parent, sender))
        {
                linkaddr_copy(&conn->parent, sender);
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
                return 0; // no parent

        if (PIGGYBACKING == 1)
        {
                packetbuf_hdralloc(sizeof(enum packet_type) + sizeof(upward_data_packet_header) + sizeof(tree_connection));
                memcpy(packetbuf_hdrptr(), &pt, sizeof(enum packet_type));
                memcpy(packetbuf_hdrptr() + sizeof(enum packet_type), &hdr, sizeof(upward_data_packet_header));
                memcpy(packetbuf_hdrptr() + sizeof(enum packet_type) + sizeof(upward_data_packet_header),
                       &tc, sizeof(tree_connection));
        }
        else
        {
                packetbuf_hdralloc(sizeof(enum packet_type) + sizeof(upward_data_packet_header));
                memcpy(packetbuf_hdrptr(), &pt, sizeof(enum packet_type));
                memcpy(packetbuf_hdrptr() + sizeof(enum packet_type), &hdr, sizeof(upward_data_packet_header));
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
                return 0;

        enum packet_type pt = downward_data_packet;
        downward_data_packet_header hdr = {.hops = 0, .path_len = path_len};

        packetbuf_hdralloc(sizeof(enum packet_type) + sizeof(downward_data_packet_header) + sizeof(linkaddr_t) * path_len);
        memcpy(packetbuf_hdrptr(), &pt, sizeof(enum packet_type));
        memcpy(packetbuf_hdrptr() + sizeof(enum packet_type), &hdr, sizeof(downward_data_packet_header));
        int i;
        for (i = path_len - 1; i >= 0; i--)
        {
                memcpy(packetbuf_hdrptr() + sizeof(enum packet_type) + sizeof(downward_data_packet_header) + sizeof(linkaddr_t) * (path_len - (i + 1)),
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

        printf("Node %02x:%02x received unicast packet with type %d\n",
               linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1], pt);

        switch (pt)
        {
        case upward_data_packet:
                printf("Node %02x:%02x receivd a unicast data packet\n",
                       linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
                forward_upward_data(conn, sender);
                break;
        case topology_report:
                if (TOPOLOGY_REPORT == 0)
                {
                        printf("ERROR: Received a topoloy report with TOPOLOGY_REPORT=0. Node: %02x:%02x\n",
                               linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
                }
                else
                {
                        printf("Node %02x:%02x receivd a unicast topology report\n",
                               linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
                        if (conn->is_sink)
                        {
                                deliver_topology_report_to_sink(conn);
                        }
                        else
                        {
                                send_topology_report(conn, 1); // forwarding
                        }
                }
                break;
        case downward_data_packet:
                printf("Node %02x:%02x receivd a unicast source routing packet\n",
                       linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
                forward_downward_data(conn, sender);
                break;
        default:
                printf("Packet type not recognized.\n");
        }
}

/* ----------------------------- Helpers for upward/downward ----------------------------- */

bool check_address_in_piggyback_block(uint8_t piggy_len, linkaddr_t node)
{
        printf("Checking piggy address: %02x:%02x\n", node.u8[0], node.u8[1]);
        tree_connection tc;
        uint8_t i;
        for (i = 0; i < piggy_len; i++)
        {
                memcpy(&tc,
                       packetbuf_dataptr() + sizeof(enum packet_type) + sizeof(upward_data_packet_header),
                       sizeof(tree_connection));
                if (linkaddr_cmp(&tc.node, &node))
                {
                        printf("ERROR: Checking piggy address found: %02x:%02x\n", node.u8[0], node.u8[1]);
                        return true;
                }
        }
        return false;
}

void forward_upward_data(my_collect_conn *conn, const linkaddr_t *sender)
{
        upward_data_packet_header hdr;
        memcpy(&hdr, packetbuf_dataptr() + sizeof(enum packet_type), sizeof(upward_data_packet_header));

        if (conn->is_sink == 1)
        {
                packetbuf_hdrreduce(sizeof(enum packet_type) + sizeof(upward_data_packet_header));
                if (PIGGYBACKING == 1)
                {
                        tree_connection tc;
                        if (hdr.piggy_len > MAX_PATH_LENGTH)
                        {
                                printf("ERROR: Piggy len=%d, path is supposed to be max %d\n",
                                       hdr.piggy_len, MAX_PATH_LENGTH);
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
                hdr.hops = hdr.hops + 1;
                if (PIGGYBACKING == 1 && !check_address_in_piggyback_block(hdr.piggy_len, linkaddr_node_addr))
                {
                        packetbuf_hdralloc(sizeof(tree_connection));
                        packetbuf_compact();
                        tree_connection tc = {.node = linkaddr_node_addr, .parent = conn->parent};
                        hdr.piggy_len = hdr.piggy_len + 1;

                        printf("Adding tree_connection to piggyinfo: key %02x:%02x value: %02x:%02x\n",
                               tc.node.u8[0], tc.node.u8[1], tc.parent.u8[0], tc.parent.u8[1]);

                        memcpy(packetbuf_hdrptr(), packetbuf_dataptr(), sizeof(enum packet_type));
                        memcpy(packetbuf_hdrptr() + sizeof(enum packet_type),
                               &hdr, sizeof(upward_data_packet_header));
                        memcpy(packetbuf_hdrptr() + sizeof(enum packet_type) + sizeof(upward_data_packet_header),
                               &tc, sizeof(tree_connection));
                }
                else
                {
                        memcpy(packetbuf_dataptr() + sizeof(enum packet_type),
                               &hdr, sizeof(upward_data_packet_header));
                }
                unicast_send(&conn->uc, &conn->parent);
        }
}

void forward_downward_data(my_collect_conn *conn, const linkaddr_t *sender)
{
        (void)sender;
        linkaddr_t addr;
        downward_data_packet_header hdr;

        memcpy(&hdr, packetbuf_dataptr() + sizeof(enum packet_type), sizeof(downward_data_packet_header));
        memcpy(&addr, packetbuf_dataptr() + sizeof(enum packet_type) + sizeof(downward_data_packet_header),
               sizeof(linkaddr_t));

        if (linkaddr_cmp(&addr, &linkaddr_node_addr))
        {
                if (hdr.path_len == 1)
                {
                        printf("PATH COMPLETE: Node %02x:%02x delivers packet from sink\n",
                               linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
                        packetbuf_hdrreduce(sizeof(enum packet_type) + sizeof(downward_data_packet_header) + sizeof(linkaddr_t));
                        conn->callbacks->sr_recv(conn, hdr.hops + 1);
                }
                else
                {
                        packetbuf_hdrreduce(sizeof(linkaddr_t));
                        hdr.path_len = hdr.path_len - 1;
                        enum packet_type pt = downward_data_packet;
                        memcpy(packetbuf_dataptr(), &pt, sizeof(enum packet_type));
                        memcpy(packetbuf_dataptr() + sizeof(enum packet_type), &hdr, sizeof(downward_data_packet_header));
                        memcpy(&addr, packetbuf_dataptr() + sizeof(enum packet_type) + sizeof(downward_data_packet_header),
                               sizeof(linkaddr_t));
                        unicast_send(&conn->uc, &addr);
                }
        }
        else
        {
                printf("ERROR: Node %02x:%02x received sr message. Was meant for node %02x:%02x\n",
                       linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1], addr.u8[0], addr.u8[1]);
        }
}
