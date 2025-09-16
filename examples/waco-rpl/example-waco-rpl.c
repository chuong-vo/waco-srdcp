/*
 * WaCo + RPL UDP example (fixed)
 * - Consistent SRDCP-style ID:00 printing (use link-layer bytes [3],[4])
 * - CSV PDR always printed: boot header + periodic snapshots
 * - Avoid wrong sink detection via linkaddr bytes; use node_id==1
 */

#include "contiki.h"
#include "net/ip/simple-udp.h"
#include "net/ipv6/uip-ds6.h"
#include "net/rpl/rpl.h"
#include "net/netstack.h"
#include "core/net/linkaddr.h"
#include "node-id.h"
#include <stdio.h>
#include <string.h>

#ifndef LOG_APP
#define LOG_APP 1
#endif
#if LOG_APP
#define APP_LOG(...) printf(__VA_ARGS__)
#else
#define APP_LOG(...)
#endif

#ifndef APP_NODES
#define APP_NODES 5
#endif

#ifndef MSG_PERIOD
#define MSG_PERIOD (15 * CLOCK_SECOND)
#endif

#ifndef SR_MSG_PERIOD
#define SR_MSG_PERIOD (12 * CLOCK_SECOND)
#endif

#ifndef PDR_PRINT_PERIOD
#define PDR_PRINT_PERIOD (30 * CLOCK_SECOND) /* shorter to guarantee CSV during short sims */
#endif

#define UL_PORT 8765
#define DL_PORT 8766

/* =================== Helpers for SRDCP-style ID:00 =================== */
static inline void addr_to_id00(const linkaddr_t *a, uint8_t *id0, uint8_t *id1)
{
  if (!a)
  {
    *id0 = 0;
    *id1 = 0;
    return;
  }
#if LINKADDR_SIZE >= 5
  *id0 = a->u8[3];
  *id1 = a->u8[4];
#else
  /* Fallback: last 2 bytes if platform uses shorter link-layer addr */
  *id0 = a->u8[LINKADDR_SIZE - 2];
  *id1 = a->u8[LINKADDR_SIZE - 1];
#endif
}

static inline void print_addr_id(const linkaddr_t *a, char *buf, size_t n)
{
  uint8_t id0, id1;
  addr_to_id00(a, &id0, &id1);
  snprintf(buf, n, "%02u:%02u", (unsigned)id0, (unsigned)id1);
}

static inline void print_parent_id(const linkaddr_t *p, char *buf, size_t n)
{
  print_addr_id(p, buf, n);
}

/* =================== UL/DL message formats =================== */

typedef struct
{
  uint16_t seqn;
  uint16_t metric;    /* hops approx */
  uint8_t src0, src1; /* SRDCP-style id:00 of source */
} __attribute__((packed)) ul_msg_t;

static struct simple_udp_connection ul_conn; /* sink server / node client */
static struct simple_udp_connection dl_conn; /* node server / sink client */

static uint16_t ul_seq = 0;
static uint16_t dl_seq = 0;
static uint8_t next_dl = 2; /* rotate 2..APP_NODES */

/* Map Rime ID (low byte) -> last seen IPv6 of that node (learned from UL) */
typedef struct
{
  uint8_t known;
  uip_ipaddr_t ip6;
} id_ip_t;
#ifndef MAP_MAX_NODES
#define MAP_MAX_NODES 64
#endif
static id_ip_t id_ip_map[MAP_MAX_NODES];

/* =================== CSV PDR stats (similar to SRDCP) =================== */

typedef struct
{
  uint8_t used;
  uint8_t id0, id1; /* linkaddr ID:00 */
  uint16_t first_seq, last_seq;
  uint32_t received, gaps, dups;
} pdr_ul_t;

#define PDR_MAX_SRC 32
static pdr_ul_t pdr_ul[PDR_MAX_SRC];
static uint8_t csv_ul_header_printed = 0;
static uint8_t csv_dl_header_printed = 0;
/* PRR headers (alias to PDR so parsers that expect PRR can read directly) */
// static uint8_t csv_prr_ul_header_printed = 0;
// static uint8_t csv_prr_dl_header_printed = 0;

static pdr_ul_t *pdr_ul_find_or_add(uint8_t id0, uint8_t id1)
{
  int i, free_i = -1;
  for (i = 0; i < PDR_MAX_SRC; i++)
  {
    if (pdr_ul[i].used && pdr_ul[i].id0 == id0 && pdr_ul[i].id1 == id1)
      return &pdr_ul[i];
    if (!pdr_ul[i].used && free_i < 0)
      free_i = i;
  }
  if (free_i >= 0)
  {
    pdr_ul[free_i].used = 1;
    pdr_ul[free_i].id0 = id0;
    pdr_ul[free_i].id1 = id1;
    pdr_ul[free_i].first_seq = pdr_ul[free_i].last_seq = 0;
    pdr_ul[free_i].received = pdr_ul[free_i].gaps = pdr_ul[free_i].dups = 0;
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

static void pdr_ul_update(uint8_t id0, uint8_t id1, uint16_t seq)
{
  pdr_ul_t *st = pdr_ul_find_or_add(id0, id1);
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

static void csv_print_info_headers_once(void)
{
  static uint8_t printed = 0;
  if (printed)
    return;
  printed = 1;
  printf("CSV,INFO_HDR,fields=local,time,role,parent,my_metric\n");
}

static void csv_print_info_role(const char *role, uint16_t metric, const linkaddr_t *parent)
{
  uint8_t me0, me1, p0, p1;
  addr_to_id00(&linkaddr_node_addr, &me0, &me1);
  addr_to_id00(parent, &p0, &p1);
  printf("CSV,INFO,local=%02u:%02u,%lu,%s,%02u:%02u,%u\n",
         me0, me1, (unsigned long)(clock_time() / CLOCK_SECOND), role, p0, p1, metric);
}

static void pdr_ul_print_csv(uint16_t my_metric, const linkaddr_t *parent)
{
  uint8_t me0, me1, p0, p1;
  int i; /* C89: khai báo ở đầu block */
  addr_to_id00(&linkaddr_node_addr, &me0, &me1);
  addr_to_id00(parent, &p0, &p1);

  if (!csv_ul_header_printed)
  {
    printf("CSV,PDR_UL,local=%02u:%02u,time,peer,first,last,recv,gaps,dups,expected,PDR%%,parent,my_metric\n",
           me0, me1);
    csv_ul_header_printed = 1;
  }

  for (i = 0; i < PDR_MAX_SRC; i++)
  {
    if (pdr_ul[i].used)
    {
      uint32_t expected = (uint32_t)(pdr_ul[i].last_seq - pdr_ul[i].first_seq + 1);
      uint32_t recv;
      uint32_t pdrx;
      if (expected == 0)
        expected = 1;
      recv = pdr_ul[i].received;
      pdrx = (recv * 10000UL) / expected;

      /* DÒNG PDR_UL (có newline) */
      printf("CSV,PDR_UL,local=%02u:%02u,%lu,%02u:%02u,%u,%u,%lu,%lu,%lu,%lu,%lu.%02lu,%02u:%02u,%u\n",
             me0, me1, (unsigned long)(clock_time() / CLOCK_SECOND),
             pdr_ul[i].id0, pdr_ul[i].id1,
             pdr_ul[i].first_seq, pdr_ul[i].last_seq,
             (unsigned long)recv, (unsigned long)pdr_ul[i].gaps, (unsigned long)pdr_ul[i].dups,
             (unsigned long)expected,
             (unsigned long)(pdrx / 100), (unsigned long)(pdrx % 100),
             p0, p1, my_metric);

      /* Alias PRR_UL = PDR_UL (có newline) để parser có số */
      printf("CSV,PRR_UL,local=%02u:%02u,%lu,%02u:%02u,%lu.%02lu\n",
             me0, me1, (unsigned long)(clock_time() / CLOCK_SECOND),
             pdr_ul[i].id0, pdr_ul[i].id1,
             (unsigned long)(pdrx / 100), (unsigned long)(pdrx % 100));
    }
  }
}

typedef struct
{
  uint8_t inited;
  uint16_t first_seq, last_seq;
  uint32_t received, gaps, dups;
} pdr_dl_t;
static pdr_dl_t pdr_dl;

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
    pdr_dl.first_seq = pdr_dl.last_seq = seq;
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

static void pdr_dl_print_csv(uint16_t my_metric, const linkaddr_t *parent, const linkaddr_t *sink)
{
  uint8_t me0, me1, p0, p1, s0, s1;
  uint32_t expected;
  uint32_t pdrx;
  addr_to_id00(&linkaddr_node_addr, &me0, &me1);
  addr_to_id00(parent, &p0, &p1);
  addr_to_id00(sink, &s0, &s1);

  if (!csv_dl_header_printed)
  {
    printf("CSV,PDR_DL,local=%02u:%02u,time,peer,first,last,recv,gaps,dups,expected,PDR%%,parent,my_metric\n",
           me0, me1);
    csv_dl_header_printed = 1;
  }
  if (!pdr_dl.inited)
    return;

  expected = (uint32_t)(pdr_dl.last_seq - pdr_dl.first_seq + 1);
  if (expected == 0)
    expected = 1;
  pdrx = (pdr_dl.received * 10000UL) / expected;

  /* DÒNG PDR_DL (có newline) */
  printf("CSV,PDR_DL,local=%02u:%02u,%lu,%02u:%02u,%u,%u,%lu,%lu,%lu,%lu,%lu.%02lu,%02u:%02u,%u\n",
         me0, me1, (unsigned long)(clock_time() / CLOCK_SECOND),
         s0, s1,
         pdr_dl.first_seq, pdr_dl.last_seq,
         (unsigned long)pdr_dl.received, (unsigned long)pdr_dl.gaps, (unsigned long)pdr_dl.dups,
         (unsigned long)expected,
         (unsigned long)(pdrx / 100), (unsigned long)(pdrx % 100),
         p0, p1, my_metric);

  /* Alias PRR_DL = PDR_DL (có newline) */
  printf("CSV,PRR_DL,local=%02u:%02u,%lu,%02u:%02u,%lu.%02lu\n",
         me0, me1, (unsigned long)(clock_time() / CLOCK_SECOND),
         s0, s1,
         (unsigned long)(pdrx / 100), (unsigned long)(pdrx % 100));
}

static uint16_t rpl_hops_approx(void)
{
  rpl_dag_t *dag = rpl_get_any_dag();
  if (dag)
  {
    rpl_rank_t r = dag->rank;
#ifdef RPL_MIN_HOPRANKINC
    return r / RPL_MIN_HOPRANKINC;
#else
    return r / 256;
#endif
  }
  return 0xFFFF;
}

/* =================== UDP callbacks =================== */

static void ul_rx_cb(struct simple_udp_connection *c, const uip_ipaddr_t *sender_addr,
                     uint16_t sender_port, const uip_ipaddr_t *receiver_addr,
                     uint16_t receiver_port, const uint8_t *data, uint16_t datalen)
{
  if (datalen >= sizeof(ul_msg_t))
  {
    const ul_msg_t *m = (const ul_msg_t *)data;
    /* Log like SRDCP */
    APP_LOG("APP-UL[SINK]: got seq=%u from %02u:%02u hops=%u my_metric=%u\n",
            (unsigned)m->seqn, (unsigned)m->src0, (unsigned)m->src1,
            (unsigned)m->metric, 0);
    /* Learn mapping from src ID -> IPv6 for DL later (at sink) */
    if (node_id == 1)
    {
      if (m->src0 < MAP_MAX_NODES)
      {
        id_ip_map[m->src0].known = 1;
        uip_ipaddr_copy(&id_ip_map[m->src0].ip6, sender_addr);
      }
      /* Update PDR-UL stats */
      pdr_ul_update(m->src0, m->src1, m->seqn);
    }
  }
}

static void dl_rx_cb(struct simple_udp_connection *c, const uip_ipaddr_t *sender_addr,
                     uint16_t sender_port, const uip_ipaddr_t *receiver_addr,
                     uint16_t receiver_port, const uint8_t *data, uint16_t datalen)
{
  /* Downlink receive at node; payload is seq only */
  if (datalen >= 2)
  {
    uint16_t seq = ((uint16_t)data[0] << 8) | data[1];
    linkaddr_t me = linkaddr_node_addr;
    char mebuf[6] = {0};
    print_addr_id(&me, mebuf, sizeof(mebuf));
    rpl_dag_t *dag = rpl_get_any_dag();
    const linkaddr_t *pl = (dag && dag->preferred_parent) ? rpl_get_parent_lladdr(dag->preferred_parent) : NULL;
    char pbuf[6] = {0};
    print_parent_id(pl, pbuf, sizeof(pbuf));
    APP_LOG("APP-DL[NODE %s]: got SR seq=%u hops=%u my_metric=%u parent=%s\n",
            mebuf, (unsigned)seq, (unsigned)rpl_hops_approx(), (unsigned)rpl_hops_approx(), pbuf);
    /* DL PDR update */
    pdr_dl_update(seq);
  }
}

/* =================== Main process =================== */

PROCESS(waco_rpl_process, "WaCo + RPL UDP example (fixed)");
AUTOSTART_PROCESSES(&waco_rpl_process);

PROCESS_THREAD(waco_rpl_process, ev, data)
{
  static struct etimer ul_timer, dl_timer, stats_timer;
  linkaddr_t me = linkaddr_node_addr;
  char mebuf[6] = {0};
  print_addr_id(&me, mebuf, sizeof(mebuf));
  static linkaddr_t last_parent;
  static uint8_t have_last_parent = 0;

  PROCESS_BEGIN();

  /* IPv6 addressing: add global addr based on link-layer */
  uip_ipaddr_t ipaddr;
  uip_ip6addr(&ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 0);
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);

  /* If sink (ID 1), start RPL root */
  if (node_id == 1)
  {
    rpl_dag_t *dag = rpl_set_root(RPL_DEFAULT_INSTANCE, &ipaddr);
    if (dag)
    {
      rpl_set_prefix(dag, &ipaddr, 64);
    }
  }

  /* Connections */
  simple_udp_register(&ul_conn, UL_PORT, NULL, UL_PORT, ul_rx_cb);
  simple_udp_register(&dl_conn, DL_PORT, NULL, DL_PORT, dl_rx_cb);

  /* Role + initial CSV headers */
  csv_print_info_headers_once();
  if (node_id == 1)
  {
    APP_LOG("APP-ROLE[SINK]: started (local=%s)\n", mebuf);
    csv_print_info_role("SINK", 0, NULL);
    /* Ensure CSV headers appear even before first packets */
    pdr_ul_print_csv(rpl_hops_approx(), NULL);
  }
  else
  {
    APP_LOG("APP-ROLE[NODE %s]: started\n", mebuf);
    rpl_dag_t *dag = rpl_get_any_dag();
    const linkaddr_t *pl = (dag && dag->preferred_parent) ? rpl_get_parent_lladdr(dag->preferred_parent) : NULL;
    csv_print_info_role("NODE", rpl_hops_approx(), pl);
    /* Ensure CSV headers appear even before first packets */
    linkaddr_t sink_ll;
    memset(&sink_ll, 0, sizeof(sink_ll));
    sink_ll.u8[3] = 1;
    sink_ll.u8[4] = 0; /* 01:00 */
    pdr_dl_print_csv(rpl_hops_approx(), pl, &sink_ll);
  }

  etimer_set(&ul_timer, CLOCK_SECOND * 5);
  if (node_id == 1)
  {
    etimer_set(&dl_timer, CLOCK_SECOND * 10);
  }
  etimer_set(&stats_timer, PDR_PRINT_PERIOD);

  while (1)
  {
    PROCESS_YIELD();

    if (etimer_expired(&ul_timer))
    {
      etimer_reset(&ul_timer);
      if (node_id != 1)
      {
        /* Node sends UL to DAG root */
        rpl_dag_t *dag = rpl_get_any_dag();
        if (dag)
        {
          ul_msg_t m;
          m.seqn = ul_seq++;
          m.metric = rpl_hops_approx();
          m.src0 = (uint8_t)node_id;
          m.src1 = 0;
          /* Detect parent change */
          const linkaddr_t *pl = (dag->preferred_parent) ? rpl_get_parent_lladdr(dag->preferred_parent) : NULL;
          if (!have_last_parent)
          {
            if (pl)
            {
              last_parent = *pl;
              have_last_parent = 1;
            }
          }
          else if (pl && (memcmp(last_parent.u8, pl->u8, LINKADDR_SIZE) != 0))
          {
            char lpbuf[6] = {0}, pbuf[6] = {0}, nbuf[6] = {0};
            print_addr_id(&me, nbuf, sizeof(nbuf));
            print_parent_id(&last_parent, lpbuf, sizeof(lpbuf));
            print_parent_id(pl, pbuf, sizeof(pbuf));
            printf("ROUTE[NODE %s]: parent %s -> %s metric=%u\n", nbuf, lpbuf, pbuf, m.metric);
            last_parent = *pl;
          }
          /* Log send with current parent */
          char pbuf[6] = {0}, nbuf[6] = {0};
          print_parent_id(pl, pbuf, sizeof(pbuf));
          print_addr_id(&me, nbuf, sizeof(nbuf));
          APP_LOG("APP-UL[NODE %s]: send seq=%u metric=%u parent=%s\n", nbuf, (unsigned)m.seqn, (unsigned)m.metric, pbuf);
          simple_udp_sendto(&ul_conn, &m, sizeof(m), &dag->dag_id);
        }
      }
    }

    if (node_id == 1 && etimer_expired(&dl_timer))
    {
      etimer_reset(&dl_timer);
      /* Sink sends DL to next node: use IP learned from UL mapping */
      if (next_dl <= APP_NODES)
      {
        if (next_dl < MAP_MAX_NODES && id_ip_map[next_dl].known)
        {
          uip_ipaddr_t dst;
          uip_ipaddr_copy(&dst, &id_ip_map[next_dl].ip6);
          uint8_t payload[2] = {(uint8_t)(dl_seq >> 8), (uint8_t)(dl_seq & 0xFF)};
          APP_LOG("APP-DL[SINK]: send SR seq=%u -> %02u:%02u\n", (unsigned)dl_seq, (unsigned)next_dl, 0);
          simple_udp_sendto(&dl_conn, payload, sizeof(payload), &dst);
          dl_seq++;
        }
        next_dl++;
        if (next_dl > APP_NODES)
          next_dl = 2;
      }
    }

    if (etimer_expired(&stats_timer))
    {
      etimer_reset(&stats_timer);
      if (node_id == 1)
      {
        /* SINK: print UL CSV stats periodically */
        pdr_ul_print_csv(rpl_hops_approx(), NULL);
      }
      else
      {
        /* NODE: print DL CSV */
        rpl_dag_t *dag = rpl_get_any_dag();
        const linkaddr_t *pl = (dag && dag->preferred_parent) ? rpl_get_parent_lladdr(dag->preferred_parent) : NULL;
        linkaddr_t sink_ll;
        memset(&sink_ll, 0, sizeof(sink_ll));
        sink_ll.u8[3] = 1;
        sink_ll.u8[4] = 0; /* 01:00 */
        pdr_dl_print_csv(rpl_hops_approx(), pl, &sink_ll);
      }
    }
  }

  PROCESS_END();
}
