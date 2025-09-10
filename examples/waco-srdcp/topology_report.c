#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "contiki.h"
#include "net/rime/rime.h"
#include "net/packetbuf.h"
#include "core/net/linkaddr.h"

#include "my_collect.h"
#include "routing_table.h"
#include "topology_report.h"

/* ===== Logs ===== */
#ifndef LOG_TOPO
#define LOG_TOPO 0
#endif

#if LOG_TOPO
#define TLOG(tag, fmt, ...) printf(tag ": " fmt "\n", ##__VA_ARGS__)
#else
#define TLOG(tag, fmt, ...) \
        do                  \
        {                   \
        } while (0)
#endif

#define TAG_TOPO "TOPO"
#define TAG_ROUTNG "ROUTING"

/* --------------------------------------------------------------------------
 * Helper: kiểm tra node có trong block DATA (len entries) chưa
 * - HEADER: [pt][len]
 * - DATA  : len * tree_connection
 * -------------------------------------------------------------------------- */
static bool topo_block_contains(uint8_t len, const linkaddr_t *node)
{
        const uint8_t *data = (const uint8_t *)packetbuf_dataptr();
        tree_connection tc;
        uint8_t i;

        for (i = 0; i < len; i++)
        {
                memcpy(&tc, data + i * sizeof(tree_connection), sizeof(tree_connection));
                if (linkaddr_cmp(&tc.node, node))
                {
                        return true;
                }
        }
        return false;
}

/* --------------------------------------------------------------------------
 * TIMER CALLBACK: hết thời gian hold thì gửi t-report riêng
 * -------------------------------------------------------------------------- */
void topology_report_hold_cb(void *ptr)
{
        my_collect_conn *conn = (my_collect_conn *)ptr;
        if (conn->treport_hold == 1)
        {
                conn->treport_hold = 0;
                /* forward=0: build gói mới và gửi */
                send_topology_report(conn, 0);
        }
}

/* --------------------------------------------------------------------------
 * Gửi topology report
 *  - forward==1: đang forward; nếu có hold thì append tc vào DATA và tăng len
 *  - forward==0: build mới 1 tc trong DATA và HEADER pt+len=1
 * -------------------------------------------------------------------------- */
void send_topology_report(my_collect_conn *conn, uint8_t forward)
{
        if (forward == 1)
        {
                /* Gói đã có sẵn HEADER pt+len và DATA tc[]. Đọc len từ HEADER. */
                uint8_t *hdr = (uint8_t *)packetbuf_hdrptr();
                uint8_t len = 0;

                if (hdr == NULL)
                {
                        /* Không có header – gói không đúng định dạng, vẫn forward nguyên trạng */
                        (void)unicast_send(&conn->uc, &conn->parent);
                        return;
                }

                memcpy(&len, hdr + sizeof(enum packet_type), sizeof(uint8_t));

                /* Nếu đang hold và block chưa chứa node hiện tại => append tc vào cuối DATA */
                if (conn->treport_hold == 1 && !topo_block_contains(len, &linkaddr_node_addr))
                {
                        uint16_t data_len = packetbuf_datalen();
                        uint16_t need = data_len + sizeof(tree_connection);

                        /* Kiểm tra dung lượng còn lại trong packetbuf */
                        if ((uint16_t)packetbuf_hdrlen() + need <= PACKETBUF_SIZE)
                        {
                                tree_connection tc = {.node = linkaddr_node_addr, .parent = conn->parent};
                                /* Append vào cuối DATA */
                                uint8_t *data = (uint8_t *)packetbuf_dataptr();
                                memcpy(data + data_len, &tc, sizeof(tree_connection));
                                packetbuf_set_datalen((uint16_t)(data_len + sizeof(tree_connection)));

                                /* Tăng len trong HEADER */
                                len = (uint8_t)(len + 1);
                                memcpy(hdr + sizeof(enum packet_type), &len, sizeof(uint8_t));

                                TLOG(TAG_TOPO, "append tc (%02u:%02u -> %02u:%02u), new len=%u",
                                     tc.node.u8[0], tc.node.u8[1], tc.parent.u8[0], tc.parent.u8[1], (unsigned)len);

                                /* Tắt hold vì đã piggy được vào gói đang đi qua */
                                conn->treport_hold = 0;
                                ctimer_stop(&conn->treport_hold_timer);
                        }
                        else
                        {
                                TLOG(TAG_TOPO, "skip append: no packetbuf space (hdr=%u, data=%u, need=%u)",
                                     (unsigned)packetbuf_hdrlen(),
                                     (unsigned)packetbuf_datalen(),
                                     (unsigned)(sizeof(tree_connection)));
                        }
                }

                /* Forward lên parent */
                (void)unicast_send(&conn->uc, &conn->parent);
                return;
        }

        /* forward==0: build gói mới của node hiện tại */
        {
                enum packet_type pt = topology_report;
                uint8_t len = 1;
                tree_connection tc = {.node = linkaddr_node_addr, .parent = conn->parent};

                /* DATA: 1 tc */
                packetbuf_clear();
                packetbuf_set_datalen(sizeof(tree_connection));
                memcpy(packetbuf_dataptr(), &tc, sizeof(tree_connection));

                /* HEADER: pt + len */
                packetbuf_hdralloc((uint16_t)(sizeof(enum packet_type) + sizeof(uint8_t)));
                uint8_t *hdr = (uint8_t *)packetbuf_hdrptr();
                memcpy(hdr, &pt, sizeof(enum packet_type));
                memcpy(hdr + sizeof(enum packet_type), &len, sizeof(uint8_t));

                TLOG(TAG_TOPO, "node %02u:%02u send t-report (parent=%02u:%02u)",
                     linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
                     conn->parent.u8[0], conn->parent.u8[1]);

                (void)unicast_send(&conn->uc, &conn->parent);
        }
}

/* --------------------------------------------------------------------------
 * Ở sink: đọc len từ HEADER, bỏ HEADER, duyệt block tc[] và nạp dict
 * -------------------------------------------------------------------------- */
void deliver_topology_report_to_sink(my_collect_conn *conn)
{
        uint8_t *hdr = (uint8_t *)packetbuf_hdrptr();
        uint8_t len = 0;
        uint8_t i;

        if (hdr == NULL)
        {
                TLOG(TAG_TOPO, "[SINK] invalid topology report: no header");
                return;
        }

        memcpy(&len, hdr + sizeof(enum packet_type), sizeof(uint8_t));

        /* Bỏ pt+len khỏi HEADER để DATA trỏ đúng vào block tc[] */
        packetbuf_hdrreduce((uint16_t)(sizeof(enum packet_type) + sizeof(uint8_t)));

        TLOG(TAG_TOPO, "[SINK] received %u topology report entries", (unsigned)len);

        /* Đảm bảo không vượt quá kích thước DATA thực tế (phòng vệ) */
        uint16_t dlen = packetbuf_datalen();
        uint16_t max_entries = dlen / sizeof(tree_connection);
        if (len > max_entries)
        {
                len = (uint8_t)max_entries;
        }

        for (i = 0; i < len; i++)
        {
                tree_connection tc;
                memcpy(&tc, ((uint8_t *)packetbuf_dataptr()) + i * sizeof(tree_connection),
                       sizeof(tree_connection));

                /* vệ sinh byte cao (nếu dùng 2B addr) & lọc rác */
                /* lọc rác theo địa chỉ rỗng (đủ 16-bit) */
                if (linkaddr_cmp(&tc.node, &linkaddr_null) ||
                    linkaddr_cmp(&tc.parent, &linkaddr_null))
                {
                        continue;
                }

                TLOG(TAG_TOPO, "[SINK] update parent: %02u:%02u -> %02u:%02u",
                     tc.node.u8[0], tc.node.u8[1], tc.parent.u8[0], tc.parent.u8[1]);

                dict_add(&conn->routing_table, tc.node, tc.parent);
        }

        print_dict_state(&conn->routing_table);
}
