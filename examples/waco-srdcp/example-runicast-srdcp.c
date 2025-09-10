/*
 * SRDCP-integrated runicast-like example for WaCo + COOJA
 * - Upward: nodes -> sink
 * - Downward: source routing (sink -> node)
 * - Powertrace
 * - Neighbor table: Hop ↑ -> PRR_eff ↓ -> RSSI ↓ -> last_seen ↓
 *   (PRR_eff = PRR% * freshness% / 100, freshness penalizes stale entries)
 */

#include "contiki.h"
#include "lib/random.h"
#include "net/rime/rime.h"
#include "net/netstack.h"
#include "net/packetbuf.h"
#include "core/net/linkaddr.h"
#include "powertrace.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "my_collect.h"

/* ===== App logging toggle ===== */
#ifndef LOG_APP
#define LOG_APP 0
#endif
#if LOG_APP
#define APP_LOG(...) printf(__VA_ARGS__)
#else
#define APP_LOG(...)
#endif

/* ===== Collect-View (tùy chọn) ===== */
#ifndef ENABLE_COLLECT_VIEW
#define ENABLE_COLLECT_VIEW 0
#endif
#if ENABLE_COLLECT_VIEW
#include "shell.h"
#include "serial-shell.h"
#include "collect-view.h"
#endif

/*==================== App configuration ====================*/
#define APP_UPWARD_TRAFFIC 1
#define APP_DOWNWARD_TRAFFIC 1

#define APP_NODES 6 /* rotate SR dest 2..APP_NODES */

#define MSG_PERIOD (25 * CLOCK_SECOND)    /* uplink period */
#define SR_MSG_PERIOD (10 * CLOCK_SECOND) /* downlink period at sink */
#define COLLECT_CHANNEL 0xAA              /* SRDCP uses C and C+1 */

/* Neighbor print & aging */
#define NEI_MAX 32
#define NEI_TOPK 5
#define NEI_PRINT_PERIOD (60 * CLOCK_SECOND)

/* Freshness & aging thresholds (giây) */
#define NEI_SOFT_SEC 60  /* <= soft: freshness = 100% */
#define NEI_HARD_SEC 150 /* >= hard: evict */
#define PARENT_TO_SEC 45 /* riêng parent: timeout rơi cây */

/*==================== App payload ====================*/
typedef struct
{
  uint16_t seqn; /* UL per-source at node; DL per-destination at sink */
} __attribute__((packed)) test_msg_t;

/*==================== SRDCP connection ====================*/
static struct my_collect_conn my_collect;

/*==================== Neighbor table ====================*/
typedef struct
{
  linkaddr_t addr;
  int16_t rssi; /* dBm */
  uint8_t lqi;  /* LQI */
  clock_time_t last_seen;
  uint16_t last_seq; /* last app seq observed (if any) */
  uint16_t metric;   /* hops to sink reported by neighbor (0xFFFF unknown) */
  uint16_t prr;      /* % (0..100) đọc từ my_collect */
  uint16_t prr_eff;  /* prr * freshness / 100 (tính lúc in) */
  uint8_t used;
  uint8_t is_parent; /* flag để không evict parent */
} nei_entry_t;

static nei_entry_t nei_tab[NEI_MAX];

/* Sink address */
// static const linkaddr_t sink_addr = {{0x01, 0x00}};

/* Lookup or add neighbor (nếu đầy: thay entry cũ nhất) */
static nei_entry_t *nei_lookup_or_add(const linkaddr_t *addr)
{
  int i;
  nei_entry_t *free_e = NULL;
  for (i = 0; i < NEI_MAX; i++)
  {
    if (nei_tab[i].used && linkaddr_cmp(&nei_tab[i].addr, addr))
      return &nei_tab[i];
    if (!nei_tab[i].used && free_e == NULL)
      free_e = &nei_tab[i];
  }
  if (free_e)
  {
    memset(free_e, 0, sizeof(*free_e));
    linkaddr_copy(&free_e->addr, addr);
    free_e->metric = 0xFFFF;
    free_e->used = 1;
    return free_e;
  }
  /* full: evict oldest non-parent */
  clock_time_t oldest = (clock_time_t)-1;
  nei_entry_t *victim = NULL;
  for (i = 0; i < NEI_MAX; i++)
  {
    if (nei_tab[i].is_parent)
      continue; /* không thay parent */
    if (nei_tab[i].last_seen < oldest)
    {
      oldest = nei_tab[i].last_seen;
      victim = &nei_tab[i];
    }
  }
  if (!victim)
  {
    /* nếu mọi entry đều là parent (gần như không thể) -> thay idx 0 */
    victim = &nei_tab[0];
  }
  memset(victim, 0, sizeof(*victim));
  linkaddr_copy(&victim->addr, addr);
  victim->metric = 0xFFFF;
  victim->used = 1;
  return victim;
}

/* Update on RX (uses packetbuf attrs RSSI/LQI) */
static void nei_update_from_rx(const linkaddr_t *sender, uint16_t app_seq, int metric_hint)
{
  int16_t rssi = (int16_t)packetbuf_attr(PACKETBUF_ATTR_RSSI);
  uint8_t lqi = (uint8_t)packetbuf_attr(PACKETBUF_ATTR_LINK_QUALITY);
  nei_entry_t *e = nei_lookup_or_add(sender);
  e->rssi = rssi;
  e->lqi = lqi;
  e->last_seen = clock_time();
  e->last_seq = app_seq;
  if (metric_hint >= 0)
    e->metric = (uint16_t)metric_hint;
  e->prr = my_collect_get_prr_percent(sender);
}

/* Update from beacon hook */
static void nei_update_from_beacon(const linkaddr_t *sender, uint16_t metric, int16_t rssi, uint8_t lqi)
{
  nei_entry_t *e = nei_lookup_or_add(sender);
  e->metric = metric;
  e->rssi = rssi;
  e->lqi = lqi;
  e->last_seen = clock_time();
  e->prr = my_collect_get_prr_percent(sender);
}

/* Refresh PRR cho tất cả entry trước khi in/sort */
static void nei_refresh_prr_all(void)
{
  int i;
  for (i = 0; i < NEI_MAX; i++)
  {
    if (nei_tab[i].used)
    {
      nei_tab[i].prr = my_collect_get_prr_percent(&nei_tab[i].addr);
    }
  }
}

/* Aging sweeper: evict hard-stale (trừ parent) */
static void nei_sweep_aging(void)
{
  clock_time_t now = clock_time();
  int i;
  for (i = 0; i < NEI_MAX; i++)
  {
    if (!nei_tab[i].used)
      continue;
    clock_time_t age_ticks = now - nei_tab[i].last_seen;
    unsigned long age_sec = (unsigned long)(age_ticks / CLOCK_SECOND);

    /* cập nhật cờ parent theo trạng thái hiện tại */
    nei_tab[i].is_parent = linkaddr_cmp(&nei_tab[i].addr, &my_collect.parent) ? 1 : 0;

    if (age_sec >= NEI_HARD_SEC && !nei_tab[i].is_parent)
    {
      APP_LOG("CSV,NEI_EVICT,local=%02u:%02u,%lu,%02u:%02u,age=%lus,hop=%u,prr=%u,reason=HARD_STALE\n",
              linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
              (unsigned long)(now / CLOCK_SECOND),
              nei_tab[i].addr.u8[0], nei_tab[i].addr.u8[1],
              age_sec, nei_tab[i].metric, nei_tab[i].prr);
      nei_tab[i].used = 0;
    }
  }
}

/* Tính freshness% (0..100) từ tuổi (giây) */
static uint16_t freshness_percent(clock_time_t last_seen)
{
  clock_time_t now = clock_time();
  unsigned long age = (unsigned long)((now - last_seen) / CLOCK_SECOND);
  if (age <= NEI_SOFT_SEC)
    return 100;
  if (age >= NEI_HARD_SEC)
    return 0;
  /* tuyến tính không float: 100 - ((age-soft)*100)/(hard-soft) */
  unsigned long num = (age - NEI_SOFT_SEC) * 100UL;
  unsigned long den = (NEI_HARD_SEC - NEI_SOFT_SEC);
  unsigned long sub = num / den;
  if (sub > 100)
    sub = 100;
  return (uint16_t)(100UL - sub);
}

/* Partial selection sort pointers by Hop↑ -> PRR_eff↓ -> RSSI↓ -> last_seen↓ */
static void nei_sorted_ptrs(nei_entry_t *ptrs[], int *out_cnt)
{
  int i, j, cnt = 0;
  for (i = 0; i < NEI_MAX; i++)
    if (nei_tab[i].used)
      ptrs[cnt++] = &nei_tab[i];
  *out_cnt = cnt;

  /* tính prr_eff trước */
  for (i = 0; i < cnt; i++)
  {
    uint16_t fresh = freshness_percent(ptrs[i]->last_seen);
    ptrs[i]->prr_eff = (uint16_t)(((uint32_t)ptrs[i]->prr * (uint32_t)fresh) / 100U);
  }

  /* selection-like sort */
  for (i = 0; i < cnt; i++)
  {
    int best = i;
    for (j = i + 1; j < cnt; j++)
    {
      uint16_t hop_b = ptrs[best]->metric == 0xFFFF ? 0x7FFF : ptrs[best]->metric;
      uint16_t hop_j = ptrs[j]->metric == 0xFFFF ? 0x7FFF : ptrs[j]->metric;
      if (hop_j < hop_b)
        best = j;
      else if (hop_j == hop_b)
      {
        if (ptrs[j]->prr_eff > ptrs[best]->prr_eff)
          best = j;
        else if (ptrs[j]->prr_eff == ptrs[best]->prr_eff)
        {
          if (ptrs[j]->rssi > ptrs[best]->rssi)
            best = j;
          else if (ptrs[j]->rssi == ptrs[best]->rssi)
          {
            if (ptrs[j]->last_seen > ptrs[best]->last_seen)
              best = j;
          }
        }
      }
    }
    if (best != i)
    {
      nei_entry_t *tmp = ptrs[i];
      ptrs[i] = ptrs[best];
      ptrs[best] = tmp;
    }
  }
}

/*==================== Route change tracking ====================*/
static linkaddr_t last_parent = {{0, 0}};
static uint8_t have_last_parent = 0;

/*==================== PDR UL at SINK (per source) ====================*/
#define PDR_MAX_SRC 32
typedef struct
{
  uint8_t used;
  linkaddr_t id;
  uint16_t first_seq;
  uint16_t last_seq;
  uint32_t received;
  uint32_t gaps;
  uint32_t dups;
} pdr_ul_t;

static pdr_ul_t pdr_ul[PDR_MAX_SRC];
static clock_time_t pdr_ul_last_print = 0;
#define PDR_PRINT_PERIOD (60 * CLOCK_SECOND)
static uint8_t csv_ul_header_printed = 0;

static pdr_ul_t *pdr_ul_find_or_add(const linkaddr_t *id)
{
  int i, free_i = -1;
  for (i = 0; i < PDR_MAX_SRC; i++)
  {
    if (pdr_ul[i].used && linkaddr_cmp(&pdr_ul[i].id, id))
      return &pdr_ul[i];
    if (!pdr_ul[i].used && free_i < 0)
      free_i = i;
  }
  if (free_i >= 0)
  {
    memset(&pdr_ul[free_i], 0, sizeof(pdr_ul_t));
    pdr_ul[free_i].used = 1;
    linkaddr_copy(&pdr_ul[free_i].id, id);
    return &pdr_ul[free_i];
  }
  return NULL;
}

static void pdr_ul_maybe_reset(pdr_ul_t *st, uint16_t seq)
{
  if (st->received > 10 && seq < 3 && st->last_seq > 100)
  {
    st->first_seq = seq;
    st->last_seq = seq;
    st->received = st->gaps = st->dups = 0;
  }
}

static void pdr_ul_update(const linkaddr_t *src, uint16_t seq)
{
  pdr_ul_t *st = pdr_ul_find_or_add(src);
  if (!st)
    return;
  if (st->received == 0 && st->first_seq == 0 && st->last_seq == 0)
  {
    st->first_seq = seq;
    st->last_seq = seq;
    st->received = 1;
    return;
  }
  pdr_ul_maybe_reset(st, seq);
  if (seq == (uint16_t)(st->last_seq + 1))
  {
    st->received++;
    st->last_seq = seq;
  }
  else if (seq > (uint16_t)(st->last_seq + 1))
  {
    st->gaps += (uint16_t)(seq - st->last_seq - 1);
    st->received++;
    st->last_seq = seq;
  }
  else
  {
    st->dups++;
  }
}

static void pdr_ul_print_csv(void)
{
  int i;
  if (!csv_ul_header_printed)
  {
    APP_LOG("CSV,PDR_UL,local=%02u:%02u,time,peer,first,last,recv,gaps,dups,expected,PDR%%,parent,my_metric\n",
            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
    csv_ul_header_printed = 1;
  }
  for (i = 0; i < PDR_MAX_SRC; i++)
  {
    if (pdr_ul[i].used)
    {
      uint32_t expected = (uint32_t)(pdr_ul[i].last_seq - pdr_ul[i].first_seq + 1);
      if (expected == 0)
        expected = 1;
      uint32_t recv = pdr_ul[i].received;
      uint32_t pdrx = (recv * 10000UL) / expected;
      APP_LOG("CSV,PDR_UL,local=%02u:%02u,%lu,%02u:%02u,%u,%u,%lu,%lu,%lu,%lu,%lu.%02lu,%02u:%02u,%u\n",
              linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
              (unsigned long)(clock_time() / CLOCK_SECOND),
              pdr_ul[i].id.u8[0], pdr_ul[i].id.u8[1],
              pdr_ul[i].first_seq, pdr_ul[i].last_seq,
              (unsigned long)recv, (unsigned long)pdr_ul[i].gaps, (unsigned long)pdr_ul[i].dups,
              (unsigned long)expected,
              (unsigned long)(pdrx / 100), (unsigned long)(pdrx % 100),
              my_collect.parent.u8[0], my_collect.parent.u8[1],
              my_collect.metric);
    }
  }
}

/*==================== PDR DL at NODE (self) ====================*/
typedef struct
{
  uint8_t inited;
  uint16_t first_seq;
  uint16_t last_seq;
  uint32_t received;
  uint32_t gaps;
  uint32_t dups;
} pdr_dl_t;

static pdr_dl_t pdr_dl;
// static clock_time_t pdr_dl_last_print = 0;
static uint8_t csv_dl_header_printed = 0;

static void pdr_dl_maybe_reset(uint16_t seq)
{
  if (pdr_dl.received > 10 && seq < 3 && pdr_dl.last_seq > 100)
  {
    memset(&pdr_dl, 0, sizeof(pdr_dl));
  }
}
static void pdr_dl_update(uint16_t seq)
{
  if (!pdr_dl.inited)
  {
    pdr_dl.inited = 1;
    pdr_dl.first_seq = seq;
    pdr_dl.last_seq = seq;
    pdr_dl.received = 1;
    return;
  }
  pdr_dl_maybe_reset(seq);
  if (seq == (uint16_t)(pdr_dl.last_seq + 1))
  {
    pdr_dl.received++;
    pdr_dl.last_seq = seq;
  }
  else if (seq > (uint16_t)(pdr_dl.last_seq + 1))
  {
    pdr_dl.gaps += (uint16_t)(seq - pdr_dl.last_seq - 1);
    pdr_dl.received++;
    pdr_dl.last_seq = seq;
  }
  else
  {
    pdr_dl.dups++;
  }
}

static void pdr_dl_print_csv(void)
{
  if (!csv_dl_header_printed)
  {
    APP_LOG("CSV,PDR_DL,local=%02u:%02u,time,peer,first,last,recv,gaps,dups,expected,PDR%%,parent,my_metric\n",
            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
    csv_dl_header_printed = 1;
  }
  if (!pdr_dl.inited)
    return;
  uint32_t expected = (uint32_t)(pdr_dl.last_seq - pdr_dl.first_seq + 1);
  if (expected == 0)
    expected = 1;
  uint32_t pdrx = (pdr_dl.received * 10000UL) / expected;
  APP_LOG("CSV,PDR_DL,local=%02u:%02u,%lu,%02u:%02u,%u,%u,%lu,%lu,%lu,%lu,%lu.%02lu,%02u:%02u,%u\n",
          linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
          (unsigned long)(clock_time() / CLOCK_SECOND),
          sink_addr.u8[0], sink_addr.u8[1],
          pdr_dl.first_seq, pdr_dl.last_seq,
          (unsigned long)pdr_dl.received, (unsigned long)pdr_dl.gaps, (unsigned long)pdr_dl.dups,
          (unsigned long)expected,
          (unsigned long)(pdrx / 100), (unsigned long)(pdrx % 100),
          my_collect.parent.u8[0], my_collect.parent.u8[1],
          my_collect.metric);
}

/*==================== CSV Neighbor dump ====================*/
static uint8_t csv_nei_header_printed = 0;
static void nei_print_csv_all(const char *who)
{
  nei_entry_t *ptrs[NEI_MAX];
  int cnt, i;

  /* trước khi in: sweep + refresh PRR + sort */
  nei_sweep_aging();
  nei_refresh_prr_all();
  nei_sorted_ptrs(ptrs, &cnt);

  if (!csv_nei_header_printed)
  {
    APP_LOG("CSV,NEI,local=%02u:%02u,who,time,rank,neigh,hop,prr,prr_eff,rssi,lqi,last_seen,neigh_last_seq,parent,my_metric\n",
            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
    csv_nei_header_printed = 1;
  }

  for (i = 0; i < cnt; i++)
  {
    nei_entry_t *e = ptrs[i];

    /* ép kiểu đúng với format để hết cảnh báo */
    unsigned long now_s = (unsigned long)(clock_time() / CLOCK_SECOND);
    unsigned long last_s = (unsigned long)(e->last_seen / CLOCK_SECOND);
    unsigned hop = (unsigned)(e->metric == 0xFFFF ? 0xFFFF : e->metric);

    APP_LOG(
        "CSV,NEI,local=%02u:%02u,%s,%lu,%d,%02u:%02u,%u,%u,%u,%d,%u,%lu,%u,%02u:%02u,%u\n",
        (unsigned)linkaddr_node_addr.u8[0], /* %02u */
        (unsigned)linkaddr_node_addr.u8[1], /* %02u */
        who,                                /* %s   */
        now_s,                              /* %lu  */
        i + 1,                              /* %d   */
        (unsigned)e->addr.u8[0],            /* %02u */
        (unsigned)e->addr.u8[1],            /* %02u */
        hop,                                /* %u   */
        (unsigned)e->prr,                   /* %u   */
        (unsigned)e->prr_eff,               /* %u   <-- đã thêm prr_eff */
        (int)e->rssi,                       /* %d   */
        (unsigned)e->lqi,                   /* %u   */
        last_s,                             /* %lu  */
        (unsigned)e->last_seq,              /* %u   */
        (unsigned)my_collect.parent.u8[0],  /* %02u */
        (unsigned)my_collect.parent.u8[1],  /* %02u */
        (unsigned)my_collect.metric         /* %u   */
    );
  }

  /* bảng ASCII TOPK */
  if (cnt > 0)
  {
    int topn = (cnt < NEI_TOPK) ? cnt : NEI_TOPK;
    int k;
    APP_LOG("NEI[%s]-TOP%d: +------+------+-----+-----+-----+----------+------+------+\n", who, topn);
    APP_LOG("NEI[%s]-TOP%d: |  ID  | LQI | RSSI| PRR |Eff%%| last_seen| seq  | hop  |\n", who, topn);
    APP_LOG("NEI[%s]-TOP%d: +------+------+-----+-----+-----+----------+------+------+\n", who, topn);
    for (k = 0; k < topn; k++)
    {
      nei_entry_t *e = ptrs[k];
      unsigned long last_s = (unsigned long)(e->last_seen / CLOCK_SECOND);
      if (e->metric == 0xFFFF)
        APP_LOG("NEI[%s]-TOP%d: | %02u:%02u | %3u | %4d| %3u | %3u | %8lus | %4u |  --  |\n",
                who, topn, e->addr.u8[0], e->addr.u8[1],
                e->lqi, (int)e->rssi, e->prr, e->prr_eff, last_s, e->last_seq);
      else
        APP_LOG("NEI[%s]-TOP%d: | %02u:%02u | %3u | %4d| %3u | %3u | %8lus | %4u | %4u |\n",
                who, topn, e->addr.u8[0], e->addr.u8[1],
                e->lqi, (int)e->rssi, e->prr, e->prr_eff, last_s, e->last_seq, e->metric);
    }
    APP_LOG("NEI[%s]-TOP%d: +------+------+-----+-----+-----+----------+------+------+\n", who, topn);
  }
}

/*==================== App callbacks ====================*/
static void recv_cb(const linkaddr_t *originator, uint8_t hops)
{
  test_msg_t msg;
  if (packetbuf_datalen() != sizeof(test_msg_t))
  {
    APP_LOG("APP-UL[SINK]: wrong length %d B (expected %u B)\n",
            packetbuf_datalen(), (unsigned)sizeof(test_msg_t));
    return;
  }
  memcpy(&msg, packetbuf_dataptr(), sizeof(msg));

  /* 1) cập nhật láng giềng 1-hop thực (last hop) với RSSI/LQI */
  const linkaddr_t *last_hop = packetbuf_addr(PACKETBUF_ADDR_SENDER);
  if (last_hop)
  {
    nei_update_from_rx(last_hop, msg.seqn, -1);
  }

  /* 2) originator: chỉ cập nhật hop (hops) & last_seen (không đụng RSSI) */
  nei_entry_t *o = nei_lookup_or_add(originator);
  o->metric = hops;
  o->last_seen = clock_time();

  APP_LOG("APP-UL[SINK]: got seq=%u from %02u:%02u hops=%u my_metric=%u\n",
          msg.seqn, originator->u8[0], originator->u8[1], hops, my_collect.metric);

  /* PDR UL */
  pdr_ul_update(originator, msg.seqn);
  if (clock_time() - pdr_ul_last_print >= PDR_PRINT_PERIOD)
  {
    pdr_ul_print_csv();
    pdr_ul_last_print = clock_time();
  }
}

static void sr_recv_cb(struct my_collect_conn *ptr, uint8_t hops)
{
  test_msg_t sr_msg;
  const linkaddr_t *sender_ll = packetbuf_addr(PACKETBUF_ADDR_SENDER);
  if (packetbuf_datalen() != sizeof(test_msg_t))
  {
    APP_LOG("APP-DL[NODE %02u:%02u]: wrong length %d B (expected %u B)\n",
            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
            packetbuf_datalen(), (unsigned)sizeof(test_msg_t));
    return;
  }
  memcpy(&sr_msg, packetbuf_dataptr(), sizeof(sr_msg));

  if (sender_ll)
    nei_update_from_rx(sender_ll, sr_msg.seqn, -1);

  APP_LOG("APP-DL[NODE %02u:%02u]: got SR seq=%u hops=%u my_metric=%u parent=%02u:%02u\n",
          linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
          sr_msg.seqn, hops, ptr->metric, ptr->parent.u8[0], ptr->parent.u8[1]);

  /* PDR DL */
  pdr_dl_update(sr_msg.seqn);

  /* Bảng định kỳ do timer, ở đây có thể in TOPK nếu muốn */
  /* nei_print_csv_all("NODE"); */
}

/* Callback sets */
static const struct my_collect_callbacks sink_cb = {.recv = recv_cb, .sr_recv = NULL};
static const struct my_collect_callbacks node_cb = {.recv = NULL, .sr_recv = sr_recv_cb};

/*==================== CSV headers once at boot ====================*/
static void csv_print_headers_once(void)
{
  APP_LOG("CSV,INFO_HDR,fields=local,time,role,parent,my_metric\n");
}

/*==================== SR beacon hook (from my_collect.c) ====================*/
void srdcp_app_beacon_observed(const linkaddr_t *sender, uint16_t metric, int16_t rssi, uint8_t lqi)
{
  /* cập nhật neighbor khi overhear beacon */
  nei_update_from_beacon(sender, metric, rssi, lqi);
}

/*==================== PROCESS ====================*/
PROCESS(example_runicast_srdcp_process, "SRDCP-integrated runicast example");
AUTOSTART_PROCESSES(&example_runicast_srdcp_process);

PROCESS_THREAD(example_runicast_srdcp_process, ev, data)
{
  static struct etimer periodic, rnd, nei_tick;
  static test_msg_t msg;
  static linkaddr_t dest;
  static int ret;

  /* DL seq per-destination tại sink (chỉ dùng low byte) */
  static uint16_t dl_seq_per_dest[64] = {0};

  int i;

  PROCESS_BEGIN();

#if ENABLE_COLLECT_VIEW
  serial_shell_init();
  shell_blink_init();
#if WITH_COFFEE
  shell_file_init();
  shell_coffee_init();
#endif
  shell_rime_init();
  shell_rime_netcmd_init();
  shell_powertrace_init();
  shell_text_init();
  shell_time_init();
#if CONTIKI_TARGET_SKY
  shell_sky_init();
#endif
  shell_collect_view_init();
#endif

  for (i = 0; i < NEI_MAX; i++)
    nei_tab[i].used = 0;

  powertrace_start(CLOCK_SECOND * 10);
  csv_print_headers_once();

  if (linkaddr_cmp(&linkaddr_node_addr, &sink_addr))
  {
    /* ==================== SINK ==================== */
    APP_LOG("APP-ROLE[SINK]: started (local=%02u:%02u)\n",
            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);

    my_collect_open(&my_collect, COLLECT_CHANNEL, true, &sink_cb);
    APP_LOG("CSV,INFO,local=%02u:%02u,%lu,SINK,%02u:%02u,%u\n",
            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
            (unsigned long)(clock_time() / CLOCK_SECOND),
            my_collect.parent.u8[0], my_collect.parent.u8[1],
            my_collect.metric);

#if APP_DOWNWARD_TRAFFIC
    etimer_set(&periodic, 180 * CLOCK_SECOND); /* warm-up topology */
    etimer_set(&nei_tick, NEI_PRINT_PERIOD);

    dest.u8[0] = 0x02;
    dest.u8[1] = 0x00;

    while (1)
    {
      PROCESS_WAIT_EVENT();

      if (etimer_expired(&periodic))
      {
        etimer_set(&periodic, SR_MSG_PERIOD);

        etimer_set(&rnd, (uint16_t)(random_rand() % (SR_MSG_PERIOD / 2)));
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&rnd));

        packetbuf_clear();
        msg.seqn = ++dl_seq_per_dest[dest.u8[0]];
        packetbuf_copyfrom(&msg, sizeof(msg));

        APP_LOG("APP-DL[SINK]: send SR seq=%u -> %02u:%02u\n",
                msg.seqn, dest.u8[0], dest.u8[1]);

        ret = sr_send(&my_collect, &dest);
        if (ret == 0)
        {
          APP_LOG("ERR,SINK,sr_send,seq=%u,dst=%02u:%02u\n", msg.seqn, dest.u8[0], dest.u8[1]);
        }

        if (dest.u8[0] < APP_NODES)
          dest.u8[0]++;
        else
          dest.u8[0] = 0x02;
      }

      if (etimer_expired(&nei_tick))
      {
        nei_print_csv_all("SINK");
        etimer_reset(&nei_tick);
      }
    }
#else
    while (1)
    {
      PROCESS_YIELD();
    }
#endif
  }
  else
  {
    /* ==================== NODE ==================== */
    APP_LOG("APP-ROLE[NODE %02u:%02u]: started\n",
            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);

    my_collect_open(&my_collect, COLLECT_CHANNEL, false, &node_cb);
    APP_LOG("CSV,INFO,local=%02u:%02u,%lu,NODE,%02u:%02u,%u\n",
            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
            (unsigned long)(clock_time() / CLOCK_SECOND),
            my_collect.parent.u8[0], my_collect.parent.u8[1],
            my_collect.metric);

#if APP_UPWARD_TRAFFIC
    etimer_set(&periodic, MSG_PERIOD);
    etimer_set(&nei_tick, NEI_PRINT_PERIOD);

    msg.seqn = 0;

    while (1)
    {
      PROCESS_WAIT_EVENT();

      if (etimer_expired(&periodic))
      {
        etimer_reset(&periodic);

        /* watch parent change */
        if (!have_last_parent)
        {
          linkaddr_copy(&last_parent, &my_collect.parent);
          have_last_parent = 1;
        }
        else if (!linkaddr_cmp(&last_parent, &my_collect.parent))
        {
          APP_LOG("ROUTE[NODE %02u:%02u]: parent %02u:%02u -> %02u:%02u metric=%u\n",
                  linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
                  last_parent.u8[0], last_parent.u8[1],
                  my_collect.parent.u8[0], my_collect.parent.u8[1],
                  my_collect.metric);
          linkaddr_copy(&last_parent, &my_collect.parent);
        }

        /* jitter */
        etimer_set(&rnd, (uint16_t)(random_rand() % (MSG_PERIOD / 2)));
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&rnd));

        packetbuf_clear();
        packetbuf_copyfrom(&msg, sizeof(msg));

        APP_LOG("APP-UL[NODE %02u:%02u]: send seq=%u metric=%u parent=%02u:%02u\n",
                linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
                msg.seqn, my_collect.metric, my_collect.parent.u8[0], my_collect.parent.u8[1]);

        ret = my_collect_send(&my_collect);
        if (ret == 0)
        {
          APP_LOG("ERR,NODE,my_collect_send,seq=%u\n", msg.seqn);
        }
        msg.seqn++;
      }

      if (etimer_expired(&nei_tick))
      {
        nei_print_csv_all("NODE");
        pdr_dl_print_csv();
        etimer_reset(&nei_tick);
      }
    }
#else
    while (1)
    {
      PROCESS_YIELD();
    }
#endif
  }

  PROCESS_END();
}
