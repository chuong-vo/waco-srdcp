#ifndef MY_COLLECT_H
#define MY_COLLECT_H

#include <stdbool.h>
#include <stdlib.h>
#include "contiki.h"
#include "sys/ctimer.h"
#include "net/rime/rime.h"
#include "net/netstack.h"
#include "core/net/linkaddr.h"

/* ===== Feature toggles ===== */
#define TOPOLOGY_REPORT 1
#define PIGGYBACKING 1

/* ===== Topology / dict limits ===== */
#define MAX_NODES 30
#define MAX_PATH_LENGTH 10

/* ===== Beacon / timing ===== */
#define BEACON_INTERVAL (CLOCK_SECOND * 10)
#define BEACON_FORWARD_DELAY (random_rand() % (CLOCK_SECOND * 3)) /* ~0..3s */
#define TOPOLOGY_REPORT_HOLD_TIME (CLOCK_SECOND * 16)             /* ~0.65× chu kỳ */
/* Lọc hàng xóm yếu nếu muốn (không dùng trong PRR-thuần) */
#define RSSI_THRESHOLD -86
#define MAX_RETRANSMISSIONS 3

/* Sink mặc định: 01:00 (được định nghĩa trong my_collect.c) */
extern const linkaddr_t sink_addr;

/* ===== Packet types ===== */
enum packet_type
{
        upward_data_packet = 0,
        downward_data_packet = 1,
        topology_report = 2
};

/* --------------------------------------------------------------------
 *                           DICT STRUCTS
 * ------------------------------------------------------------------ */
typedef struct DictEntry
{
        linkaddr_t key;   /* node */
        linkaddr_t value; /* parent */
} DictEntry;

typedef struct TreeDict
{
        int len;
        DictEntry entries[MAX_NODES];
        linkaddr_t tree_path[MAX_PATH_LENGTH];
} TreeDict;

/* --------------------------------------------------------------------
 *                  PUBLIC CONNECTION OBJECT & CALLBACKS
 * ------------------------------------------------------------------ */
struct my_collect_conn
{
        /* Radio pipes */
        struct broadcast_conn bc;
        struct unicast_conn uc;

        /* Timers */
        struct ctimer beacon_timer;
        struct ctimer treport_hold_timer;

        /* NEW: kiểm tra timeout của parent định kỳ */
        struct ctimer parent_to_timer;

        /* Parent & metric */
        linkaddr_t parent;
        uint16_t metric; /* hop-count (0 nếu sink, 0xFFFF = chưa vào cây) */
        uint16_t beacon_seqn;

        /* Role */
        uint8_t is_sink; /* 1 = sink, 0 = node */

        /* NEW: trạng thái ổn định hoá chọn parent */
        clock_time_t parent_last_seen; /* lần cuối nghe beacon từ parent */
        clock_time_t dwell_deadline;   /* hạn dwell để cho phép đổi ngang hop */

        /* T-report batching */
        uint8_t treport_hold;

        /* Callbacks */
        const struct my_collect_callbacks *callbacks;

        /* Routing dict (dùng ở sink) */
        TreeDict routing_table;
};
typedef struct my_collect_conn my_collect_conn;

struct my_collect_callbacks
{
        /* UL deliver at sink */
        void (*recv)(const linkaddr_t *originator, uint8_t hops);
        /* DL deliver at node (source routing) */
        void (*sr_recv)(struct my_collect_conn *ptr, uint8_t hops);
};

/* --------------------------------------------------------------------
 *                            PUBLIC API
 * ------------------------------------------------------------------ */

/* Khởi tạo kết nối SRDCP:
 *  - channels: kênh bắt đầu C (SRDCP dùng C và C+1)
 *  - is_sink : true = sink, false = node
 *  - callbacks: bộ callback tuỳ vai trò
 */
void my_collect_open(struct my_collect_conn *conn,
                     uint16_t channels,
                     bool is_sink,
                     const struct my_collect_callbacks *callbacks);

/* Gửi UL (node -> sink). Trả 0 nếu không gửi được. */
int my_collect_send(struct my_collect_conn *conn);

/* Gửi DL theo SR (sink -> dest). Trả 0 nếu không gửi được. */
int sr_send(struct my_collect_conn *conn, const linkaddr_t *dest);

/* Topology report (được triển khai trong topology_report.c) */
void send_topology_report(my_collect_conn *conn, uint8_t forwarding);

/* --------------------------------------------------------------------
 *             Telemetry / Hooks / PRR accessors (cho App)
 * ------------------------------------------------------------------ */

/* App-hook: gọi khi overhear beacon (để cập nhật bảng lân cận phía app).
 * Không thay đổi logic SRDCP. Có weak default trong my_collect.c.
 */
__attribute__((weak)) void srdcp_app_beacon_observed(const linkaddr_t *sender,
                                                     uint16_t metric,
                                                     int16_t rssi,
                                                     uint8_t lqi);

/* Đọc PRR% (0..100) và số mẫu (exp) mà stack SRDCP học được cho địa chỉ a. */
uint16_t my_collect_get_prr_percent(const linkaddr_t *a);
uint16_t my_collect_get_prr_samples(const linkaddr_t *a);

/* --------------------------------------------------------------------
 *                GÓI TIN – HEADER / PAYLOAD STRUCTS
 * ------------------------------------------------------------------ */
typedef struct tree_connection
{
        linkaddr_t node;
        linkaddr_t parent;
} __attribute__((packed)) tree_connection;

typedef struct beacon_msg
{
        uint16_t seqn;
        uint16_t metric; /* hop-count của sender tới sink (0 nếu chính sink) */
} __attribute__((packed)) beacon_msg;

typedef struct upward_data_packet_header
{
        linkaddr_t source;
        uint8_t hops;
        uint8_t piggy_len; /* số entry piggy-back tree_connection (0 nếu không) */
} __attribute__((packed)) upward_data_packet_header;

typedef struct downward_data_packet_header
{
        uint8_t hops;
        uint8_t path_len;
} __attribute__((packed)) downward_data_packet_header;

#endif /* MY_COLLECT_H */
