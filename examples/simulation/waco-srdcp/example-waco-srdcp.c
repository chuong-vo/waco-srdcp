/*
 * SRDCP-integrated runicast-like example for WaCo + COOJA
 * - Upward traffic (many-to-one): nodes -> sink
 * - Downward traffic (source routing): sink -> selected node
 * - Powertrace (energy accounting)
 *
 * Logging/Telemetry (CSV via printf -> Cooja Log Listener saves to file):
 * - PDR UL at SINK (per source)
 * - PDR DL at NODE (per-destination seq -> correct per-node PDR)
 * - Neighbor table sorted by hop metric (hops asc, RSSI desc, last_seen desc)
 * - Route changes, parent, metric, retries
 *
 * IMPORTANT:
 * - We keep SRDCP logic unchanged.
 * - To show hop(metric) on NODE-side neighbor table, we hook SRDCP beacon:
 *   srdcp_app_beacon_observed(sender, metric, rssi, lqi)
 *   -> add prototype in my_collect.h and call in my_collect.c (see section 2).
 */

#include "contiki.h"
#include "lib/random.h"
#include "net/rime/rime.h"
#include "net/netstack.h"
#include "core/net/linkaddr.h"
#include "powertrace.h"
#include "net/queuebuf.h"
#include "sensors.h"
#include "dev/battery-sensor.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "my_collect.h"
/* If Serial shell/Collect-View are unused, we keep stubs (no-op). */
#define serial_shell_init() ((void)0)
#define shell_blink_init() ((void)0)

/* ===== Application logging toggle (does not change behavior) ===== */
#ifndef LOG_APP
#define LOG_APP 1 /* 1: enable all app logs (including CSV); 0: silence */
#endif
#if LOG_APP
#define APP_LOG(...) printf(__VA_ARGS__)
#else
#define APP_LOG(...)
#endif
/*==================== App configuration ====================*/
#define APP_UPWARD_TRAFFIC 1   /* Nodes -> Sink */
#define APP_DOWNWARD_TRAFFIC 1 /* Sink -> Nodes (source routing) */

/* Tunables (can be overridden via CFLAGS) */
#ifndef APP_NODES
#define APP_NODES 5 /* rotate SR dest 2..APP_NODES */
#endif

#ifndef MSG_PERIOD
/* Fast-convergence app profile: a bit more frequent UL */
#define MSG_PERIOD (30 * CLOCK_SECOND) /* uplink period */
#endif
#ifndef SR_MSG_PERIOD
/* Fast-convergence app profile: slightly faster DL rotation */
#define SR_MSG_PERIOD (45 * CLOCK_SECOND) /* downlink period at sink */
#endif
#define COLLECT_CHANNEL 0xAA /* SRDCP uses C and C+1 */

#define NEI_MAX 32
#define NEI_TOPK 5
#define NEI_PRINT_PERIOD (30 * CLOCK_SECOND)
#define PDR_PRINT_PERIOD (30 * CLOCK_SECOND)
/* Neighbor credit aging: +1 on beacon seen (max 10),
 * -1 every BEACON_INTERVAL if not seen; entry is removed if credit reaches 0.
 */
#ifndef NEI_CREDIT_MAX
#define NEI_CREDIT_MAX 10
#endif
#ifndef NEI_CREDIT_INIT
#define NEI_CREDIT_INIT 3
#endif

// static const linkaddr_t sink_addr = {{0x01, 0x00}};

/*==================== App payload ====================*/
typedef struct
{
  uint16_t seqn;      /* UL: per-source at node; DL: per-destination at sink */
  uint32_t timestamp; /* clock_time() when message was queued */
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
  uint8_t credit;    /* aging credit: 0..NEI_CREDIT_MAX */
  uint8_t used;
} nei_entry_t;

static nei_entry_t nei_tab[NEI_MAX];

/* Lookup or add neighbor */
/**
 * @brief Find or add a neighbor by address.
 * @param addr Pointer to the neighbor's address to find or add.
 * @return Pointer to the entry in the neighbor table.
 * @details If the table is full, it replaces the entry with the oldest
 *          last_seen timestamp.
 */
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
    free_e->metric = 0xFFFF; /* unknown initially */
    free_e->credit = NEI_CREDIT_INIT;
    free_e->used = 1;
    return free_e;
  }
  /* replace oldest if full */
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
  victim->metric = 0xFFFF;
  victim->used = 1;
  return victim;
}

/* Update on RX (uses packetbuf attrs RSSI/LQI) */
/**
 * @brief Update neighbor table upon receiving a unicast data packet.
 * @param sender      Address of the source that just sent the data packet.
 * @param app_seq     The application sequence number observed in the packet.
 * @param metric_hint A hint for the hop count (or -1 to not update).
 */
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
}

/* Update from beacon hook (we receive metric and rssi/lqi explicitly) */
/**
 * @brief Update neighbor table upon overhearing an SRDCP beacon.
 * @param sender The neighbor's address.
 * @param metric The hop count to the sink as reported by the neighbor.
 * @param rssi   The RSSI estimated from the beacon.
 * @param lqi    The LQI estimated from the beacon.
 */
static void nei_update_from_beacon(const linkaddr_t *sender, uint16_t metric, int16_t rssi, uint8_t lqi)
{
  nei_entry_t *e = nei_lookup_or_add(sender);
  e->metric = metric;
  e->rssi = rssi;
  e->lqi = lqi;
  e->last_seen = clock_time();
  if (e->credit < NEI_CREDIT_MAX)
    e->credit++;
}

/* Partial selection sort on pointers by (metric asc, RSSI desc, last_seen desc) */
/**
 * @brief Sorts pointers to neighbor entries for printing/observation.
 * @param[out] ptrs   The resulting array of sorted pointers.
 * @param[out] out_cnt The number of valid elements filled into ptrs.
 * @details The sorting order is: hop (metric) asc -> PRR desc -> RSSI desc ->
 *          last_seen desc.
 */
static void nei_sorted_ptrs(nei_entry_t *ptrs[], int *out_cnt)
{
  int i, j, cnt = 0;
  for (i = 0; i < NEI_MAX; i++)
    if (nei_tab[i].used)
      ptrs[cnt++] = &nei_tab[i];
  *out_cnt = cnt;
  /* selection-like sort up to N (full since CSV wants all)
   * Order: hop(metric) asc, PRR desc, RSSI desc, last_seen desc
   */
  for (i = 0; i < cnt; i++)
  {
    int best = i;
    for (j = i + 1; j < cnt; j++)
    {
      uint16_t mi = ptrs[best]->metric;
      uint16_t mj = ptrs[j]->metric;
      if (mj < mi)
        best = j;
      else if (mj == mi)
      {
        uint8_t prr_i = my_collect_prr_percent(&ptrs[best]->addr);
        uint8_t prr_j = my_collect_prr_percent(&ptrs[j]->addr);
        if (prr_j > prr_i)
          best = j;
        else if (prr_j == prr_i)
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

/* Sink tracks last observed hops per originator (by low byte) */
static uint8_t last_hops_by_node[64];
static uint8_t last_hops_inited = 0;

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
static uint8_t csv_ul_header_printed = 0;

/**
 * @brief Find or add a PDR UL (uplink) entry for a source at the SINK.
 * @param id The address of the source node.
 * @return A pointer to the PDR UL entry (newly created if necessary),
 *         or NULL if the table is full.
 */
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

/**
 * @brief Reset UL statistics when a new sequence number cycle is detected.
 * @param st  The PDR UL entry.
 * @param seq The current sequence number.
 */
static void pdr_ul_maybe_reset(pdr_ul_t *st, uint16_t seq)
{
  if (st->received > 10 && seq < 3 && st->last_seq > 100)
  {
    st->first_seq = seq;
    st->last_seq = seq;
    st->received = st->gaps = st->dups = 0;
  }
}

/**
 * @brief Update per-source UL PDR at the SINK.
 * @param src The address of the source node.
 * @param seq The received sequence number.
 */
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

/**
 * @brief Print per-source UL PDR statistics in CSV format at the SINK.
 */
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
      /* Sanity filters: skip invalid peer or uninitialised stats */
      if ((pdr_ul[i].id.u8[0] == 0 && pdr_ul[i].id.u8[1] == 0) ||
          (pdr_ul[i].id.u8[0] == 0xFF && pdr_ul[i].id.u8[1] == 0xFF) ||
          (pdr_ul[i].first_seq == 0xFFFF) || (pdr_ul[i].last_seq == 0xFFFF))
      {
        continue;
      }
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
/* We use per-destination seq at SINK, so node sees contiguous seq for itself */
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
static clock_time_t pdr_dl_last_print = 0;
static uint8_t csv_dl_header_printed = 0;
static clock_time_t last_dl_delay_ticks_value = 0;

/**
 * @brief Reset DL PDR (self-assessed by node) on a new sequence number cycle.
 * @param seq The received sequence number.
 */
static void pdr_dl_maybe_reset(uint16_t seq)
{
  if (pdr_dl.received > 10 && seq < 3 && pdr_dl.last_seq > 100)
  {
    memset(&pdr_dl, 0, sizeof(pdr_dl));
  }
}
/**
 * @brief Update DL PDR at the NODE (receiving a source-routed packet from SINK).
 * @param seq The received sequence number.
 */
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

/**
 * @brief Print DL PDR statistics in CSV format at the NODE.
 */
static void pdr_dl_print_csv(void)
{
  if (!csv_dl_header_printed)
  {
    APP_LOG("CSV,PDR_DL,local=%02u:%02u,time,peer,first,last,recv,gaps,dups,expected,PDR%%,parent,my_metric\n",
            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
    csv_dl_header_printed = 1;
  }
  /* Sanity: skip if not inited or invalid seq window */
  if (!pdr_dl.inited || pdr_dl.first_seq == 0xFFFF || pdr_dl.last_seq == 0xFFFF || pdr_dl.last_seq < pdr_dl.first_seq)
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
/**
 * @brief Print the entire sorted neighbor table (CSV + TOP-K summary).
 * @param who A label to classify the log source ("SINK" or "NODE").
 */
static void nei_print_csv_all(const char *who)
{
  nei_entry_t *ptrs[NEI_MAX];
  int cnt, i;
  nei_sorted_ptrs(ptrs, &cnt);

  if (!csv_nei_header_printed)
  {
    APP_LOG("CSV,NEI,local=%02u:%02u,who,time,rank,neigh,hop,rssi,lqi,prr,last_seen,neigh_last_seq,parent,my_metric\n",
            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
    csv_nei_header_printed = 1;
  }

  for (i = 0; i < cnt; i++)
  {
    nei_entry_t *e = ptrs[i];
    unsigned long last_s = (unsigned long)(e->last_seen / CLOCK_SECOND);
    uint16_t hop = e->metric;
    uint8_t prr = my_collect_prr_percent(&e->addr);
    APP_LOG("CSV,NEI,local=%02u:%02u,%s,%lu,%d,%02u:%02u,%u,%d,%u,%u,%lu,%u,%02u:%02u,%u\n",
            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
            who,
            (unsigned long)(clock_time() / CLOCK_SECOND),
            i + 1,
            e->addr.u8[0], e->addr.u8[1],
            hop,
            (int)e->rssi,
            e->lqi,
            prr,
            last_s,
            e->last_seq,
            my_collect.parent.u8[0], my_collect.parent.u8[1],
            my_collect.metric);
  }

  /* Optional: also show a short human table TOPK for quick console view */
  if (cnt > 0)
  {
    int topn = (cnt < NEI_TOPK) ? cnt : NEI_TOPK;
    int k;
    APP_LOG("NEI[%s]-TOP%d: +------+------+-----+----------+------+------+-+--+\n", who, topn);
    APP_LOG("NEI[%s]-TOP%d: |  ID  | LQI | RSSI| last_seen| seq  | hop  |PRR|CR|\n", who, topn);
    APP_LOG("NEI[%s]-TOP%d: +------+------+-----+----------+------+------+-+--+\n", who, topn);
    for (k = 0; k < topn; k++)
    {
      nei_entry_t *e = ptrs[k];
      unsigned long last_s = (unsigned long)(e->last_seen / CLOCK_SECOND);
      uint8_t prr = my_collect_prr_percent(&e->addr);
      if (e->metric == 0xFFFF)
        APP_LOG("NEI[%s]-TOP%d: | %02u:%02u | %3u | %4d| %8lus | %4u |  --  | %2u|%2u|\n",
                who, topn, e->addr.u8[0], e->addr.u8[1], e->lqi, (int)e->rssi, last_s, e->last_seq, prr, e->credit);
      else
        APP_LOG("NEI[%s]-TOP%d: | %02u:%02u | %3u | %4d| %8lus | %4u | %4u | %2u|%2u|\n",
                who, topn, e->addr.u8[0], e->addr.u8[1], e->lqi, (int)e->rssi, last_s, e->last_seq, e->metric, prr, e->credit);
    }
    APP_LOG("NEI[%s]-TOP%d: +------+------+-----+----------+------+------+-+--+\n", who, topn);
  }
}

/**
 * @brief Age neighbor credit: decrement by 1 if no beacon is seen within
 *        BEACON_INTERVAL; remove if credit reaches 0.
 */
static void nei_credit_aging(void)
{
  int i;
  clock_time_t now = clock_time();
  for (i = 0; i < NEI_MAX; i++)
  {
    if (!nei_tab[i].used)
      continue;
    if ((now - nei_tab[i].last_seen) >= BEACON_INTERVAL)
    {
      if (nei_tab[i].credit > 0)
        nei_tab[i].credit--;
      if (nei_tab[i].credit == 0)
      {
        APP_LOG("NEI-AGING: drop %02u:%02u (stale)\n", nei_tab[i].addr.u8[0], nei_tab[i].addr.u8[1]);
        nei_tab[i].used = 0;
      }
    }
  }
}

/*==================== App callbacks ====================*/
/**
 * @brief Callback at the SINK upon receiving UL (uplink) data.
 * @param originator The address of the source node.
 * @param hops       The number of hops from the originator to the SINK.
 */
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

  if (msg.timestamp != 0)
  {
    clock_time_t now = clock_time();
    clock_time_t ts = (clock_time_t)msg.timestamp;
    clock_time_t ul_delay = (now >= ts) ? (now - ts) : 0;
    APP_LOG("STAT,UL_DELAY,local=%02u:%02u,time=%lu,src=%02u:%02u,hops=%u,delay_ticks=%lu\n",
            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
            (unsigned long)(now / CLOCK_SECOND),
            originator->u8[0], originator->u8[1],
            hops,
            (unsigned long)ul_delay);
  }

  /* update neighbor table (include metric=hops for originator as seen by sink) */
  nei_update_from_rx(originator, msg.seqn, (int)hops);

  APP_LOG("APP-UL[SINK]: got seq=%u from %02u:%02u hops=%u my_metric=%u\n",
          msg.seqn, originator->u8[0], originator->u8[1], hops, my_collect.metric);

  /* Track path length changes to trigger a quick NEI dump (optional) */
  if (!last_hops_inited)
  {
    int i;
    for (i = 0; i < 256; i++)
      last_hops_by_node[i] = 0xFF;
    last_hops_inited = 1;
  }
  if (last_hops_by_node[originator->u8[0]] != hops)
  {
    if (last_hops_by_node[originator->u8[0]] != 0xFF)
    {
      APP_LOG("TOPO[SINK]: %02u:%02u hops %u -> %u\n",
              originator->u8[0], originator->u8[1],
              last_hops_by_node[originator->u8[0]], hops);
    }
    else
    {
      APP_LOG("TOPO[SINK]: %02u:%02u initial hops -> %u\n",
              originator->u8[0], originator->u8[1], hops);
    }
    last_hops_by_node[originator->u8[0]] = hops;
    /* small immediate NEI snapshot for visibility */
    nei_print_csv_all("SINK");
  }

  /* PDR UL at sink */
  pdr_ul_update(originator, msg.seqn);
  if (clock_time() - pdr_ul_last_print >= PDR_PRINT_PERIOD)
  {
    pdr_ul_print_csv();
    pdr_ul_last_print = clock_time();
  }
}

/**
 * @brief Callback at a NODE upon receiving a DL (downlink) packet (via SRDCP).
 * @param ptr  A pointer to the my_collect connection structure.
 * @param hops The number of hops for the downlink path.
 */
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

  if (sr_msg.timestamp != 0)
  {
    clock_time_t now = clock_time();
    clock_time_t ts = (clock_time_t)sr_msg.timestamp;
    clock_time_t dl_delay = (now >= ts) ? (now - ts) : 0;
    last_dl_delay_ticks_value = dl_delay;
    APP_LOG("STAT,DL_DELAY,local=%02u:%02u,time=%lu,delay_ticks=%lu,parent=%02u:%02u\n",
            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
            (unsigned long)(now / CLOCK_SECOND),
            (unsigned long)dl_delay,
            ptr->parent.u8[0], ptr->parent.u8[1]);
  }

  APP_LOG("APP-DL[NODE %02u:%02u]: got SR seq=%u hops=%u my_metric=%u parent=%02u:%02u\n",
          linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
          sr_msg.seqn, hops, ptr->metric, ptr->parent.u8[0], ptr->parent.u8[1]);

  /* PDR DL at node */
  pdr_dl_update(sr_msg.seqn);
  if (clock_time() - pdr_dl_last_print >= PDR_PRINT_PERIOD)
  {
    pdr_dl_print_csv();
    pdr_dl_last_print = clock_time();
  }

  /* Periodic NEI CSV is handled in main loop; here we can print quick TOPK */
  nei_print_csv_all("NODE");
}

/* Callback sets */
static const struct my_collect_callbacks sink_cb = {.recv = recv_cb, .sr_recv = NULL};
static const struct my_collect_callbacks node_cb = {.recv = NULL, .sr_recv = sr_recv_cb};

/*==================== CSV headers once at boot ====================*/
/**
 * @brief Print a fixed header for CSV INFO lines at boot.
 */
static void csv_print_headers_once(void)
{
  /* Print a static header once for INFO rows (data rows printed after role known) */
  APP_LOG("CSV,INFO_HDR,fields=local,time,role,parent,my_metric\n");
}

/*==================== SR beacon hook (from my_collect.c) ====================*/
/**
 * @brief Application hook: called by SRDCP when a beacon is overheard (for telemetry).
 * @param sender The address of the neighbor.
 * @param metric The neighbor's hop count.
 * @param rssi   The RSSI observed from the beacon.
 * @param lqi    The LQI observed from the beacon.
 */
void srdcp_app_beacon_observed(const linkaddr_t *sender, uint16_t metric, int16_t rssi, uint8_t lqi)
{
  /* Update neighbor metric at NODE/SINK when we overhear a beacon */
  nei_update_from_beacon(sender, metric, rssi, lqi);
}

uint16_t srdcp_app_battery_mv(void)
{
#if CONTIKI_TARGET_SKY || CONTIKI_TARGET_Z1 || defined(BATTERY_SENSOR)
  uint16_t value;
  SENSORS_ACTIVATE(battery_sensor);
  value = (uint16_t)battery_sensor.value(0);
  SENSORS_DEACTIVATE(battery_sensor);
  return value;
#else
  return 0;
#endif
}

uint8_t srdcp_app_queue_load_percent(void)
{
#if defined(QUEUEBUF_CONF_NUM)
  const uint16_t total = QUEUEBUF_CONF_NUM;
#else
  const uint16_t total = 16; /* fallback */
#endif
  uint16_t free = (uint16_t)queuebuf_numfree();
  if (free > total)
    free = total;
  uint16_t used = total - free;
  uint8_t percent = (uint8_t)((used * 100U) / (total ? total : 1));
  return percent;
}

uint16_t srdcp_app_last_ul_delay_ticks(void)
{
  return 0;
}

uint16_t srdcp_app_last_dl_delay_ticks(void)
{
  if (last_dl_delay_ticks_value > 0xFFFF)
    return 0xFFFF;
  return (uint16_t)last_dl_delay_ticks_value;
}

/*==================== PROCESS ====================*/
PROCESS(example_runicast_srdcp_process, "SRDCP-integrated runicast example");
AUTOSTART_PROCESSES(&example_runicast_srdcp_process);

PROCESS_THREAD(example_runicast_srdcp_process, ev, data)
{
  static struct etimer periodic, rnd, nei_tick;
  static struct etimer nei_aging;
  static test_msg_t msg;
  static linkaddr_t dest;
  static int ret;

  /* DL seq per-destination at sink (index by low byte) */
  static uint16_t dl_seq_per_dest[64] = {0};

  int i;

  PROCESS_BEGIN();

  serial_shell_init();
  shell_blink_init();
#if ENABLE_COLLECT_VIEW
#if WITH_COFFEE
  shell_file_init();
  shell_coffee_init();
#endif /* WITH_COFFEE */

  /* shell_download_init(); */
  /* shell_rime_sendcmd_init(); */
  /* shell_ps_init(); */
  // shell_reboot_init();
  shell_rime_init();
  shell_rime_netcmd_init();
  /* shell_rime_ping_init(); */
  /* shell_rime_debug_init(); */
  /* shell_rime_debug_runicast_init(); */
  shell_powertrace_init();
  /* shell_base64_init(); */
  shell_text_init();
  shell_time_init();
  /* shell_sendtest_init(); */

#if CONTIKI_TARGET_SKY
  shell_sky_init();
#endif /* CONTIKI_TARGET_SKY */

  shell_collect_view_init();
#endif
  /* init neighbor table */
  for (i = 0; i < NEI_MAX; i++)
    nei_tab[i].used = 0;

  powertrace_start(CLOCK_SECOND * 10);
  csv_print_headers_once();

  if (linkaddr_cmp(&linkaddr_node_addr, &sink_addr))
  {
    /*==================== SINK ====================*/
    APP_LOG("APP-ROLE[SINK]: started (local=%02u:%02u)\n",
            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);

    my_collect_open(&my_collect, COLLECT_CHANNEL, true, &sink_cb);
    APP_LOG("CSV,INFO,local=%02u:%02u,%lu,SINK,%02u:%02u,%u\n",
            linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
            (unsigned long)(clock_time() / CLOCK_SECOND),
            my_collect.parent.u8[0], my_collect.parent.u8[1],
            my_collect.metric);

#if APP_DOWNWARD_TRAFFIC
    /* Warm-up before downlink; can override via -DWARMUP_S=NN */
#ifndef WARMUP_S
/* Fast-convergence: shorter warm-up before DL */
#define WARMUP_S 240
#endif
    etimer_set(&periodic, WARMUP_S * CLOCK_SECOND); /* warm-up topology */
    etimer_set(&nei_tick, NEI_PRINT_PERIOD);
    etimer_set(&nei_aging, BEACON_INTERVAL);

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
        /* per-destination seq so each target sees contiguous seq */
        msg.seqn = ++dl_seq_per_dest[dest.u8[0]];
        msg.timestamp = clock_time();
        packetbuf_copyfrom(&msg, sizeof(msg));

        APP_LOG("APP-DL[SINK]: send SR seq=%u -> %02u:%02u\n",
                msg.seqn, dest.u8[0], dest.u8[1]);

        ret = sr_send(&my_collect, &dest);
        if (ret == 0)
        {
          APP_LOG("ERR,SINK,sr_send,seq=%u,dst=%02u:%02u\n", msg.seqn, dest.u8[0], dest.u8[1]);
        }

        /* rotate 2..APP_NODES */
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

      if (etimer_expired(&nei_aging))
      {
        nei_credit_aging();
        etimer_reset(&nei_aging);
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
    /*==================== NODE ====================*/
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
    etimer_set(&nei_aging, BEACON_INTERVAL);

    msg.seqn = 0;

    while (1)
    {
      PROCESS_WAIT_EVENT();

      if (etimer_expired(&periodic))
      {
        etimer_reset(&periodic);

        /* route change watch before send */
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

        /* uplink send */
        packetbuf_clear();
        msg.timestamp = clock_time();
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
        pdr_dl_print_csv(); /* periodically dump DL PDR as well */
        etimer_reset(&nei_tick);
      }

      if (etimer_expired(&nei_aging))
      {
        nei_credit_aging();
        etimer_reset(&nei_aging);
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
