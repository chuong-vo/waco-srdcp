/*
 * WaCo + RPL UDP baseline (skip mode = chuẩn RPL)
 *
 * Behavior:
 *  - RPL non-storing, ContikiMAC baseline style
 *  - Downlink: sink only sends if RPL has a valid downward route
 *    => RPL will attach SRH (Source Routing Header) itself
 *  - If no route, we SKIP the send (this is how a real RPL app behaves)
 *    BUT we still log a STAT,DL_ATTEMPT so we can analyze coverage later
 *  - UL / DL delay, PDR, PRR exported in CSV for plotting
 *  - Node IDs are always logged as XX:YY from link-layer addr bytes [3],[4]
 *    to match SRDCP-style logs
 *  - Warm-up / RPL-ready wait to avoid measuring transient boot mess
 */

#include "contiki.h"
#include "lib/random.h"
#include "net/ip/simple-udp.h"
#include "net/ipv6/uip-ds6.h"
#include "net/rpl/rpl.h"
#include "net/rpl/rpl-ns.h"
#include "net/netstack.h"
#include "core/net/linkaddr.h"
#include "node-id.h"
#include <stdio.h>
#include <string.h>

/* =================== logging control =================== */
#ifndef LOG_APP
#define LOG_APP 1
#endif
/* Minimal logging: only essential logs for metrics extraction */
#ifndef LOG_APP_MINIMAL
#define LOG_APP_MINIMAL 1
#endif
#if LOG_APP && !LOG_APP_MINIMAL
#define APP_LOG(...) printf(__VA_ARGS__)
#elif LOG_APP && LOG_APP_MINIMAL
/* Only log critical metrics: APP-UL/DL send/got for parser */
#define APP_LOG_METRICS(...) printf(__VA_ARGS__)
#define APP_LOG_DEBUG(...) /* silent */
#define APP_LOG(...) APP_LOG_METRICS(__VA_ARGS__)
#else
#define APP_LOG(...)
#define APP_LOG_METRICS(...)
#define APP_LOG_DEBUG(...)
#endif

/* =================== app parameters =================== */
#ifndef APP_NODES
#define APP_NODES 5 /* total logical nodes including sink=1 */
#endif

#ifndef MSG_PERIOD
#define MSG_PERIOD (30 * CLOCK_SECOND) /* UL period */
#endif

#ifndef SR_MSG_PERIOD
#define SR_MSG_PERIOD (45 * CLOCK_SECOND) /* DL period (after warm-up) */
#endif

#ifndef PDR_PRINT_PERIOD
#define PDR_PRINT_PERIOD (30 * CLOCK_SECOND) /* CSV snapshot period */
#endif

#define UL_PORT 8765
#define DL_PORT 8766

/* Wait so RPL can form */
#define RPL_READY_TIMEOUT (240UL * CLOCK_SECOND)
#define RPL_READY_POLL_INTERVAL (CLOCK_SECOND)

/* Sink waits extra before starting DL traffic */
#define WARMUP_DL_DELAY (240UL * CLOCK_SECOND)

/* Start rotating DL targets from node ID 2 upward */
#define DL_ROTATION_START 2

/* =================== Forward declarations =================== */
static uint16_t dag_rank_to_hops(const rpl_dag_t *dag);
static uint8_t dag_has_parent(const rpl_dag_t *dag);
static clock_time_t compute_ul_jitter(void);
static clock_time_t compute_dl_jitter(void);
static void parent_tracker_update(const linkaddr_t *new_parent, uint16_t hops_est);
static void send_ul_message(void);
static void send_dl_message(void);
static uint8_t pick_next_dl_target(void);
static uint16_t rpl_hops_approx(void);

/* =================== SRDCP-style ID helpers ===================
 * We standardize node identity as XX:YY using link-layer addr bytes.
 * Must match SRDCP logs => easy comparison.
 */
static inline void addr_to_id00(const linkaddr_t *a, uint8_t *id0, uint8_t *id1)
{
  if (!a)
  {
    *id0 = 0;
    *id1 = 0;
    return;
  }
#if LINKADDR_SIZE >= 5
  /* On Sky/Tmote-style addresses, bytes [3],[4] are used as short ID */
  *id0 = a->u8[3];
  *id1 = a->u8[4];
#else
  /* fallback: last two bytes */
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

/* =================== UL/DL message formats =================== */
typedef struct
{
  uint16_t seqn;
  uint16_t metric;    /* hop-count-ish metric from RPL rank */
  uint8_t src0, src1; /* XX:YY ID of source */
  uint32_t timestamp; /* clock_time() when queued for UL */
} __attribute__((packed)) ul_msg_t;

typedef struct
{
  uint16_t seqn;      /* sequence for DL deliveries that were ACTUALLY sent */
  uint32_t timestamp; /* timestamp at sink right before send */
} __attribute__((packed)) dl_msg_t;

/* UDP sockets: UL (node -> sink), DL (sink -> node) */
static struct simple_udp_connection ul_conn;
static struct simple_udp_connection dl_conn;

/* sequence counters */
static uint16_t ul_seq = 0;         /* only increments on REAL UL sends */
static uint16_t ul_attempt_seq = 0; /* increments on every UL attempt, even if skipped */
static uint16_t dl_seq = 0;         /* only increments on REAL DL sends */
static uint16_t dl_attempt_seq = 0; /* increments on every DL attempt, even if skipped */

/* rotate DL targets across nodes 2..APP_NODES */
static uint8_t next_dl = DL_ROTATION_START;

/* Map node short-ID -> last-seen IPv6 from UL, learned at sink */
typedef struct
{
  uint8_t known;
  uip_ipaddr_t ip6;
} id_ip_t;

#ifndef MAP_MAX_NODES
#define MAP_MAX_NODES 32
#endif
static id_ip_t id_ip_map[MAP_MAX_NODES];

/* Track parent changes per node for logging */
static linkaddr_t tracked_parent;
static uint8_t parent_is_known = 0;

/* Jitter timers to de-sync bursts */
static struct etimer ul_jitter_timer;
static struct etimer dl_jitter_timer;

/* =================== CSV PDR/PRR stats ===================
 * We keep per-source UL stats at the sink,
 * and per-node DL stats at each node.
 */
typedef struct
{
  uint8_t used;
  uint8_t id0, id1; /* peer ID XX:YY */
  uint16_t first_seq;
  uint16_t last_seq;
  uint16_t received;
  uint16_t gaps;
  uint16_t dups;
} pdr_ul_t;

#define PDR_MAX_SRC 32
static pdr_ul_t pdr_ul[PDR_MAX_SRC];

static uint8_t csv_ul_header_printed = 0;
static uint8_t csv_dl_header_printed = 0;

static pdr_ul_t *pdr_ul_find_or_add(uint8_t id0, uint8_t id1)
{
  int i, free_i = -1;
  for (i = 0; i < PDR_MAX_SRC; i++)
  {
    if (pdr_ul[i].used &&
        pdr_ul[i].id0 == id0 &&
        pdr_ul[i].id1 == id1)
    {
      return &pdr_ul[i];
    }
    if (!pdr_ul[i].used && free_i < 0)
    {
      free_i = i;
    }
  }
  if (free_i >= 0)
  {
    pdr_ul[free_i].used = 1;
    pdr_ul[free_i].id0 = id0;
    pdr_ul[free_i].id1 = id1;
    pdr_ul[free_i].first_seq = 0;
    pdr_ul[free_i].last_seq = 0;
    pdr_ul[free_i].received = 0;
    pdr_ul[free_i].gaps = 0;
    pdr_ul[free_i].dups = 0;
    return &pdr_ul[free_i];
  }
  return NULL;
}

static void pdr_ul_maybe_reset(pdr_ul_t *st, uint16_t seq)
{
  /* reset window if wrap-around happened and we've already collected enough */
  if (st->received > 10 && seq < 3 && st->last_seq > 100)
  {
    st->first_seq = seq;
    st->last_seq = seq;
    st->received = 0;
    st->gaps = 0;
    st->dups = 0;
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
    /* gap -> lost packets */
    st->gaps += (uint16_t)(seq - st->last_seq - 1);
    st->received++;
    st->last_seq = seq;
  }
  else
  {
    /* out-of-order or duplicate */
    st->dups++;
  }
}

/* --- DL PDR at each node (1 sink -> N nodes) --- */
typedef struct
{
  uint8_t inited;
  uint16_t first_seq;
  uint16_t last_seq;
  uint16_t received;
  uint16_t gaps;
  uint16_t dups;
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

/* =================== CSV helpers =================== */

static void csv_print_info_headers_once(void)
{
  static uint8_t printed = 0;
  if (printed)
    return;
  printed = 1;
  printf("CSV,INFO_HDR,fields=local,time,role,parent,my_hops\n");
}

/* role is "SINK" or "NODE"
 * my_hops ~ hop distance to sink (approx from RPL rank)
 * parent is XX:YY of my preferred parent
 */
static void csv_print_info_role(const char *role,
                                uint16_t hops_est,
                                const linkaddr_t *parent)
{
  uint8_t me0, me1, p0, p1;
  addr_to_id00(&linkaddr_node_addr, &me0, &me1);
  addr_to_id00(parent, &p0, &p1);

  printf("CSV,INFO,local=%02u:%02u,%lu,%s,%02u:%02u,%u\n",
         me0, me1,
         (unsigned long)(clock_time() / CLOCK_SECOND),
         role,
         p0, p1,
         hops_est);
}

/* sink prints UL stats for all sources it has seen */
static void pdr_ul_print_csv(uint16_t my_hops,
                             const linkaddr_t *parent)
{
  uint8_t me0, me1, p0, p1;
  int i;

  addr_to_id00(&linkaddr_node_addr, &me0, &me1);
  addr_to_id00(parent, &p0, &p1);

  if (!csv_ul_header_printed)
  {
    printf("CSV,PDR_UL,local=%02u:%02u,time,peer,first,last,recv,gaps,dups,expected,PDR%%,parent,my_hops\n",
           me0, me1);
    csv_ul_header_printed = 1;
  }

  for (i = 0; i < PDR_MAX_SRC; i++)
  {
    if (pdr_ul[i].used)
    {
      uint32_t expected = (uint32_t)(pdr_ul[i].last_seq - pdr_ul[i].first_seq + 1);
      uint32_t recv_cnt;
      uint32_t pdrx;
      if (expected == 0)
        expected = 1;
      recv_cnt = pdr_ul[i].received;
      pdrx = (recv_cnt * 10000UL) / expected; /* percentage *100 */

      printf("CSV,PDR_UL,local=%02u:%02u,%lu,%02u:%02u,%u,%u,%lu,%lu,%lu,%lu,%lu.%02lu,%02u:%02u,%u\n",
             me0, me1,
             (unsigned long)(clock_time() / CLOCK_SECOND),
             pdr_ul[i].id0, pdr_ul[i].id1,
             pdr_ul[i].first_seq, pdr_ul[i].last_seq,
             (unsigned long)recv_cnt,
             (unsigned long)pdr_ul[i].gaps,
             (unsigned long)pdr_ul[i].dups,
             (unsigned long)expected,
             (unsigned long)(pdrx / 100),
             (unsigned long)(pdrx % 100),
             p0, p1,
             my_hops);

      /* alias PRR_UL == PDR_UL for plotting convenience */
      printf("CSV,PRR_UL,local=%02u:%02u,%lu,%02u:%02u,%lu.%02lu\n",
             me0, me1,
             (unsigned long)(clock_time() / CLOCK_SECOND),
             pdr_ul[i].id0, pdr_ul[i].id1,
             (unsigned long)(pdrx / 100),
             (unsigned long)(pdrx % 100));
    }
  }
}

/* each node prints DL stats about packets received from the sink */
static void pdr_dl_print_csv(uint16_t my_hops,
                             const linkaddr_t *parent,
                             const linkaddr_t *sink_ll)
{
  uint8_t me0, me1, p0, p1, s0, s1;
  uint32_t expected;
  uint32_t pdrx;

  addr_to_id00(&linkaddr_node_addr, &me0, &me1);
  addr_to_id00(parent, &p0, &p1);
  addr_to_id00(sink_ll, &s0, &s1);

  if (!csv_dl_header_printed)
  {
    printf("CSV,PDR_DL,local=%02u:%02u,time,peer,first,last,recv,gaps,dups,expected,PDR%%,parent,my_hops\n",
           me0, me1);
    csv_dl_header_printed = 1;
  }

  if (!pdr_dl.inited)
  {
    return;
  }

  expected = (uint32_t)(pdr_dl.last_seq - pdr_dl.first_seq + 1);
  if (expected == 0)
    expected = 1;
  pdrx = (pdr_dl.received * 10000UL) / expected;

  printf("CSV,PDR_DL,local=%02u:%02u,%lu,%02u:%02u,%u,%u,%lu,%lu,%lu,%lu,%lu.%02lu,%02u:%02u,%u\n",
         me0, me1,
         (unsigned long)(clock_time() / CLOCK_SECOND),
         s0, s1,
         pdr_dl.first_seq, pdr_dl.last_seq,
         (unsigned long)pdr_dl.received,
         (unsigned long)pdr_dl.gaps,
         (unsigned long)pdr_dl.dups,
         (unsigned long)expected,
         (unsigned long)(pdrx / 100),
         (unsigned long)(pdrx % 100),
         p0, p1, my_hops);

  /* alias PRR_DL == PDR_DL */
  printf("CSV,PRR_DL,local=%02u:%02u,%lu,%02u:%02u,%lu.%02lu\n",
         me0, me1,
         (unsigned long)(clock_time() / CLOCK_SECOND),
         s0, s1,
         (unsigned long)(pdrx / 100),
         (unsigned long)(pdrx % 100));
}

/* =================== RPL / topology helpers =================== */

/* Convert RPL rank to "approx hopcount" */
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

/* Has this DAG got a preferred parent yet? */
static uint8_t dag_has_parent(const rpl_dag_t *dag)
{
  return (dag != NULL && dag->preferred_parent != NULL);
}

/* jitter so nodes don't all TX at same time */
static clock_time_t compute_ul_jitter(void)
{
  clock_time_t window = MSG_PERIOD / 2;
  if (window == 0)
  {
    return 0;
  }
  return (clock_time_t)(random_rand() % window);
}

static clock_time_t compute_dl_jitter(void)
{
  clock_time_t window = SR_MSG_PERIOD / 2;
  if (window == 0)
  {
    return 0;
  }
  return (clock_time_t)(random_rand() % window);
}

/* Log whenever parent changes */
static void parent_tracker_update(const linkaddr_t *new_parent,
                                  uint16_t hops_est)
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
      printf("ROUTE[NODE %02u:%02u]: parent %02u:%02u -> --:-- hops=%u\n",
             (unsigned)me0, (unsigned)me1,
             (unsigned)old0, (unsigned)old1,
             (unsigned)hops_est);
      parent_is_known = 0;
      memset(&tracked_parent, 0, sizeof(tracked_parent));
    }
    return;
  }

  addr_to_id00(new_parent, &new0, &new1);
  if (!parent_is_known ||
      memcmp(tracked_parent.u8, new_parent->u8, LINKADDR_SIZE) != 0)
  {
    printf("ROUTE[NODE %02u:%02u]: parent %02u:%02u -> %02u:%02u hops=%u\n",
           (unsigned)me0, (unsigned)me1,
           (unsigned)old0, (unsigned)old1,
           (unsigned)new0, (unsigned)new1,
           (unsigned)hops_est);
    linkaddr_copy(&tracked_parent, new_parent);
    parent_is_known = 1;
  }
}

/* Send UL data from a normal node -> sink */
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
    /* Log UL attempt with route_ok=0 (like DL) */
    addr_to_id00(&linkaddr_node_addr, &me0, &me1);
    printf("STAT,UL_ATTEMPT,time=%lu,source=%02u:%02u,attempt_seq=%u,route_ok=0\n",
           (unsigned long)(clock_time() / CLOCK_SECOND),
           (unsigned)me0, (unsigned)me1,
           (unsigned)ul_attempt_seq);
    return;
  }

  parent = rpl_get_parent_lladdr(curr_dag->preferred_parent);
  hops = dag_rank_to_hops(curr_dag);

  parent_tracker_update(parent, hops);

  /* Fill UL message */
  m.seqn = ul_seq++;
  m.metric = hops;

  /* IMPORTANT: unified ID with SRDCP */
  addr_to_id00(&linkaddr_node_addr, &m.src0, &m.src1);

  m.timestamp = (uint32_t)clock_time();

  addr_to_id00(&linkaddr_node_addr, &me0, &me1);
  addr_to_id00(parent, &p0, &p1);

  APP_LOG("APP-UL[NODE %02u:%02u]: send seq=%u hops=%u parent=%02u:%02u\n",
          (unsigned)me0, (unsigned)me1,
          (unsigned)m.seqn,
          (unsigned)m.metric,
          (unsigned)p0, (unsigned)p1);

  /* Log UL attempt with route_ok=1 (like DL) */
  printf("STAT,UL_ATTEMPT,time=%lu,source=%02u:%02u,attempt_seq=%u,route_ok=1,ul_seq=%u\n",
         (unsigned long)(clock_time() / CLOCK_SECOND),
         (unsigned)me0, (unsigned)me1,
         (unsigned)ul_attempt_seq,
         (unsigned)ul_seq);

  /* send to DAG root (sink IPv6) */
  simple_udp_sendto(&ul_conn, &m, sizeof(m), &curr_dag->dag_id);
}

/* Round-robin pick next DL target (2..APP_NODES) that we've seen uplink from */
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
      /* advance pointer for next time */
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

  /* fallback */
  next_dl = DL_ROTATION_START;
  return 0;
}

/*
 * Sink -> Node DL (control) using RPL non-storing downward SRH.
 *
 * This is the "skip" baseline:
 *  - We ONLY send if RPL has a valid downward route.
 *  - If no route, we SKIP actual transmit (this matches real RPL app behavior).
 *  - We log STAT,DL_ATTEMPT (with attempt_seq and route_ok flag)
 *    so we can still analyze coverage offline.
 *
 * dl_seq++ ONLY when we actually sent (route_ok=1).
 * dl_attempt_seq++ ALWAYS, for each timer attempt.
 */
static void send_dl_message(void)
{
  uint8_t target_id;
  rpl_dag_t *curr_dag;
  rpl_ns_node_t *dest_node;

  /* every DL timer tick (after warm-up) is an attempt */
  dl_attempt_seq++;

  target_id = pick_next_dl_target();

  if (target_id >= DL_ROTATION_START &&
      target_id < MAP_MAX_NODES &&
      id_ip_map[target_id].known)
  {

    dl_msg_t payload;
    uip_ipaddr_t dst;

    payload.seqn = dl_seq;
    payload.timestamp = (uint32_t)clock_time();
    uip_ipaddr_copy(&dst, &id_ip_map[target_id].ip6);

    /* Check RPL DAG / route availability */
    curr_dag = rpl_get_any_dag();
    if (curr_dag)
    {
      dest_node = rpl_ns_get_node(curr_dag, &dst);
      if (dest_node && rpl_ns_is_node_reachable(curr_dag, &dst))
      {
        /* route exists => RPL will generate SRH on send */
        APP_LOG("APP-DL[SINK]: send via RPL SRH dl_seq=%u -> %02u:00 (reachable)\n",
                (unsigned)dl_seq,
                (unsigned)target_id);

        /* For fairness/coverage analysis */
        printf("STAT,DL_ATTEMPT,time=%lu,attempt_seq=%u,target=%u:00,route_ok=1,dl_seq=%u\n",
               (unsigned long)(clock_time() / CLOCK_SECOND),
               (unsigned)dl_attempt_seq,
               (unsigned)target_id,
               (unsigned)dl_seq);

        simple_udp_sendto(&dl_conn, &payload, sizeof(payload), &dst);
        dl_seq++;
        return;
      }
      else
      {
        /* known target but no downward route in RPL */
        APP_LOG_DEBUG("APP-DL[SINK]: skip dl_attempt=%u -> %02u:00 (no route in RPL)\n",
                (unsigned)dl_attempt_seq,
                (unsigned)target_id);

        printf("STAT,DL_ATTEMPT,time=%lu,attempt_seq=%u,target=%u:00,route_ok=0\n",
               (unsigned long)(clock_time() / CLOCK_SECOND),
               (unsigned)dl_attempt_seq,
               (unsigned)target_id);
      }
    }
    else
    {
      /* no DAG at sink?! */
      APP_LOG_DEBUG("APP-DL[SINK]: skip dl_attempt=%u -> %02u:00 (no DAG)\n",
              (unsigned)dl_attempt_seq,
              (unsigned)target_id);

      printf("STAT,DL_ATTEMPT,time=%lu,attempt_seq=%u,target=%u:00,route_ok=0\n",
             (unsigned long)(clock_time() / CLOCK_SECOND),
             (unsigned)dl_attempt_seq,
             (unsigned)target_id);
    }
  }
  else
  {
    /* nobody to send to (no UL heard yet etc.) */
    APP_LOG_DEBUG("APP-DL[SINK]: skip dl_attempt=%u (no known UL target)\n",
            (unsigned)dl_attempt_seq);

    printf("STAT,DL_ATTEMPT,time=%lu,attempt_seq=%u,target=--:--,route_ok=0\n",
           (unsigned long)(clock_time() / CLOCK_SECOND),
           (unsigned)dl_attempt_seq);
  }
}

/* hop approx from local RPL rank */
static uint16_t rpl_hops_approx(void)
{
  return dag_rank_to_hops(rpl_get_any_dag());
}

/* =================== UDP callbacks =================== */

/* UL receive at sink */
static void ul_rx_cb(struct simple_udp_connection *c,
                     const uip_ipaddr_t *sender_addr,
                     uint16_t sender_port,
                     const uip_ipaddr_t *receiver_addr,
                     uint16_t receiver_port,
                     const uint8_t *data,
                     uint16_t datalen)
{
  if (datalen >= sizeof(ul_msg_t))
  {
    const ul_msg_t *m = (const ul_msg_t *)data;

    /* Sink prints UL log similar to SRDCP */
    APP_LOG("APP-UL[SINK]: got seq=%u from %02u:%02u hops=%u\n",
            (unsigned)m->seqn,
            (unsigned)m->src0, (unsigned)m->src1,
            (unsigned)m->metric);

    /* UL delay calc (sink now - node timestamp) */
    {
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
    }

    /* teach sink the IPv6 for DL later */
    if (node_id == 1)
    {
      if (m->src0 < MAP_MAX_NODES)
      {
        id_ip_map[m->src0].known = 1;
        uip_ipaddr_copy(&id_ip_map[m->src0].ip6, sender_addr);
      }
      /* update UL PDR stats at sink */
      pdr_ul_update(m->src0, m->src1, m->seqn);
    }
  }
}

/* DL receive at node */
static void dl_rx_cb(struct simple_udp_connection *c,
                     const uip_ipaddr_t *sender_addr,
                     uint16_t sender_port,
                     const uip_ipaddr_t *receiver_addr,
                     uint16_t receiver_port,
                     const uint8_t *data,
                     uint16_t datalen)
{
  if (datalen >= sizeof(dl_msg_t))
  {
    const dl_msg_t *msg = (const dl_msg_t *)data;
    uint16_t seq = msg->seqn;

    rpl_dag_t *dag = rpl_get_any_dag();
    const linkaddr_t *pl = (dag && dag->preferred_parent)
                               ? rpl_get_parent_lladdr(dag->preferred_parent)
                               : NULL;

    uint8_t me0, me1;
    addr_to_id00(&linkaddr_node_addr, &me0, &me1);

    linkaddr_t parent_tmp = linkaddr_null;
    if (pl)
    {
      linkaddr_copy(&parent_tmp, pl);
    }

    {
      uint8_t p0, p1;
      addr_to_id00(&parent_tmp, &p0, &p1);

      APP_LOG("APP-DL[NODE %02u:%02u]: got SR seq=%u hops=%u parent=%02u:%02u\n",
              (unsigned)me0, (unsigned)me1,
              (unsigned)seq,
              (unsigned)rpl_hops_approx(),
              (unsigned)p0, (unsigned)p1);
    }

    /* DL delay calc (now - sink timestamp) */
    {
      clock_time_t now = clock_time();
      clock_time_t ts = (clock_time_t)msg->timestamp;
      uint32_t delay_ticks = (now >= ts) ? (uint32_t)(now - ts) : 0;

      printf("STAT,DL_DELAY,local=%02u:%02u,time=%lu,delay_ticks=%lu\n",
             (unsigned)me0, (unsigned)me1,
             (unsigned long)(now / CLOCK_SECOND),
             (unsigned long)delay_ticks);
    }

    /* update DL PDR stats locally */
    pdr_dl_update(seq);
  }
}

/* =================== Main Contiki process =================== */

PROCESS(waco_rpl_process, "WaCo + RPL UDP baseline (skip mode)");
AUTOSTART_PROCESSES(&waco_rpl_process);

PROCESS_THREAD(waco_rpl_process, ev, data)
{
  static struct etimer ul_timer, dl_timer, stats_timer, warmup_timer;
  static struct etimer rpl_wait_timer;

  linkaddr_t me_ll = linkaddr_node_addr;
  char mebuf[6] = {0};

  static clock_time_t rpl_wait_deadline;
  static uint8_t rpl_ready_flag;
  static uint8_t rpl_timeout_flag;
  static uint8_t warmup_done_flag;

  PROCESS_BEGIN();

  memset(&tracked_parent, 0, sizeof(tracked_parent));
  parent_is_known = 0;
  memset(id_ip_map, 0, sizeof(id_ip_map));
  next_dl = DL_ROTATION_START;
  dl_seq = 0;
  dl_attempt_seq = 0;

  print_addr_id(&me_ll, mebuf, sizeof(mebuf));

  /* Build global IPv6 based on link-layer IID */
  {
    uip_ipaddr_t ipaddr;
    uip_ip6addr(&ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 0);
    uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
    uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);

    /* If I'm sink (ID==1), become RPL root */
    if (node_id == 1)
    {
      rpl_dag_t *dag = rpl_set_root(RPL_DEFAULT_INSTANCE, &ipaddr);
      if (dag)
      {
        rpl_set_prefix(dag, &ipaddr, 64);
      }
    }
  }

  /* Register UDP sockets */
  simple_udp_register(&ul_conn, UL_PORT, NULL, UL_PORT, ul_rx_cb);
  simple_udp_register(&dl_conn, DL_PORT, NULL, DL_PORT, dl_rx_cb);

  /* Initial CSV headers + role announcement */
  csv_print_info_headers_once();

  if (node_id == 1)
  {
    APP_LOG_DEBUG("APP-ROLE[SINK]: started (local=%s)\n", mebuf);
    csv_print_info_role("SINK", 0, NULL);
    /* PDR_UL snapshot at the sink (my_hops ~ 0) */
    pdr_ul_print_csv(rpl_hops_approx(), NULL);
  }
  else
  {
    rpl_dag_t *init_dag = rpl_get_any_dag();
    const linkaddr_t *init_parent =
        (init_dag && init_dag->preferred_parent)
            ? rpl_get_parent_lladdr(init_dag->preferred_parent)
            : NULL;
    APP_LOG_DEBUG("APP-ROLE[NODE %s]: started\n", mebuf);
    csv_print_info_role("NODE", dag_rank_to_hops(init_dag), init_parent);

    /* print initial DL PDR status line */
    {
      linkaddr_t sink_ll;
      memset(&sink_ll, 0, sizeof(sink_ll));
      sink_ll.u8[3] = 1;
      sink_ll.u8[4] = 0; /* 01:00 in XX:YY form */
      pdr_dl_print_csv(dag_rank_to_hops(init_dag), init_parent, &sink_ll);
    }
  }

  /* === Phase 1: wait for RPL to become ready === */
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
    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER &&
                             data == &rpl_wait_timer);
  }

  etimer_stop(&rpl_wait_timer);

  if (rpl_timeout_flag)
  {
    APP_LOG_DEBUG("APP-RPL: readiness timeout, continue best-effort\n");
  }

  /* After RPL "ready", print updated role snapshot */
  if (node_id != 1)
  {
    rpl_dag_t *ready_dag = rpl_get_any_dag();
    if (dag_has_parent(ready_dag))
    {
      const linkaddr_t *ready_parent =
          rpl_get_parent_lladdr(ready_dag->preferred_parent);
      csv_print_info_role("NODE",
                          dag_rank_to_hops(ready_dag),
                          ready_parent);
    }
  }
  else
  {
    csv_print_info_role("SINK", 0, NULL);
  }

  /* === Phase 2: start periodic timers === */

  warmup_done_flag = 0;

  if (node_id != 1)
  {
    etimer_set(&ul_timer, MSG_PERIOD);
  }

  if (node_id == 1)
  {
    /* sink waits warm-up period before starting DL */
    etimer_set(&warmup_timer, WARMUP_DL_DELAY);
    // APP_LOG("APP-WARMUP[SINK]: waiting %lu s before DL traffic\n",
    // (unsigned long)(WARMUP_DL_DELAY / CLOCK_SECOND));
  }

  etimer_set(&stats_timer, PDR_PRINT_PERIOD);

  /* main event loop */
  while (1)
  {
    PROCESS_WAIT_EVENT();

    if (ev == PROCESS_EVENT_TIMER)
    {

      /* warm-up for sink before DL */
      if (data == &warmup_timer)
      {
        if (node_id == 1 && !warmup_done_flag)
        {
          warmup_done_flag = 1;
          etimer_stop(&warmup_timer);
          etimer_set(&dl_timer, SR_MSG_PERIOD);
          //  APP_LOG("APP-WARMUP[SINK]: done, starting DL traffizc\n");
        }
      }
      else if (data == &ul_timer)
      {
        /* periodic UL from nodes */
        if (node_id != 1)
        {
          clock_time_t jitter;
          rpl_dag_t *period_dag;

          etimer_reset(&ul_timer);

          period_dag = rpl_get_any_dag();
          /* Track UL attempts (like DL) */
          ul_attempt_seq++;
          
          if (!dag_has_parent(period_dag))
          {
            parent_tracker_update(NULL, 0xFFFF);
            APP_LOG_DEBUG("APP-UL[SKIP]: no parent yet\n");
            /* Log UL attempt with route_ok=0 */
            {
              uint8_t me0, me1;
              addr_to_id00(&linkaddr_node_addr, &me0, &me1);
              printf("STAT,UL_ATTEMPT,time=%lu,source=%02u:%02u,attempt_seq=%u,route_ok=0\n",
                     (unsigned long)(clock_time() / CLOCK_SECOND),
                     (unsigned)me0, (unsigned)me1,
                     (unsigned)ul_attempt_seq);
            }
          }
          else
          {
            /* schedule UL with jitter */
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
        /* send UL now */
        send_ul_message();
      }
      else if (data == &dl_timer)
      {
        /* periodic DL from SINK after warm-up */
        if (node_id == 1)
        {
          clock_time_t jitter;

          etimer_reset(&dl_timer);

          jitter = compute_dl_jitter();
          etimer_stop(&dl_jitter_timer);
          if (jitter > 0)
          {
            etimer_set(&dl_jitter_timer, jitter);
          }
          else
          {
            send_dl_message();
          }
        }
      }
      else if (data == &dl_jitter_timer)
      {
        /* send DL now */
        send_dl_message();
      }
      else if (data == &stats_timer)
      {
        /* periodic CSV dump */
        rpl_dag_t *stats_dag;
        const linkaddr_t *parent_ref;

        etimer_reset(&stats_timer);

        stats_dag = rpl_get_any_dag();

        if (node_id == 1)
        {
          /* sink prints UL PDR from all sources */
          pdr_ul_print_csv(dag_rank_to_hops(stats_dag), NULL);
        }
        else
        {
          /* node prints DL PDR from sink */
          parent_ref = (stats_dag && stats_dag->preferred_parent)
                           ? rpl_get_parent_lladdr(stats_dag->preferred_parent)
                           : NULL;

          {
            linkaddr_t sink_ll;
            memset(&sink_ll, 0, sizeof(sink_ll));
            sink_ll.u8[3] = 1;
            sink_ll.u8[4] = 0; /* sink is 01:00 */
            pdr_dl_print_csv(dag_rank_to_hops(stats_dag),
                             parent_ref,
                             &sink_ll);
          }
        }
      }
    }
  }

  PROCESS_END();
}
