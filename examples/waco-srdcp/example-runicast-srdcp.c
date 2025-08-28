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

/*==================== App configuration ====================*/
#define APP_UPWARD_TRAFFIC 1   /* Nodes -> Sink */
#define APP_DOWNWARD_TRAFFIC 1 /* Sink -> Nodes (source routing) */

#define APP_NODES 5 /* Tổng số node dự kiến (để quay vòng địa chỉ đích) */

#define MSG_PERIOD (30 * CLOCK_SECOND)    /* chu kỳ gửi uplink */
#define SR_MSG_PERIOD (10 * CLOCK_SECOND) /* chu kỳ gửi downlink từ sink */
#define COLLECT_CHANNEL 0xAA              /* kênh SRDCP dùng C và C+1 */

/* ---- Cấu hình bảng lân cận ---- */
#define NEI_MAX 16 /* số mục tối đa lưu trong bảng lân cận cục bộ */
#define NEI_TOPK 5 /* in top-K theo RSSI */

/* Sink mặc định 1.0 (Node ID = 1) */
static const linkaddr_t SINK_ADDR = {{0x01, 0x00}};

/* Kiểu payload ứng dụng */
typedef struct
{
  uint16_t seqn;
} __attribute__((packed)) test_msg_t;

/* Kết nối SRDCP */
static struct my_collect_conn my_collect;

/*==================== Neighbor table (cục bộ từng node) ====================*/
typedef struct
{
  linkaddr_t addr;
  int16_t rssi; /* dBm */
  uint8_t lqi;  /* Link Quality Index */
  clock_time_t last_seen;
  uint16_t last_seq; /* seq ứng dụng gần nhất (nếu có) */
  uint8_t used;
} nei_entry_t;

static nei_entry_t nei_tab[NEI_MAX];

/* Tìm hoặc tạo mục cho địa chỉ addr */
static nei_entry_t *nei_lookup_or_add(const linkaddr_t *addr)
{
  int i;
  nei_entry_t *free_e = NULL;
  for (i = 0; i < NEI_MAX; i++)
  {
    if (nei_tab[i].used &&
        linkaddr_cmp(&nei_tab[i].addr, addr))
    {
      return &nei_tab[i];
    }
    if (!nei_tab[i].used && free_e == NULL)
    {
      free_e = &nei_tab[i];
    }
  }
  if (free_e != NULL)
  {
    memset(free_e, 0, sizeof(*free_e));
    linkaddr_copy(&free_e->addr, addr);
    free_e->used = 1;
    return free_e;
  }
  /* Nếu đầy: ghi đè mục cũ nhất (last_seen nhỏ nhất) */
  {
    clock_time_t oldest = (clock_time_t)-1;
    nei_entry_t *victim = &nei_tab[0];
    for (i = 0; i < NEI_MAX; i++)
    {
      if (nei_tab[i].last_seen < oldest)
      {
        oldest = nei_tab[i].last_seen;
        victim = &nei_tab[i];
      }
    }
    memset(victim, 0, sizeof(*victim));
    linkaddr_copy(&victim->addr, addr);
    victim->used = 1;
    return victim;
  }
}

/* Cập nhật từ một gói nhận (đọc attr RSSI/LQI từ packetbuf) */
static void nei_update_from_rx(const linkaddr_t *sender, uint16_t app_seq)
{
  int16_t rssi;
  uint8_t lqi;
  nei_entry_t *e;

  rssi = (int16_t)packetbuf_attr(PACKETBUF_ATTR_RSSI);
  lqi = (uint8_t)packetbuf_attr(PACKETBUF_ATTR_LINK_QUALITY);

  e = nei_lookup_or_add(sender);
  e->rssi = rssi;
  e->lqi = lqi;
  e->last_seen = clock_time();
  e->last_seq = app_seq;
}

/* In top-K lân cận theo RSSI giảm dần (không dùng qsort) */
static void nei_print_topk(const char *who /* "SINK" hoặc "NODE" */)
{
  int i, j, cnt = 0, topn, max_idx;
  nei_entry_t *ptrs[NEI_MAX], *tmp;

  for (i = 0; i < NEI_MAX; i++)
  {
    if (nei_tab[i].used)
    {
      ptrs[cnt++] = &nei_tab[i];
    }
  }
  if (cnt == 0)
  {
    printf("NEI[%s]: (empty)\n", who);
    return;
  }

  /* Lấy topn = min(cnt, NEI_TOPK) */
  topn = (cnt < NEI_TOPK) ? cnt : NEI_TOPK;

  /* Partial selection sort: đưa phần tử RSSI lớn nhất lên vị trí 0..topn-1 */
  for (i = 0; i < topn; i++)
  {
    max_idx = i;
    for (j = i + 1; j < cnt; j++)
    {
      if (ptrs[j]->rssi > ptrs[max_idx]->rssi ||
          (ptrs[j]->rssi == ptrs[max_idx]->rssi &&
           ptrs[j]->last_seen > ptrs[max_idx]->last_seen))
      {
        max_idx = j;
      }
    }
    if (max_idx != i)
    {
      tmp = ptrs[i];
      ptrs[i] = ptrs[max_idx];
      ptrs[max_idx] = tmp;
    }
  }

  printf("NEI[%s]-TOP%d: +------+----+-----+----------+------+\n", who, topn);
  printf("NEI[%s]-TOP%d: |  ID  |LQI |RSSI | last_seen| seq  |\n", who, topn);
  printf("NEI[%s]-TOP%d: +------+----+-----+----------+------+\n", who, topn);
  for (i = 0; i < topn; i++)
  {
    nei_entry_t *e = ptrs[i];
    unsigned long last_s = (unsigned long)(e->last_seen / CLOCK_SECOND);
    printf("NEI[%s]-TOP%d: | %02x:%02x |%3u |%4d | %8lus | %4u |\n",
           who, topn,
           e->addr.u8[0], e->addr.u8[1],
           e->lqi, (int)e->rssi, last_s, e->last_seq);
  }
  printf("NEI[%s]-TOP%d: +------+----+-----+----------+------+\n", who, topn);
}

/*==================== Theo dõi ROUTING thay đổi ====================*/
/* Node: theo dõi parent thay đổi để in */
static linkaddr_t last_parent = {{0, 0}};
static uint8_t have_last_parent = 0;

/* Sink: theo dõi thay đổi "hops" (độ sâu) theo từng node originator */
static uint8_t last_hops_by_node[256]; /* index theo byte thấp của địa chỉ (00..FF) */
static uint8_t last_hops_inited = 0;

/*==================== Callbacks ====================*/
/* Khi sink nhận uplink từ node (many-to-one) */
static void recv_cb(const linkaddr_t *originator, uint8_t hops)
{
  test_msg_t msg;
  uint16_t len;
  int changed;

  len = (uint16_t)packetbuf_datalen();
  if (len != sizeof(test_msg_t))
  {
    printf("App: recv wrong length %d\n", packetbuf_datalen());
    return;
  }
  memcpy(&msg, packetbuf_dataptr(), sizeof(msg));

  /* Cập nhật bảng lân cận bằng RSSI/LQI của sender */
  nei_update_from_rx(originator, msg.seqn);

  /* In thông tin app */
  printf("App: SINK received seq=%u from %02x:%02x, hops=%u, metric=%u\n",
         msg.seqn, originator->u8[0], originator->u8[1], hops, my_collect.metric);

  /* Sink: theo dõi thay đổi độ sâu/hops của node originator */
  if (!last_hops_inited)
  {
    int i_init;
    for (i_init = 0; i_init < 256; i_init++)
      last_hops_by_node[i_init] = 0xFF;
    last_hops_inited = 1;
  }

  changed = 0;
  if (last_hops_by_node[originator->u8[0]] != hops)
  {
    if (last_hops_by_node[originator->u8[0]] == 0xFF)
    {
      printf("TOPO[SINK]: path_len(node %02x:%02x -> sink) initial -> %u\n",
             originator->u8[0], originator->u8[1], hops);
    }
    else
    {
      printf("TOPO[SINK]: path_len(node %02x:%02x -> sink) changed: %u -> %u\n",
             originator->u8[0], originator->u8[1],
             last_hops_by_node[originator->u8[0]], hops);
    }
    last_hops_by_node[originator->u8[0]] = hops;
    changed = 1;
  }

  /* Chỉ in TOP-5 lân cận ở SINK khi topo thay đổi để bớt log và tránh warning */
  if (changed)
  {
    nei_print_topk("SINK");
  }
}

/* Khi node nhận downlink từ sink (source routing) */
static void sr_recv_cb(struct my_collect_conn *ptr, uint8_t hops)
{
  test_msg_t sr_msg;
  const linkaddr_t *sender_ll;
  uint16_t len;

  len = (uint16_t)packetbuf_datalen();
  if (len != sizeof(test_msg_t))
  {
    printf("App: sr_recv wrong length %d\n", packetbuf_datalen());
    return;
  }
  memcpy(&sr_msg, packetbuf_dataptr(), sizeof(sr_msg));

  /* Lấy địa chỉ LL của bên gửi (thường là sink hoặc next-hop) */
  sender_ll = packetbuf_addr(PACKETBUF_ADDR_SENDER);
  if (sender_ll != NULL)
  {
    nei_update_from_rx(sender_ll, sr_msg.seqn);
  }

  printf("App: NODE %02x:%02x received SR seq=%u, hops=%u, my_metric=%u\n",
         linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
         sr_msg.seqn, hops, ptr->metric);

  /* In TOP-5 lân cận ở NODE mỗi lần nhận SR */
  nei_print_topk("NODE");
}

/* Bộ callback cho sink và node */
static const struct my_collect_callbacks sink_cb = {
    .recv = recv_cb, /* sink nhận uplink */
    .sr_recv = NULL  /* sink không nhận downlink */
};
static const struct my_collect_callbacks node_cb = {
    .recv = NULL,         /* node không nhận uplink */
    .sr_recv = sr_recv_cb /* node nhận downlink từ sink */
};

/*==================== PROCESS ====================*/
PROCESS(example_runicast_srdcp_process, "SRDCP-integrated runicast example");
AUTOSTART_PROCESSES(&example_runicast_srdcp_process);

PROCESS_THREAD(example_runicast_srdcp_process, ev, data)
{
  static struct etimer periodic, rnd;
  static test_msg_t msg = {.seqn = 0};
  static linkaddr_t dest;
  static int ret;
  int i; /* C90: declare at block start */

  PROCESS_BEGIN();

  /* Init neighbor table */
  for (i = 0; i < NEI_MAX; i++)
    nei_tab[i].used = 0;

  /* Bật đo năng lượng định kỳ (in log mỗi 10s) */
  powertrace_start(CLOCK_SECOND * 10);

  /* Phân nhánh SINK vs NODE bằng địa chỉ Link-layer */
  if (linkaddr_cmp(&linkaddr_node_addr, &SINK_ADDR))
  {
    /*==================== SINK ====================*/
    printf("App: I am SINK %02x:%02x\n",
           linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);

    /* Khởi tạo SRDCP ở chế độ sink, dùng 2 kênh C và C+1 */
    my_collect_open(&my_collect, COLLECT_CHANNEL, true, &sink_cb);

#if APP_DOWNWARD_TRAFFIC
    /* Chờ lâu hơn ở đầu phiên để thu đủ topology (cây định tuyến) */
    etimer_set(&periodic, 75 * CLOCK_SECOND);

    /* Khởi tạo đích đầu tiên là 2.0 (nếu tồn tại), sau đó quay vòng 2..APP_NODES */
    dest.u8[0] = 0x02; /* low byte địa chỉ */
    dest.u8[1] = 0x00; /* high byte */

    while (1)
    {
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic));
      etimer_set(&periodic, SR_MSG_PERIOD);

      /* Thêm jitter nhỏ để tránh xung đột */
      etimer_set(&rnd, (uint16_t)(random_rand() % (SR_MSG_PERIOD / 2)));
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&rnd));

      /* Tạo gói downlink (source routing) */
      packetbuf_clear();
      packetbuf_copyfrom(&msg, sizeof(msg));
      printf("App: SINK -> %02x:%02x send SR seq=%u\n",
             dest.u8[0], dest.u8[1], msg.seqn);

      ret = sr_send(&my_collect, &dest);
      if (ret == 0)
      {
        printf("App: ERROR sr_send seq=%u to %02x:%02x\n",
               msg.seqn, dest.u8[0], dest.u8[1]);
      }
      msg.seqn++;

      /* Quay vòng dest: 2.0 -> 3.0 -> ... -> APP_NODES.0 -> 2.0 ... */
      if (dest.u8[0] < APP_NODES)
      {
        dest.u8[0]++;
      }
      else
      {
        dest.u8[0] = 0x02;
      }
    }
#else
    /* Nếu tắt downward traffic, sink chỉ mở SRDCP để làm root cho cây */
    while (1)
    {
      PROCESS_YIELD();
    }
#endif /* APP_DOWNWARD_TRAFFIC */
  }
  else
  {
    /*==================== NODE ====================*/
    printf("App: I am NODE %02x:%02x\n",
           linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);

    /* Khởi tạo SRDCP ở chế độ node/router */
    my_collect_open(&my_collect, COLLECT_CHANNEL, false, &node_cb);

#if APP_UPWARD_TRAFFIC
    etimer_set(&periodic, MSG_PERIOD);
    while (1)
    {
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic));
      etimer_reset(&periodic);

      /* Jitter để tránh trùng nhau */
      etimer_set(&rnd, (uint16_t)(random_rand() % (MSG_PERIOD / 2)));
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&rnd));

      /* Theo dõi ROUTE CHANGE (parent thay đổi) trước khi gửi */
      if (!have_last_parent)
      {
        linkaddr_copy(&last_parent, &my_collect.parent);
        have_last_parent = 1;
      }
      else
      {
        if (!linkaddr_cmp(&last_parent, &my_collect.parent))
        {
          printf("ROUTE[NODE %02x:%02x]: parent changed %02x:%02x -> %02x:%02x (metric=%u)\n",
                 linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
                 last_parent.u8[0], last_parent.u8[1],
                 my_collect.parent.u8[0], my_collect.parent.u8[1],
                 my_collect.metric);
          linkaddr_copy(&last_parent, &my_collect.parent);
        }
      }

      /* Uplink: node -> sink */
      packetbuf_clear();
      packetbuf_copyfrom(&msg, sizeof(msg));

      printf("App: NODE %02x:%02x -> SINK send UP seq=%u (metric=%u parent=%02x:%02x)\n",
             linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
             msg.seqn, my_collect.metric,
             my_collect.parent.u8[0], my_collect.parent.u8[1]);

      ret = my_collect_send(&my_collect);
      if (ret == 0)
      {
        printf("App: ERROR my_collect_send seq=%u\n", msg.seqn);
      }
      msg.seqn++;
    }
#else
    while (1)
    {
      PROCESS_YIELD();
    }
#endif /* APP_UPWARD_TRAFFIC */
  }

  PROCESS_END();
}
