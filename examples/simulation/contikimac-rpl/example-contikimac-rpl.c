/*
 * WaCo + RPL UDP example (fixed)
 * - Consistent SRDCP-style ID:00 printing (use link-layer bytes [3],[4])
 * - CSV PDR always printed: boot header + periodic snapshots
 * - Avoid wrong sink detection via linkaddr bytes; use node_id==1
 */

#include "contiki.h"
#include "lib/random.h"
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
#define MSG_PERIOD (30 * CLOCK_SECOND)
#endif

#ifndef SR_MSG_PERIOD
#define SR_MSG_PERIOD (45 * CLOCK_SECOND)
#endif

#ifndef PDR_PRINT_PERIOD
#define PDR_PRINT_PERIOD (30 * CLOCK_SECOND) /* shorter to guarantee CSV during short sims */
#endif

#define UL_PORT 8765
#define DL_PORT 8766

#define RPL_READY_TIMEOUT (120UL * CLOCK_SECOND)
#define RPL_READY_POLL_INTERVAL (CLOCK_SECOND)

#define DL_ROTATION_START 2

/* =================== Forward declarations =================== */
static uint16_t dag_rank_to_hops(const rpl_dag_t *dag);
static uint8_t dag_has_parent(const rpl_dag_t *dag);
static clock_time_t compute_ul_jitter(void);
static void parent_tracker_update(const linkaddr_t *new_parent, uint16_t metric);
static void send_ul_message(void);
static uint8_t pick_next_dl_target(void);

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
  uint32_t timestamp; /* clock_time() when queued */
} __attribute__((packed)) ul_msg_t;

typedef struct
{
  uint16_t seqn;
  uint32_t timestamp;
} __attribute__((packed)) dl_msg_t;

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
#define MAP_MAX_NODES 32
#endif
static id_ip_t id_ip_map[MAP_MAX_NODES];

static linkaddr_t tracked_parent;
static uint8_t parent_is_known = 0;
static struct etimer ul_jitter_timer;

/* =================== CSV PDR stats (similar to SRDCP) =================== */

typedef struct
{
  uint8_t used;
  uint8_t id0, id1; /* linkaddr ID:00 */
  uint16_t first_seq, last_seq;
  uint16_t received, gaps, dups;
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
  uint16_t received, gaps, dups;
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

/* =================== Utility helpers =================== */

static uint16_t dag_rank_to_hops(const rpl_dag_t *dag)
{
  if (dag == NULL)
  {
    return 0xFFFF;
  }
#ifdef RPL_MIN_HOPRANKINC
  return dag->rank / RPL_MIN_HOPRANKINC;
#else
  return dag->rank / 256;
#endif
}

static uint8_t dag_has_parent(const rpl_dag_t *dag)
{
  return (dag != NULL && dag->preferred_parent != NULL);
}

static clock_time_t compute_ul_jitter(void)
{
  clock_time_t window = MSG_PERIOD / 2;
  if (window == 0)
  {
    return 0;
  }
  return (clock_time_t)(random_rand() % window);
}

static void parent_tracker_update(const linkaddr_t *new_parent, uint16_t metric)
{
  uint8_t me0, me1, old0 = 0, old1 = 0, new0 = 0, new1 = 0;

  addr_to_id00(&linkaddr_node_addr, &me0, &me1);

  if (parent_is_known)
  {
    addr_to_id00(&tracked_parent, &old0, &old1);
  }

  if (!new_parent)
  {
    if (parent_is_known)
    {
      printf("ROUTE[NODE %02u:%02u]: parent %02u:%02u -> --:-- metric=%u\n",
             (unsigned)me0, (unsigned)me1,
             (unsigned)old0, (unsigned)old1,
             (unsigned)metric);
      parent_is_known = 0;
      memset(&tracked_parent, 0, sizeof(tracked_parent));
    }
    return;
  }

  addr_to_id00(new_parent, &new0, &new1);
  if (!parent_is_known || memcmp(tracked_parent.u8, new_parent->u8, LINKADDR_SIZE) != 0)
  {
    printf("ROUTE[NODE %02u:%02u]: parent %02u:%02u -> %02u:%02u metric=%u\n",
           (unsigned)me0, (unsigned)me1,
           (unsigned)old0, (unsigned)old1,
           (unsigned)new0, (unsigned)new1,
           (unsigned)metric);
    linkaddr_copy(&tracked_parent, new_parent);
    parent_is_known = 1;
  }
}

static void send_ul_message(void)
{
  rpl_dag_t *curr_dag;
  const linkaddr_t *parent;
  ul_msg_t m;
  uint16_t hops;
  uint8_t me0, me1, p0, p1;

  curr_dag = rpl_get_any_dag();
  if (!dag_has_parent(curr_dag))
  {
    parent_tracker_update(NULL, 0xFFFF);
    return;
  }

  parent = rpl_get_parent_lladdr(curr_dag->preferred_parent);
  hops = dag_rank_to_hops(curr_dag);
  parent_tracker_update(parent, hops);

  m.seqn = ul_seq++;
  m.metric = hops;
  m.src0 = (uint8_t)node_id;
  m.src1 = 0;
  m.timestamp = (uint32_t)clock_time();

  addr_to_id00(&linkaddr_node_addr, &me0, &me1);
  addr_to_id00(parent, &p0, &p1);

  APP_LOG("APP-UL[NODE %02u:%02u]: send seq=%u metric=%u parent=%02u:%02u\n",
          (unsigned)me0, (unsigned)me1,
          (unsigned)m.seqn,
          (unsigned)m.metric,
          (unsigned)p0, (unsigned)p1);

  simple_udp_sendto(&ul_conn, &m, sizeof(m), &curr_dag->dag_id);
}

static uint8_t pick_next_dl_target(void)
{
  uint8_t attempts;
  uint8_t candidate;

  if (APP_NODES <= 1)
  {
    return 0;
  }

  candidate = next_dl;
  attempts = (uint8_t)(APP_NODES - 1);

  while (attempts > 0)
  {
    if (candidate < MAP_MAX_NODES && id_ip_map[candidate].known)
    {
      next_dl = (uint8_t)(candidate + 1);
      if (next_dl > APP_NODES)
      {
        next_dl = DL_ROTATION_START;
      }
      return candidate;
    }

    candidate++;
    if (candidate > APP_NODES)
    {
      candidate = DL_ROTATION_START;
    }
    attempts--;
  }

  next_dl = DL_ROTATION_START;
  return 0;
}

static uint16_t rpl_hops_approx(void)
{
  return dag_rank_to_hops(rpl_get_any_dag());
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
    clock_time_t now = clock_time();
    clock_time_t ts = (clock_time_t)m->timestamp;
    uint32_t delay_ticks = (now >= ts) ? (uint32_t)(now - ts) : 0;
    uint8_t sink0, sink1;
    addr_to_id00(&linkaddr_node_addr, &sink0, &sink1);
    printf("STAT,UL_DELAY,local=%02u:%02u,time=%lu,src=%02u:%02u,hops=%u,delay_ticks=%lu\n",
           (unsigned)sink0, (unsigned)sink1,
           (unsigned long)(now / CLOCK_SECOND),
           (unsigned)m->src0, (unsigned)m->src1,
           (unsigned)m->metric,
           (unsigned long)delay_ticks);
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
  /* Downlink receive at node; payload carries seq and timestamp */
  if (datalen >= sizeof(dl_msg_t))
  {
    const dl_msg_t *msg = (const dl_msg_t *)data;
    uint16_t seq = msg->seqn;
    rpl_dag_t *dag = rpl_get_any_dag();
    const linkaddr_t *pl = (dag && dag->preferred_parent) ? rpl_get_parent_lladdr(dag->preferred_parent) : NULL;
    uint8_t me0, me1;
    addr_to_id00(&linkaddr_node_addr, &me0, &me1);
    linkaddr_t parent_tmp = linkaddr_null;
    if (pl)
    {
      linkaddr_copy(&parent_tmp, pl);
    }
    uint8_t p0, p1;
    addr_to_id00(&parent_tmp, &p0, &p1);
    APP_LOG("APP-DL[NODE %02u:%02u]: got SR seq=%u hops=%u my_metric=%u parent=%02u:%02u\n",
            (unsigned)me0, (unsigned)me1,
            (unsigned)seq,
            (unsigned)rpl_hops_approx(),
            (unsigned)rpl_hops_approx(),
            (unsigned)p0, (unsigned)p1);
    clock_time_t now = clock_time();
    clock_time_t ts = (clock_time_t)msg->timestamp;
    uint32_t delay_ticks = (now >= ts) ? (uint32_t)(now - ts) : 0;
    printf("STAT,DL_DELAY,local=%02u:%02u,time=%lu,delay_ticks=%lu,parent=%02u:%02u\n",
           (unsigned)me0, (unsigned)me1,
           (unsigned long)(now / CLOCK_SECOND),
           (unsigned long)delay_ticks,
           (unsigned)p0, (unsigned)p1);
    /* DL PDR update */
    pdr_dl_update(seq);
  }
}

/* =================== Main process =================== */

PROCESS(waco_rpl_process, "WaCo + RPL UDP example");
AUTOSTART_PROCESSES(&waco_rpl_process);

PROCESS_THREAD(waco_rpl_process, ev, data)
{
  static struct etimer ul_timer, dl_timer, stats_timer;
  static struct etimer rpl_wait_timer;
  linkaddr_t me = linkaddr_node_addr;
  char mebuf[6] = {0};
  static clock_time_t rpl_wait_deadline;
  static uint8_t rpl_ready_flag;
  static uint8_t rpl_timeout_flag;

  PROCESS_BEGIN();

  memset(&tracked_parent, 0, sizeof(tracked_parent));
  parent_is_known = 0;
  memset(id_ip_map, 0, sizeof(id_ip_map));
  next_dl = DL_ROTATION_START;

  print_addr_id(&me, mebuf, sizeof(mebuf));

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
    pdr_ul_print_csv(rpl_hops_approx(), NULL);
  }
  else
  {
    rpl_dag_t *init_dag = rpl_get_any_dag();
    const linkaddr_t *init_parent = (init_dag && init_dag->preferred_parent) ? rpl_get_parent_lladdr(init_dag->preferred_parent) : NULL;
    APP_LOG("APP-ROLE[NODE %s]: started\n", mebuf);
    csv_print_info_role("NODE", dag_rank_to_hops(init_dag), init_parent);
    {
      linkaddr_t sink_ll;
      memset(&sink_ll, 0, sizeof(sink_ll));
      sink_ll.u8[3] = 1;
      sink_ll.u8[4] = 0; /* 01:00 */
      pdr_dl_print_csv(dag_rank_to_hops(init_dag), init_parent, &sink_ll);
    }
  }

  /* Wait for RPL readiness before starting traffic (reduce early drops) */
  rpl_wait_deadline = clock_time() + RPL_READY_TIMEOUT;
  rpl_ready_flag = 0;
  rpl_timeout_flag = 0;

  while (!rpl_ready_flag)
  {
    rpl_dag_t *wait_dag = rpl_get_any_dag();

    if (node_id == 1)
    {
      if (wait_dag != NULL)
      {
        rpl_ready_flag = 1;
      }
    }
    else if (dag_has_parent(wait_dag))
    {
      rpl_ready_flag = 1;
    }

    if (rpl_ready_flag)
    {
      break;
    }

    if (clock_time() >= rpl_wait_deadline)
    {
      rpl_timeout_flag = 1;
      break;
    }

    etimer_set(&rpl_wait_timer, RPL_READY_POLL_INTERVAL);
    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER && data == &rpl_wait_timer);
  }

  etimer_stop(&rpl_wait_timer);

  if (rpl_timeout_flag)
  {
    APP_LOG("APP-RPL: readiness timeout, continue best-effort\n");
  }

  if (node_id != 1)
  {
    rpl_dag_t *ready_dag = rpl_get_any_dag();
    if (dag_has_parent(ready_dag))
    {
      const linkaddr_t *ready_parent = rpl_get_parent_lladdr(ready_dag->preferred_parent);
      csv_print_info_role("NODE", dag_rank_to_hops(ready_dag), ready_parent);
    }
  }
  else
  {
    csv_print_info_role("SINK", 0, NULL);
  }

  if (node_id != 1)
  {
    etimer_set(&ul_timer, MSG_PERIOD);
  }
  if (node_id == 1)
  {
    etimer_set(&dl_timer, SR_MSG_PERIOD);
  }
  etimer_set(&stats_timer, PDR_PRINT_PERIOD);

  while (1)
  {
    PROCESS_WAIT_EVENT();

    if (ev == PROCESS_EVENT_TIMER)
    {
      if (data == &ul_timer)
      {
        if (node_id != 1)
        {
          clock_time_t jitter;
          rpl_dag_t *period_dag;

          etimer_reset(&ul_timer);

          period_dag = rpl_get_any_dag();
          if (!dag_has_parent(period_dag))
          {
            parent_tracker_update(NULL, 0xFFFF);
            APP_LOG("APP-UL[SKIP]: no parent yet\n");
          }
          else
          {
            jitter = compute_ul_jitter();
            etimer_stop(&ul_jitter_timer);
            if (jitter > 0)
            {
              etimer_set(&ul_jitter_timer, jitter);
            }
            else
            {
              send_ul_message();
            }
          }
        }
      }
      else if (data == &ul_jitter_timer)
      {
        send_ul_message();
      }
      else if (data == &dl_timer)
      {
        if (node_id == 1)
        {
          uint8_t target_id;

          etimer_reset(&dl_timer);

          target_id = pick_next_dl_target();
          if (target_id >= DL_ROTATION_START && target_id < MAP_MAX_NODES && id_ip_map[target_id].known)
          {
            dl_msg_t payload;
            uip_ipaddr_t dst;

            payload.seqn = dl_seq;
            payload.timestamp = (uint32_t)clock_time();
            uip_ipaddr_copy(&dst, &id_ip_map[target_id].ip6);
            APP_LOG("APP-DL[SINK]: send SR seq=%u -> %02u:%02u\n", (unsigned)dl_seq, (unsigned)target_id, 0);
            simple_udp_sendto(&dl_conn, &payload, sizeof(payload), &dst);
            dl_seq++;
          }
          else
          {
            APP_LOG("APP-DL[SINK]: skip (no known UL target)\n");
          }
        }
      }
      else if (data == &stats_timer)
      {
        rpl_dag_t *stats_dag;
        const linkaddr_t *parent_ref;

        etimer_reset(&stats_timer);

        stats_dag = rpl_get_any_dag();
        if (node_id == 1)
        {
          pdr_ul_print_csv(dag_rank_to_hops(stats_dag), NULL);
        }
        else
        {
          parent_ref = (stats_dag && stats_dag->preferred_parent) ? rpl_get_parent_lladdr(stats_dag->preferred_parent) : NULL;
          {
            linkaddr_t sink_ll;
            memset(&sink_ll, 0, sizeof(sink_ll));
            sink_ll.u8[3] = 1;
            sink_ll.u8[4] = 0; /* 01:00 */
            pdr_dl_print_csv(dag_rank_to_hops(stats_dag), parent_ref, &sink_ll);
          }
        }
      }
    }
  }

  PROCESS_END();
}
