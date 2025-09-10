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

/* ------------------------------------------------------------------------------------- */
/* Sink address (node 1 là sink) */
const linkaddr_t sink_addr = {{0x01, 0x00}};

/* ------------------------------------------------------------------------------------- */
/* ===== PRR & Parent selection params (không float) ===== */
#ifndef NEI_MAX
#define NEI_MAX 24
#endif

#define PRR_SATURATE_THRESHOLD 1000 /* chống tràn bộ đếm */
#define PRR_EXP_MIN 3               /* tối thiểu mẫu để tin PRR (hop tốt hơn/hop ngang) */
#define PRR_MIN 30                  /* % sàn khi hop tốt hơn */
#define PRR_SWITCH_MARGIN 10        /* % chênh lệch tối thiểu khi hop ngang */
#define K_CONSEC_BEACON 2           /* cần K beacon liên tiếp thỏa điều kiện khi hop ngang */

#ifndef BEACON_INTERVAL
/* Nếu BEACON_INTERVAL không có sẵn, giả lập ~15s */
#define BEACON_INTERVAL (15 * CLOCK_SECOND)
#endif

#define DWELL_CYCLES 3          /* sau đổi parent phải ở yên N chu kỳ */
#define PARENT_TIMEOUT_CYCLES 3 /* mất N chu kỳ beacon từ parent thì rơi cây */
#define SOFT_STALE_SEC (60)     /* bỏ qua ứng viên quá cũ khi xét parent */
#define PARENT_TIMEOUT_TICKS ((clock_time_t)(PARENT_TIMEOUT_CYCLES) * (clock_time_t)BEACON_INTERVAL)
#define DWELL_TICKS ((clock_time_t)(DWELL_CYCLES) * (clock_time_t)BEACON_INTERVAL)

#ifndef MAX_HOPS
#define MAX_HOPS 64 /* TTL cho UL */
#endif

/* ------------------------------------------------------------------------------------- */
/* ===== PRR table (nội bộ) ===== */
typedef struct
{
        linkaddr_t addr;
        uint16_t last_seq;
        uint16_t rx;            /* beacon thực nhận */
        uint16_t exp;           /* beacon kỳ vọng (từ chênh lệch seq) */
        clock_time_t last_seen; /* lần cuối nghe beacon (để bỏ qua ứng viên stale) */
        uint8_t ok_streak;      /* đếm liên tiếp thỏa điều kiện (hop ngang) */
        uint8_t used;
} prr_entry_t;

static prr_entry_t prr_tbl[NEI_MAX];

static prr_entry_t *prr_lookup_or_add(const linkaddr_t *a)
{
        int i;
        prr_entry_t *free_e = NULL;
        for (i = 0; i < NEI_MAX; i++)
        {
                if (prr_tbl[i].used && linkaddr_cmp(&prr_tbl[i].addr, a))
                        return &prr_tbl[i];
                if (!prr_tbl[i].used && free_e == NULL)
                        free_e = &prr_tbl[i];
        }
        if (free_e)
        {
                memset(free_e, 0, sizeof(*free_e));
                linkaddr_copy(&free_e->addr, a);
                free_e->used = 1;
                return free_e;
        }
        return NULL; /* đầy bảng: bỏ qua */
}

static void prr_update_on_beacon(const linkaddr_t *a, uint16_t seq)
{
        prr_entry_t *e = prr_lookup_or_add(a);
        if (!e)
                return;

        /* cập nhật last_seen mỗi lần nghe beacon */
        e->last_seen = clock_time();

        if (e->exp == 0 && e->rx == 0)
        {
                e->last_seq = seq;
                e->rx = 1;
                e->exp = 1;
                return;
        }

        uint16_t delta = (uint16_t)(seq - e->last_seq);
        if (delta == 0)
        {
                /* trùng seq: bỏ */
                return;
        }
        if (delta > 1000)
        {
                /* nhảy seq lớn (reboot...) -> reset cửa sổ */
                e->last_seq = seq;
                e->rx = 1;
                e->exp = 1;
                return;
        }

        e->exp += delta;
        e->rx += 1;
        e->last_seq = seq;

        if (e->exp > PRR_SATURATE_THRESHOLD || e->rx > PRR_SATURATE_THRESHOLD)
        {
                e->exp = (e->exp + 1) >> 1;
                e->rx = (e->rx + 1) >> 1;
                if (e->exp == 0)
                        e->exp = 1;
        }
}

static uint16_t prr_percent_of(const linkaddr_t *a)
{
        int i;
        for (i = 0; i < NEI_MAX; i++)
        {
                if (prr_tbl[i].used && linkaddr_cmp(&prr_tbl[i].addr, a))
                {
                        uint16_t exp = prr_tbl[i].exp ? prr_tbl[i].exp : 1u;
                        return (uint16_t)((uint32_t)prr_tbl[i].rx * 100u / exp);
                }
        }
        return 0;
}

static uint16_t prr_samples_of(const linkaddr_t *a)
{
        int i;
        for (i = 0; i < NEI_MAX; i++)
        {
                if (prr_tbl[i].used && linkaddr_cmp(&prr_tbl[i].addr, a))
                {
                        return prr_tbl[i].exp;
                }
        }
        return 0;
}

/* ===== Expose API cho ứng dụng ===== */
uint16_t my_collect_get_prr_percent(const linkaddr_t *addr)
{
        return prr_percent_of(addr);
}
uint16_t my_collect_get_prr_samples(const linkaddr_t *addr)
{
        return prr_samples_of(addr);
}

/*--------------------------------------------------------------------------------------*/
/* Forward declarations (for clean initialization order) */
static void beacon_timer_cb(void *ptr);
static void send_beacon(struct my_collect_conn *conn);
static void bc_recv(struct broadcast_conn *bc_conn, const linkaddr_t *sender);
static void uc_recv(struct unicast_conn *uc_conn, const linkaddr_t *sender);

int sr_send(struct my_collect_conn *conn, const linkaddr_t *dest);
int my_collect_send(struct my_collect_conn *conn);

static bool check_address_in_piggyback_block(uint8_t piggy_len, linkaddr_t node);
static void forward_upward_data(struct my_collect_conn *conn, const linkaddr_t *sender);
static void forward_downward_data(struct my_collect_conn *conn, const linkaddr_t *sender);

/*--------------------------------------------------------------------------------------*/
/* Callback structures */
static struct broadcast_callbacks bc_cb = {.recv = bc_recv};
static struct unicast_callbacks uc_cb = {.recv = uc_recv};

/* ------------------------------------------------------------------------------------- */
/* Parent timeout timer (cho node thường) */
static void parent_to_cb(void *ptr)
{
        struct my_collect_conn *conn = (struct my_collect_conn *)ptr;
        if (conn->is_sink)
        {
                return;
        }
        /* nếu có parent, kiểm tra timeout */
        if (!linkaddr_cmp(&conn->parent, &linkaddr_null))
        {
                clock_time_t now = clock_time();
                if (now - conn->parent_last_seen >= PARENT_TIMEOUT_TICKS)
                {
                        LOG(TAG_COLLECT, "parent timeout -> drop parent %02u:%02u",
                            conn->parent.u8[0], conn->parent.u8[1]);
                        linkaddr_copy(&conn->parent, &linkaddr_null);
                        conn->metric = 65535;
                }
        }
        /* lặp lại kiểm tra */
        ctimer_set(&conn->parent_to_timer, BEACON_INTERVAL, parent_to_cb, conn);
}

void my_collect_open(struct my_collect_conn *conn, uint16_t channels,
                     bool is_sink, const struct my_collect_callbacks *callbacks)
{
        linkaddr_copy(&conn->parent, &linkaddr_null);
        conn->metric = 65535; /* chưa vào cây */
        conn->beacon_seqn = 0;
        conn->callbacks = callbacks;
        conn->treport_hold = 0;
        conn->is_sink = is_sink ? 1 : 0;

        conn->dwell_deadline = 0;
        conn->parent_last_seen = 0;

        broadcast_open(&conn->bc, channels, &bc_cb);
        unicast_open(&conn->uc, channels + 1, &uc_cb);

        if (conn->is_sink)
        {
                conn->metric = 0;
                conn->routing_table.len = 0;
                ctimer_set(&conn->beacon_timer, CLOCK_SECOND, beacon_timer_cb, conn);
        }
        else
        {
                /* node thường: vòng kiểm tra parent-timeout */
                ctimer_set(&conn->parent_to_timer, BEACON_INTERVAL, parent_to_cb, conn);
        }
}

/* ------------------------------------ BEACON Management ------------------------------------ */

static void beacon_timer_cb(void *ptr)
{
        struct my_collect_conn *conn = ptr;
        send_beacon(conn);
        if (conn->is_sink == 1)
        {
                ctimer_set(&conn->beacon_timer, BEACON_INTERVAL, beacon_timer_cb, conn);
                conn->beacon_seqn = conn->beacon_seqn + 1;
        }
}

static void send_beacon(struct my_collect_conn *conn)
{
        struct beacon_msg beacon = {.seqn = conn->beacon_seqn, .metric = conn->metric};

        packetbuf_clear();
        packetbuf_copyfrom(&beacon, sizeof(beacon));
        LOG(TAG_BEACON, "send seq=%u metric=%u", (unsigned)conn->beacon_seqn, (unsigned)conn->metric);
        broadcast_send(&conn->bc);
}

/* ============================ Parent decision helpers ============================ */
static bool is_candidate_stale(const linkaddr_t *cand)
{
        /* bỏ qua ứng viên quá cũ (SOFT_STALE_SEC) */
        int i;
        clock_time_t now = clock_time();
        for (i = 0; i < NEI_MAX; i++)
        {
                if (prr_tbl[i].used && linkaddr_cmp(&prr_tbl[i].addr, cand))
                {
                        if (now - prr_tbl[i].last_seen > (clock_time_t)(SOFT_STALE_SEC * CLOCK_SECOND))
                        {
                                return true;
                        }
                        return false;
                }
        }
        return false; /* nếu chưa có entry, không coi là stale (sẽ bị chặn bởi PRR_EXP_MIN) */
}

static void start_dwell(struct my_collect_conn *conn)
{
        conn->dwell_deadline = clock_time() + DWELL_TICKS;
}

static bool dwell_active(const struct my_collect_conn *conn)
{
        return clock_time() < conn->dwell_deadline;
}

static void set_parent(struct my_collect_conn *conn, const linkaddr_t *p, uint16_t cand_metric, uint16_t prr_cand)
{
        bool changed = !linkaddr_cmp(&conn->parent, p);
        linkaddr_copy(&conn->parent, p);
        conn->metric = cand_metric;
        conn->parent_last_seen = clock_time();
        start_dwell(conn);

        LOG(TAG_COLLECT, "parent -> %02u:%02u (hop=%u, PRR=%u%%)",
            conn->parent.u8[0], conn->parent.u8[1], (unsigned)cand_metric, (unsigned)prr_cand);

        if (changed && TOPOLOGY_REPORT && !conn->is_sink)
        {
                conn->treport_hold = 1;
                ctimer_stop(&conn->treport_hold_timer);
                ctimer_set(&conn->treport_hold_timer, TOPOLOGY_REPORT_HOLD_TIME,
                           topology_report_hold_cb, conn);
        }
}

/* ------------------------------------ Broadcast receive ------------------------------------ */

static void bc_recv(struct broadcast_conn *bc_conn, const linkaddr_t *sender)
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

        LOG(TAG_BEACON, "rx from=%02u:%02u seq=%u metric=%u rssi=%d lqi=%u",
            sender->u8[0], sender->u8[1],
            (unsigned)beacon.seqn, (unsigned)beacon.metric, (int)rssi, (unsigned)lqi);

        /* Hook ứng dụng (CSV/neighbor...) */
        srdcp_app_beacon_observed(sender, beacon.metric, rssi, lqi);

        /* CẬP NHẬT PRR cho cả SINK & NODE (đặt trước return) */
        prr_update_on_beacon(sender, beacon.seqn);

        /* Nếu beacon từ chính parent: đồng bộ metric + gia hạn parent_last_seen */
        if (linkaddr_cmp(&conn->parent, sender) && beacon.metric != 65535u)
        {
                conn->metric = (uint16_t)(beacon.metric + 1u);
                conn->parent_last_seen = clock_time();
        }

        if (conn->is_sink)
        {
                /* sink không xét parent/forward beacon nhận được */
                return;
        }

        /* Đồng bộ seq để forward */
        if (conn->beacon_seqn < beacon.seqn)
        {
                conn->beacon_seqn = beacon.seqn;
        }

        /* ---------------- HOP trước, PRR sau (có dwell/timeout/margin) ---------------- */
        /* chỉ xét ứng viên đã thuộc cây */
        if (beacon.metric != 65535u)
        {

                const uint16_t cand_metric = (uint16_t)(beacon.metric + 1u);
                const uint16_t cur_metric = conn->metric;
                const bool no_parent = linkaddr_cmp(&conn->parent, &linkaddr_null);

                /* bỏ qua ứng viên quá cũ */
                if (is_candidate_stale(sender))
                {
                        LOG(TAG_COLLECT, "skip cand=%02u:%02u (stale)", sender->u8[0], sender->u8[1]);
                        goto schedule_forward;
                }

                /* lấy PRR & samples */
                const uint16_t prr_cand = prr_percent_of(sender);
                const uint16_t prr_par = no_parent ? 0 : prr_percent_of(&conn->parent);
                const uint16_t exp_cand = prr_samples_of(sender);

                /* cập nhật ok_streak (hop ngang) */
                prr_entry_t *e = prr_lookup_or_add(sender);
                // if (e)
                // {
                //         /* mặc định reset, nếu đủ điều kiện hop ngang + margin sẽ tăng sau */
                //         e->ok_streak = 0;
                // }

                /* điều kiện đổi parent */
                bool accept = false;

                if (no_parent)
                {
                        /* bám nhanh vào cây lần đầu: yêu cầu ứng viên hợp lệ + tối thiểu mẫu */
                        accept = (exp_cand >= PRR_EXP_MIN);
                }
                else if (cand_metric < cur_metric)
                {
                        /* hop tốt hơn: yêu cầu PRR tối thiểu + đủ mẫu */
                        accept = (exp_cand >= PRR_EXP_MIN) && (prr_cand >= PRR_MIN);
                }
                else if (cand_metric == cur_metric)
                {
                        if (!dwell_active(conn))
                        {
                                bool cond = (exp_cand >= PRR_EXP_MIN) &&
                                            (prr_cand >= (uint16_t)(prr_par + PRR_SWITCH_MARGIN));

                                if (e)
                                {
                                        if (cond)
                                        {
                                                if (e->ok_streak < 255)
                                                        e->ok_streak++; /* tránh tràn */
                                        }
                                        else
                                        {
                                                e->ok_streak = 0;
                                        }
                                        if (e->ok_streak >= K_CONSEC_BEACON)
                                        {
                                                accept = true;
                                        }
                                }
                                else
                                {
                                        /* chưa có entry PRR -> chưa đủ mẫu => coi như cond=false */
                                }
                        }
                        else
                        {
                                LOG(TAG_COLLECT, "dwell active: skip equal-hop switch");
                        }
                }
                else
                {
                        /* hop tệ hơn: không bao giờ nhận để giữ DAG */
                        accept = false;
                }

                if (accept)
                {
                        set_parent(conn, sender, cand_metric, prr_cand);
                }
                else
                {
                        LOG(TAG_COLLECT, "keep parent (cand hop=%u PRR=%u%% exp=%u; cur hop=%u PRR=%u%%)",
                            (unsigned)cand_metric, (unsigned)prr_cand, (unsigned)exp_cand,
                            (unsigned)cur_metric, (unsigned)prr_par);
                }
        }
        else
        {
                LOG(TAG_COLLECT, "ignore (candidate not in tree)");
        }

schedule_forward:
        /* chỉ forward beacon khi đã vào cây */
        if (conn->metric != 65535u)
        {
                ctimer_set(&conn->beacon_timer, BEACON_FORWARD_DELAY, beacon_timer_cb, conn);
                LOG(TAG_COLLECT, "schedule beacon forward after %u ticks", (unsigned)BEACON_FORWARD_DELAY);
        }
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
                return 0;
        }

        if (PIGGYBACKING == 1)
        {
                packetbuf_hdralloc(sizeof(enum packet_type) + sizeof(upward_data_packet_header) + sizeof(tree_connection));
                memcpy(packetbuf_hdrptr(), &pt, sizeof(enum packet_type));
                memcpy(packetbuf_hdrptr() + sizeof(enum packet_type), &hdr, sizeof(upward_data_packet_header));
                memcpy(packetbuf_hdrptr() + sizeof(enum packet_type) + sizeof(upward_data_packet_header), &tc, sizeof(tree_connection));
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
        {
                LOG(TAG_SRDCP, "no route to %02u:%02u (downlink dropped)", dest->u8[0], dest->u8[1]);
                return 0;
        }

        enum packet_type pt = downward_data_packet;
        downward_data_packet_header hdr = {.hops = 0, .path_len = (uint8_t)path_len};

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

static void uc_recv(struct unicast_conn *uc_conn, const linkaddr_t *sender)
{
        struct my_collect_conn *conn =
            (struct my_collect_conn *)(((uint8_t *)uc_conn) - offsetof(struct my_collect_conn, uc));

        enum packet_type pt;
        memcpy(&pt, packetbuf_dataptr(), sizeof(enum packet_type));

        LOG(TAG_UC, "rx type=%d from=%02u:%02u", (int)pt, sender->u8[0], sender->u8[1]);

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
                                send_topology_report(conn, 1);
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

static bool check_address_in_piggyback_block(uint8_t piggy_len, linkaddr_t node)
{
        uint8_t i;
        tree_connection tc;
        for (i = 0; i < piggy_len; i++)
        {
                memcpy(&tc,
                       packetbuf_dataptr() + sizeof(enum packet_type) + sizeof(upward_data_packet_header) + sizeof(tree_connection) * i,
                       sizeof(tree_connection));
                if (linkaddr_cmp(&tc.node, &node))
                {
                        return true;
                }
        }
        return false;
}

static void forward_upward_data(struct my_collect_conn *conn, const linkaddr_t *sender)
{
        upward_data_packet_header hdr;
        memcpy(&hdr, packetbuf_dataptr() + sizeof(enum packet_type), sizeof(upward_data_packet_header));

        /* chống loop: nếu nhận UL từ CHÍNH parent của mình -> drop */
        if (!conn->is_sink && linkaddr_cmp(sender, &conn->parent))
        {
                LOG(TAG_UL, "drop (rx UL from my parent)");
                return;
        }
        /* TTL */
        if (hdr.hops >= MAX_HOPS)
        {
                LOG(TAG_UL, "drop (TTL exceeded)");
                return;
        }

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

                                if (!linkaddr_cmp(&tc.node, &linkaddr_null) &&
                                    !linkaddr_cmp(&tc.parent, &linkaddr_null))
                                {
                                        dict_add(&conn->routing_table, tc.node, tc.parent);
                                }
                        }
                        packetbuf_hdrreduce(sizeof(tree_connection) * hdr.piggy_len);
                }
                conn->callbacks->recv(&hdr.source, (uint8_t)(hdr.hops + 1));
        }
        else
        {
                hdr.hops++;

                if (PIGGYBACKING == 1 &&
                    !linkaddr_cmp(&conn->parent, &linkaddr_null) &&
                    !check_address_in_piggyback_block(hdr.piggy_len, linkaddr_node_addr))
                {

                        packetbuf_hdralloc(sizeof(tree_connection));
                        packetbuf_compact();
                        tree_connection tc = {.node = linkaddr_node_addr, .parent = conn->parent};
                        hdr.piggy_len = (uint8_t)(hdr.piggy_len + 1);

                        memcpy(packetbuf_hdrptr(), packetbuf_dataptr(), sizeof(enum packet_type));
                        memcpy(packetbuf_hdrptr() + sizeof(enum packet_type), &hdr, sizeof(upward_data_packet_header));
                        memcpy(packetbuf_hdrptr() + sizeof(enum packet_type) + sizeof(upward_data_packet_header), &tc, sizeof(tree_connection));
                }
                else
                {
                        memcpy(packetbuf_dataptr() + sizeof(enum packet_type), &hdr, sizeof(upward_data_packet_header));
                }
                unicast_send(&conn->uc, &conn->parent);
        }
}

static void forward_downward_data(struct my_collect_conn *conn, const linkaddr_t *sender)
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
                        LOG(TAG_SRDCP, "path complete at %02u:%02u; deliver",
                            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
                        packetbuf_hdrreduce(sizeof(enum packet_type) + sizeof(downward_data_packet_header) + sizeof(linkaddr_t));
                        conn->callbacks->sr_recv(conn, (uint8_t)(hdr.hops + 1));
                }
                else
                {
                        packetbuf_hdrreduce(sizeof(linkaddr_t));
                        hdr.path_len = (uint8_t)(hdr.path_len - 1);
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
                LOG(TAG_SRDCP, "drop (for=%02u:%02u; I'm=%02u:%02u)",
                    addr.u8[0], addr.u8[1], linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
        }
}
