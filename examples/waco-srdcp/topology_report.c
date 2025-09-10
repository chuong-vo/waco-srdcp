#include <stdbool.h>
#include <stdio.h>
#include "my_collect.h"
#include "routing_table.h"
#include <string.h>
#include <stdint.h>
/* ------------------------------------ LOG Tags / Helper ------------------------------------ */
#define TAG_TOPO "TOPO"
#define TAG_ROUTING "ROUTING"
/* ===== Logging toggle for my_collect.c ===== */
#ifndef LOG_TOPO
#define LOG_TOPO 0 /* 1: bật log; 0: tắt log */
#endif

#if LOG_TOPO
#define LOG(tag, fmt, ...) printf(tag ": " fmt "\n", ##__VA_ARGS__)
#else
#define LOG(tag, fmt, ...) \
        do                 \
        {                  \
        } while (0)
#endif

#define LOG(tag, fmt, ...) printf(tag ": " fmt "\n", ##__VA_ARGS__)

/* --------------------------------------------------------------------------
 *                             TIMER CALLBACKS
 * -------------------------------------------------------------------------- */

/* Called when the waiting time to forward a topology report has ended. */
void topology_report_hold_cb(void *ptr)
{
        struct my_collect_conn *conn = ptr;
        if (conn->treport_hold == 1)
        {
                conn->treport_hold = 0;
                /* 0 = root topology report (not in forwarding mode) */
                send_topology_report(conn, 0);
        }
}

/* --------------------------------------------------------------------------
 *                        TOPOLOGY REPORT MANAGEMENT
 * -------------------------------------------------------------------------- */

/* Check whether 'node' already appears in the report block (length = len entries). */
bool check_topology_report_address(my_collect_conn *conn, linkaddr_t node, uint8_t len)
{
        (void)conn;
        LOG(TAG_TOPO, "checking report block for %02u:%02u", node.u8[0], node.u8[1]);

        tree_connection tc;
        uint8_t i;
        for (i = 0; i < len; i++)
        {
                memcpy(&tc,
                       packetbuf_dataptr() + sizeof(enum packet_type) + sizeof(uint8_t) + sizeof(tree_connection) * i,
                       sizeof(tree_connection));
                /* Compare against the provided 'node' (correct semantics) */
                if (linkaddr_cmp(&tc.node, &node))
                {
                        LOG(TAG_TOPO, "already contains %02u:%02u", node.u8[0], node.u8[1]);
                        return true;
                }
        }
        return false;
}

/*
 * Send a topology report to the parent.
 * - Sent when parent changes, or when no piggyback has happened for a while.
 * - Also used to forward a report received from a child toward the sink.
 */
void send_topology_report(my_collect_conn *conn, uint8_t forward)
{
        /* Forward an incoming report upward */
        if (forward == 1)
        {
                uint8_t len;
                memcpy(&len, packetbuf_dataptr() + sizeof(enum packet_type), sizeof(uint8_t));

                /* If we are about to send our own report soon, piggyback it here instead */
                if (conn->treport_hold == 1 &&
                    !check_topology_report_address(conn, linkaddr_node_addr, len))
                {

                        LOG(TAG_TOPO, "append (node=%02u:%02u parent=%02u:%02u)",
                            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
                            conn->parent.u8[0], conn->parent.u8[1]);

                        enum packet_type pt = topology_report;
                        tree_connection tc = {.node = linkaddr_node_addr, .parent = conn->parent};
                        len = (uint8_t)(len + 1);

                        /* Prepend one tree_connection to the header */
                        packetbuf_hdralloc(sizeof(tree_connection));
                        packetbuf_compact(); /* make header and data contiguous */

                        memcpy(packetbuf_hdrptr(), &pt, sizeof(enum packet_type));
                        memcpy(packetbuf_hdrptr() + sizeof(enum packet_type), &len, sizeof(uint8_t));
                        memcpy(packetbuf_hdrptr() + sizeof(enum packet_type) + sizeof(uint8_t),
                               &tc, sizeof(tree_connection));

                        conn->treport_hold = 0;
                        ctimer_stop(&conn->treport_hold_timer); /* no need for a dedicated packet anymore */
                }

                /* Send upward to parent */
                (void)unicast_send(&conn->uc, &conn->parent);
                return;
        }

        /* Build this node's topology report and send to parent */
        LOG(TAG_TOPO, "node %02u:%02u sending topology report",
            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);

        enum packet_type pt = topology_report;
        tree_connection tc = {.node = linkaddr_node_addr, .parent = conn->parent};
        uint8_t len = 1;

        packetbuf_clear();
        packetbuf_set_datalen(sizeof(tree_connection));
        memcpy(packetbuf_dataptr(), &tc, sizeof(tree_connection));

        packetbuf_hdralloc(sizeof(enum packet_type) + sizeof(uint8_t));
        memcpy(packetbuf_hdrptr(), &pt, sizeof(enum packet_type));
        memcpy(packetbuf_hdrptr() + sizeof(enum packet_type), &len, sizeof(uint8_t));

        (void)unicast_send(&conn->uc, &conn->parent);
}

/*
 * At the sink: parse the tree_connection block and update parents.
 */
void deliver_topology_report_to_sink(my_collect_conn *conn)
{
        uint8_t len;
        tree_connection tc;

        /* Remove (pt + len) header so data starts at the connection block */
        memcpy(&len, packetbuf_dataptr() + sizeof(enum packet_type), sizeof(uint8_t));
        packetbuf_hdrreduce(sizeof(enum packet_type) + sizeof(uint8_t));

        LOG(TAG_TOPO, "[SINK]: received %u topology report(s)", (unsigned)len);

        uint8_t i;
        /* ---- PATCH START (topology_report.c) ---- */
        for (i = 0; i < len; i++)
        {
                memcpy(&tc, packetbuf_dataptr() + sizeof(tree_connection) * i, sizeof(tree_connection));
                /* vệ sinh byte cao và bỏ entry rác */
                tc.node.u8[1] = 0x00;
                tc.parent.u8[1] = 0x00;
                if (tc.node.u8[0] == 0 || tc.parent.u8[0] == 0)
                {
                        /* printf("[TOPO] drop invalid entry node=%02u:%02u parent=%02u:%02u\n",
                           tc.node.u8[0], tc.node.u8[1], tc.parent.u8[0], tc.parent.u8[1]); */
                        continue;
                }
                printf("Sink: received topology report. Updating parent of node %02u:%02u\n",
                       tc.node.u8[0], tc.node.u8[1]);
                dict_add(&conn->routing_table, tc.node, tc.parent);
        }
        /* ---- PATCH END ---- */

        print_dict_state(&conn->routing_table);
}
