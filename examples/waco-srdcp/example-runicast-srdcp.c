/*
 * SRDCP-integrated runicast-like example for WaCo + COOJA
 * - Upward traffic (many-to-one): nodes -> sink
 * - Downward traffic (source routing): sink -> a chosen node
 * - Always-on powertrace (energy tracking)
 *
 * Assumptions:
 *   - Sink node has address 1.0 (Node ID = 1 trong COOJA).
 *   - SRDCP sources present in same folder: my_collect.c/.h, routing_table.c/.h, topology_report.c/.h
 */

#include "contiki.h"
#include "lib/random.h"
#include "net/rime/rime.h"
#include "net/netstack.h"
#include "core/net/linkaddr.h"
#include "powertrace.h"

#include <stdio.h>
#include <string.h>

#include "my_collect.h"
#include "wur_trace.h"   /* <<== THÊM: khung log WuR */

/*==================== App configuration ====================*/
#define APP_UPWARD_TRAFFIC    1   /* Nodes -> Sink */
#define APP_DOWNWARD_TRAFFIC  1   /* Sink -> Nodes (source routing) */

#define APP_NODES             5   /* Tổng số node dự kiến (để quay vòng địa chỉ đích) */

#define MSG_PERIOD     (30 * CLOCK_SECOND)   /* chu kỳ gửi uplink */
#define SR_MSG_PERIOD  (10 * CLOCK_SECOND)   /* chu kỳ gửi downlink từ sink */
#define COLLECT_CHANNEL 0xAA                 /* kênh SRDCP dùng C và C+1 */

/* Sink mặc định 1.0 (Node ID = 1) */
static const linkaddr_t SINK_ADDR = {{0x01, 0x00}};

/* Kiểu payload ứng dụng */
typedef struct {
  uint16_t seqn;
} __attribute__((packed)) test_msg_t;

/* Kết nối SRDCP */
static struct my_collect_conn my_collect;

/*==================== Helper log ====================*/
static void log_addr(const char *tag, const linkaddr_t *a) {
  if(a) {
    WUR_LOG("%s %02x:%02x\n", tag, a->u8[0], a->u8[1]);
  } else {
    WUR_LOG("%s (null)\n", tag);
  }
}

/*==================== Callbacks ====================*/
/* Khi sink nhận uplink từ node (many-to-one) */
static void recv_cb(const linkaddr_t *originator, uint8_t hops)
{
  test_msg_t msg;
  if(packetbuf_datalen() != sizeof(test_msg_t)) {
    printf("App: recv wrong length %d\n", packetbuf_datalen());
    WUR_WARN("[APP][UP][SINK] wrong payload len=%d -> drop\n", packetbuf_datalen());
    return;
  }
  memcpy(&msg, packetbuf_dataptr(), sizeof(msg));

  /* Log chi tiết khi SINK nhận uplink (sau chuỗi WUS->WAKE->DATA ở tầng dưới) */
  WUR_LOG("[APP][UP][SINK][RX] seq=%u from %02x:%02x hops=%u metric(root)=%u\n",
          msg.seqn, originator->u8[0], originator->u8[1], hops, my_collect.metric);

  printf("App: SINK received seq=%u from %02x:%02x, hops=%u, metric=%u\n",
         msg.seqn, originator->u8[0], originator->u8[1], hops, my_collect.metric);
}

/* Khi node nhận downlink từ sink (source routing) */
static void sr_recv_cb(struct my_collect_conn *ptr, uint8_t hops)
{
  test_msg_t sr_msg;
  if(packetbuf_datalen() != sizeof(test_msg_t)) {
    printf("App: sr_recv wrong length %d\n", packetbuf_datalen());
    WUR_WARN("[APP][SR][NODE] wrong payload len=%d -> drop\n", packetbuf_datalen());
    return;
  }
  memcpy(&sr_msg, packetbuf_dataptr(), sizeof(sr_msg));

  /* Log chi tiết khi NODE nhận downlink bằng SR (sau khi được WuR đánh thức) */
  WUR_LOG("[APP][SR][NODE][RX] seq=%u hops=%u my_metric=%u\n",
          sr_msg.seqn, hops, ptr->metric);

  printf("App: NODE %02x:%02x received SR seq=%u, hops=%u, my_metric=%u\n",
         linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
         sr_msg.seqn, hops, ptr->metric);
}

/* Bộ callback cho sink và node */
static const struct my_collect_callbacks sink_cb = {
  .recv   = recv_cb,      /* sink nhận uplink */
  .sr_recv= NULL          /* sink không nhận downlink */
};
static const struct my_collect_callbacks node_cb = {
  .recv   = NULL,         /* node không nhận uplink */
  .sr_recv= sr_recv_cb    /* node nhận downlink từ sink */
};

/*==================== PROCESS ====================*/
PROCESS(example_runicast_srdcp_process, "SRDCP-integrated runicast example");
AUTOSTART_PROCESSES(&example_runicast_srdcp_process);

PROCESS_THREAD(example_runicast_srdcp_process, ev, data)
{
  static struct etimer periodic, rnd;
  static test_msg_t msg = { .seqn = 0 };
  static linkaddr_t dest;
  static int ret;

  PROCESS_BEGIN();

  /* Bật đo năng lượng định kỳ (in log mỗi 10s) */
  powertrace_start(CLOCK_SECOND * 10);
  WUR_LOG("[APP] powertrace started interval=10s\n");

  /* Phân nhánh SINK vs NODE bằng địa chỉ Link-layer */
  if(linkaddr_cmp(&linkaddr_node_addr, &SINK_ADDR)) {
    /*==================== SINK ====================*/
    printf("App: I am SINK %02x:%02x\n",
           linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
    WUR_LOG("[APP] role=SINK\n");

    /* Khởi tạo SRDCP ở chế độ sink, dùng 2 kênh C và C+1 */
    my_collect_open(&my_collect, COLLECT_CHANNEL, true, &sink_cb);
    WUR_LOG("[APP][SRDCP] open as sink on channel=0x%02X (uses C and C+1)\n", COLLECT_CHANNEL);

#if APP_DOWNWARD_TRAFFIC
    /* Chờ lâu hơn ở đầu phiên để thu đủ topology (cây định tuyến) */
    etimer_set(&periodic, 75 * CLOCK_SECOND);
    WUR_LOG("[APP][SR] warmup wait=75s for topology reports\n");

    /* Khởi tạo đích đầu tiên là 2.0 (nếu tồn tại), sau đó quay vòng 2..APP_NODES */
    dest.u8[0] = 0x02;  /* low byte địa chỉ */
    dest.u8[1] = 0x00;  /* high byte */
    log_addr("[APP][SR] initial dest", &dest);

    while(1) {
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic));
      etimer_set(&periodic, SR_MSG_PERIOD);
      WUR_LOG("[APP][SR] periodic tick: next TX in %lus\n",
              (unsigned long)(SR_MSG_PERIOD / CLOCK_SECOND));

      /* Thêm jitter nhỏ để tránh xung đột */
      clock_time_t jitter = random_rand() % (SR_MSG_PERIOD/2);
      etimer_set(&rnd, jitter);
      WUR_LOG("[APP][SR] jitter=%lums\n",
              (unsigned long)(jitter * 1000UL / CLOCK_SECOND));
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&rnd));

      /* Tạo gói downlink (source routing) */
      packetbuf_clear();
      packetbuf_copyfrom(&msg, sizeof(msg));

      WUR_LOG("[APP][SR][TX_REQ] expect WuR wakeup then DATA -> dest=%02x:%02x seq=%u\n",
              dest.u8[0], dest.u8[1], msg.seqn);
      printf("App: SINK -> %02x:%02x send SR seq=%u\n",
             dest.u8[0], dest.u8[1], msg.seqn);

      ret = sr_send(&my_collect, &dest);
      if(ret == 0) {
        printf("App: ERROR sr_send seq=%u to %02x:%02x\n",
               msg.seqn, dest.u8[0], dest.u8[1]);
        WUR_WARN("[APP][SR][TX_REQ] sr_send FAILED seq=%u dest=%02x:%02x\n",
                 msg.seqn, dest.u8[0], dest.u8[1]);
      } else {
        WUR_LOG("[APP][SR][TX_REQ] sr_send OK seq=%u dest=%02x:%02x\n",
                msg.seqn, dest.u8[0], dest.u8[1]);
      }
      msg.seqn++;

      /* Quay vòng dest: 2.0 -> 3.0 -> ... -> APP_NODES.0 -> 2.0 ... */
      if(dest.u8[0] < APP_NODES) {
        dest.u8[0]++;
      } else {
        dest.u8[0] = 0x02;
      }
      log_addr("[APP][SR] next dest", &dest);
    }
#else
    /* Nếu tắt downward traffic, sink chỉ mở SRDCP để làm root cho cây */
    WUR_LOG("[APP][SRDCP] sink-only (no SR downlink)\n");
    while(1) {
      PROCESS_YIELD();
    }
#endif /* APP_DOWNWARD_TRAFFIC */

  } else {
    /*==================== NODE ====================*/
    printf("App: I am NODE %02x:%02x\n",
           linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
    WUR_LOG("[APP] role=NODE\n");

    /* Khởi tạo SRDCP ở chế độ node/router */
    my_collect_open(&my_collect, COLLECT_CHANNEL, false, &node_cb);
    WUR_LOG("[APP][SRDCP] open as node on channel=0x%02X\n", COLLECT_CHANNEL);
    WUR_LOG("[APP][SRDCP] initial metric=%u parent=%02x:%02x\n",
            my_collect.metric, my_collect.parent.u8[0], my_collect.parent.u8[1]);

#if APP_UPWARD_TRAFFIC
    etimer_set(&periodic, MSG_PERIOD);
    WUR_LOG("[APP][UP] first send in %lus\n",
            (unsigned long)(MSG_PERIOD / CLOCK_SECOND));
    while(1) {
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic));
      etimer_reset(&periodic);
      WUR_LOG("[APP][UP] periodic tick: next TX in %lus\n",
              (unsigned long)(MSG_PERIOD / CLOCK_SECOND));

      /* Jitter để tránh trùng nhau */
      clock_time_t jitter = random_rand() % (MSG_PERIOD/2);
      etimer_set(&rnd, jitter);
      WUR_LOG("[APP][UP] jitter=%lums\n",
              (unsigned long)(jitter * 1000UL / CLOCK_SECOND));
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&rnd));

      /* Uplink: node -> sink */
      packetbuf_clear();
      packetbuf_copyfrom(&msg, sizeof(msg));

      WUR_LOG("[APP][UP][TX_REQ] expect WuR wakeup then DATA -> parent=%02x:%02x seq=%u metric=%u\n",
              my_collect.parent.u8[0], my_collect.parent.u8[1],
              msg.seqn, my_collect.metric);

      printf("App: NODE %02x:%02x -> SINK send UP seq=%u (metric=%u parent=%02x:%02x)\n",
             linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
             msg.seqn, my_collect.metric,
             my_collect.parent.u8[0], my_collect.parent.u8[1]);

      ret = my_collect_send(&my_collect);
      if(ret == 0) {
        printf("App: ERROR my_collect_send seq=%u\n", msg.seqn);
        WUR_WARN("[APP][UP][TX_REQ] my_collect_send FAILED seq=%u\n", msg.seqn);
      } else {
        WUR_LOG("[APP][UP][TX_REQ] my_collect_send OK seq=%u\n", msg.seqn);
      }
      msg.seqn++;
    }
#else
    WUR_LOG("[APP][UP] disabled\n");
    while(1) {
      PROCESS_YIELD();
    }
#endif /* APP_UPWARD_TRAFFIC */
  }

  PROCESS_END();
}
