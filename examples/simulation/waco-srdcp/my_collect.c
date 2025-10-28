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
#define TAG_PRR "PRR"
#define TAG_STAB "STAB"
#define TAG_GRAPH "GRAPH"
/* ===== Logging toggle for my_collect.c ===== */
#ifndef LOG_COLLECT
#define LOG_COLLECT 0 /* 1: enable log; 0: disable log */
#endif

#if LOG_COLLECT
#define LOG(tag, fmt, ...) printf(tag ": " fmt "\n", ##__VA_ARGS__)
#else
#define LOG(tag, fmt, ...) \
        do                 \
        {                  \
        } while (0)
#endif

// #define LOG(tag, fmt, ...) printf(tag ": " fmt "\n", ##__VA_ARGS__)
const linkaddr_t sink_addr = {{0x01, 0x00}};
/* ---- PRR tie-break config ---- */
#ifndef PRR_NEI_MAX
#define PRR_NEI_MAX 24
#endif
#ifndef PRR_HYSTERESIS
/* Require PRR to be better by this many percentage points to switch on tie */
#define PRR_HYSTERESIS 25
#endif
/* Absolute PRR requirement for a candidate (percent) */
#ifndef PRR_ABS_MIN
#define PRR_ABS_MIN 80
#endif
/* Minimum PRR required to accept an improved-hops parent (percent) */
#ifndef PRR_IMPROVE_MIN
#define PRR_IMPROVE_MIN 60
#endif
/* Minimum time to keep chosen parent before tie-break switches (ticks) */
/* Fast-convergence: allow faster parent changes under PRR tie-breaks */
#ifndef MIN_PARENT_DWELL
#define MIN_PARENT_DWELL (30 * CLOCK_SECOND)
#endif
/* Parent timeout (no beacon from parent for too long -> stale) */
#ifndef PARENT_TIMEOUT
#define PARENT_TIMEOUT (4 * BEACON_INTERVAL)
#endif

#ifndef SRDCP_NEIGHBOR_STALE_TICKS
#define SRDCP_NEIGHBOR_STALE_TICKS (3 * BEACON_INTERVAL)
#endif

typedef struct
{
        uint8_t used;
        linkaddr_t addr;
        uint16_t last_tx_seq;
        uint32_t expected;      /* beacons expected based on tx_seq deltas */
        uint32_t received;      /* beacons actually received */
        clock_time_t last_seen; /* last time a beacon was observed from this neighbor */
        int8_t last_rssi;
        uint8_t last_lqi;
        uint16_t last_metric;
} prr_entry_t;

static prr_entry_t prr_tab[PRR_NEI_MAX];

/**
 * @brief Finds or adds an entry for a neighbor in the PRR statistics table.
 * @param addr The address of the neighbor.
 * @return A pointer to the neighbor's PRR entry.
 * @details If the table is full, it replaces the entry with the lowest
 *          `expected` beacon count to keep the table populated with active
 *          neighbors.
 */
static prr_entry_t *prr_lookup_or_add(const linkaddr_t *addr)
{
        int i;
        int free_i = -1;
        for (i = 0; i < PRR_NEI_MAX; i++)
        {
                if (prr_tab[i].used && linkaddr_cmp(&prr_tab[i].addr, addr))
                        return &prr_tab[i];
                if (!prr_tab[i].used && free_i < 0)
                        free_i = i;
        }
        if (free_i >= 0)
        {
                memset(&prr_tab[free_i], 0, sizeof(prr_tab[free_i]));
                prr_tab[free_i].used = 1;
                linkaddr_copy(&prr_tab[free_i].addr, addr);
                return &prr_tab[free_i];
        }
        /* replace the one with smallest expected to keep table fresh */
        int victim = 0;
        for (i = 1; i < PRR_NEI_MAX; i++)
                if (prr_tab[i].expected < prr_tab[victim].expected)
                        victim = i;
        memset(&prr_tab[victim], 0, sizeof(prr_tab[victim]));
        prr_tab[victim].used = 1;
        linkaddr_copy(&prr_tab[victim].addr, addr);
        return &prr_tab[victim];
}

/**
 * @brief Updates the PRR statistics for a neighbor upon receiving a beacon.
 * @param addr The address of the neighbor that sent the beacon.
 * @param tx_seq The per-sender beacon sequence number from the beacon payload.
 * @details This function calculates the number of expected beacons based on the
 *          delta between sequence numbers and increments the received count. This
 *          allows for an estimation of the Packet Reception Rate (PRR) of beacons.
 */
static prr_entry_t *prr_update_on_beacon(const linkaddr_t *addr, uint16_t tx_seq)
{
        prr_entry_t *e = prr_lookup_or_add(addr);
        uint16_t delta = 0;
        if (e->expected == 0 && e->received == 0)
        {
                /* first observation */
                e->last_tx_seq = tx_seq;
                e->expected = 1;
                e->received = 1;
                e->last_seen = clock_time();
                return e;
        }
        else
        {
                delta = (uint16_t)(tx_seq - e->last_tx_seq); /* handles wrap-around */
                if (delta == 0)
                        delta = 1; /* at least account for current beacon */
                e->expected += delta;
                e->received += 1;
                e->last_tx_seq = tx_seq;
                e->last_seen = clock_time();
        }
        return e;
}

/**
 * @brief Calculates the Packet Reception Rate (PRR) for a given neighbor.
 * @param addr The address of the neighbor.
 * @return The PRR as a percentage (0-100). Returns 0 if the neighbor is unknown
 *         or has no statistics yet.
 */
static uint8_t prr_percent(const linkaddr_t *addr)
{
        int i;
        for (i = 0; i < PRR_NEI_MAX; i++)
        {
                if (prr_tab[i].used && linkaddr_cmp(&prr_tab[i].addr, addr))
                {
                        if (prr_tab[i].expected == 0)
                                return 0;
                        uint32_t prr = (prr_tab[i].received * 100UL) / prr_tab[i].expected;
                        if (prr > 100)
                                prr = 100;
                        return (uint8_t)prr;
                }
        }
        return 0; /* unknown -> 0 */
}

/**
 * @brief Gets the last time a beacon was seen from a specific neighbor.
 * @param addr The address of the neighbor.
 * @return The clock_time() when the last beacon was received, or 0 if the
 *         neighbor is not in the PRR table.
 */
static clock_time_t prr_last_seen_time(const linkaddr_t *addr)
{
        int i;
        for (i = 0; i < PRR_NEI_MAX; i++)
        {
                if (prr_tab[i].used && linkaddr_cmp(&prr_tab[i].addr, addr))
                        return prr_tab[i].last_seen;
        }
        return 0;
}

/**
 * @brief Returns the observed PRR (0..100) for a neighbor.
 * @param addr The address of the neighbor to look up.
 * @return The PRR value in percent (0..100); 0 if no statistics are available.
 */
uint8_t my_collect_prr_percent(const linkaddr_t *addr)
{
        return prr_percent(addr);
}
/*--------------------------------------------------------------------------------------*/
/* Forward declarations (for clean initialization order) */
void beacon_timer_cb(void *ptr);
void send_beacon(struct my_collect_conn *conn);
void bc_recv(struct broadcast_conn *bc_conn, const linkaddr_t *sender);
void uc_recv(struct unicast_conn *uc_conn, const linkaddr_t *sender);

int sr_send(struct my_collect_conn *conn, const linkaddr_t *dest);
int my_collect_send(struct my_collect_conn *conn);

static uint8_t piggy_collect_neighbors(srdcp_piggy_neighbor_item *items, uint8_t max_items);
static srdcp_graph_node *graph_lookup_or_create(srdcp_graph_state *graph, const linkaddr_t *node);
static void graph_update_neighbors(struct my_collect_conn *conn, const linkaddr_t *owner,
                                   const srdcp_piggy_neighbor_item *items, uint8_t count,
                                   uint8_t queue_load);
static void graph_update_status(struct my_collect_conn *conn, const srdcp_node_status *status);

bool check_address_in_piggyback_block(uint8_t piggy_len, linkaddr_t node);
void forward_upward_data(struct my_collect_conn *conn, const linkaddr_t *sender);
void forward_downward_data(struct my_collect_conn *conn, const linkaddr_t *sender);

/*--------------------------------------------------------------------------------------*/
/* Callback structures */
static struct broadcast_callbacks bc_cb = {.recv = bc_recv};
static struct unicast_callbacks uc_cb = {.recv = uc_recv};

/* ------------------------------------------------------------------------------------- */
/**
 * @brief Initializes an SRDCP (collect) connection for a NODE or SINK.
 * @param conn      The collect connection structure.
 * @param channels  Channel C (SRDCP uses C and C+1).
 * @param is_sink   true if this is the SINK, false if it is a NODE.
 * @param callbacks A set of callbacks for UL/DL (recv, sr_recv).
 * @note If this is the SINK, the function will start a periodic beacon timer.
 */
void my_collect_open(struct my_collect_conn *conn, uint16_t channels,
                     bool is_sink, const struct my_collect_callbacks *callbacks)
{
        linkaddr_copy(&conn->parent, &linkaddr_null);
        conn->metric = 65535; /* not connected yet */
        conn->beacon_seqn = 0;
        conn->beacon_tx_seq = 0;
        conn->callbacks = callbacks;
        conn->treport_hold = 0;
        conn->is_sink = is_sink ? 1 : 0;
        conn->parent_lock_until = 0;

        broadcast_open(&conn->bc, channels, &bc_cb);
        unicast_open(&conn->uc, channels + 1, &uc_cb);

        if (conn->is_sink)
        {
                conn->metric = 0;
                conn->routing_table.len = 0;
                memset(&conn->graph, 0, sizeof(conn->graph));
                ctimer_set(&conn->beacon_timer, CLOCK_SECOND, beacon_timer_cb, conn);
        }
}

/* ------------------------------------ BEACON Management ------------------------------------ */

/**
 * @brief Beacon timer callback: sends a beacon and reschedules itself.
 * @param ptr Pointer to the my_collect_conn structure.
 * @note If it's the SINK, it reschedules the timer according to BEACON_INTERVAL
 *       and increments the sequence number.
 */
void beacon_timer_cb(void *ptr) // NOLINT(readability-non-const-parameter)
{
        struct my_collect_conn *conn = ptr;
        send_beacon(conn);
        if (conn->is_sink == 1)
        {
                ctimer_set(&conn->beacon_timer, BEACON_INTERVAL, beacon_timer_cb, conn);
                conn->beacon_seqn = conn->beacon_seqn + 1;
        }
}

/**
 * @brief Sends an SRDCP beacon.
 * @param conn The collect connection structure.
 * @details Increments the tx_seq counter (per-sender) for neighbors to estimate PRR.
 *          The beacon payload consists of {seqn, tx_seq, metric}.
 */
void send_beacon(struct my_collect_conn *conn)
{
        /* increase per-sender beacon counter for PRR estimation */
        conn->beacon_tx_seq++;
        struct beacon_msg beacon = {.seqn = conn->beacon_seqn, .tx_seq = conn->beacon_tx_seq, .metric = conn->metric};

        packetbuf_clear();
        packetbuf_copyfrom(&beacon, sizeof(beacon));
        LOG(TAG_BEACON, "send seq=%u metric=%u", (unsigned)conn->beacon_seqn, (unsigned)conn->metric);
        broadcast_send(&conn->bc);
}

/**
 * @brief Handles a received beacon (broadcast callback).
 * @param bc_conn Pointer to the broadcast connection.
 * @param sender  Address of the neighbor that sent the beacon.
 * @details Filters by RSSI_THRESHOLD; updates PRR(sender). For a new epoch,
 *          it only changes the parent if there is no parent or if the hop count
 *          is better. Within the same epoch, if hops are tied, it uses PRR
 *          for tie-breaking (with hysteresis, PRR_ABS_MIN, and dwell time).
 *          Finally, it schedules a beacon forward after BEACON_FORWARD_DELAY.
 */
void bc_recv(struct broadcast_conn *bc_conn, const linkaddr_t *sender)
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

        LOG(TAG_BEACON, "rx from=%02u:%02u seq=%u tx=%u metric=%u rssi=%d lqi=%u",
            sender->u8[0], sender->u8[1],
            (unsigned)beacon.seqn, (unsigned)beacon.tx_seq, (unsigned)beacon.metric, (int)rssi, (unsigned)lqi);

        /* Application hook: implemented in example-waco-srdcp.c */
        srdcp_app_beacon_observed(sender, beacon.metric, rssi, lqi);

        if (rssi < RSSI_THRESHOLD)
        {
                LOG(TAG_BEACON, "drop (rssi=%d < thr=%d)", (int)rssi, (int)RSSI_THRESHOLD);
                return;
        }

        /* Update PRR estimator for this neighbor */
        prr_entry_t *dbg = prr_update_on_beacon(sender, beacon.tx_seq);
        if (dbg)
        {
                dbg->last_rssi = rssi;
                dbg->last_lqi = lqi;
                dbg->last_metric = beacon.metric;
                /* Log integer-only PRR (no floats: Sky has no FPU) */
                uint8_t prr = prr_percent(sender);
                LOG(TAG_PRR, "nei=%02u:%02u prr=%u recv=%lu exp=%lu tx=%u rssi=%d lqi=%u metric=%u",
                    sender->u8[0], sender->u8[1],
                    (unsigned)prr,
                    (unsigned long)dbg->received,
                    (unsigned long)dbg->expected,
                    (unsigned)beacon.tx_seq,
                    (int)dbg->last_rssi,
                    (unsigned)dbg->last_lqi,
                    (unsigned)dbg->last_metric);
        }

        uint16_t new_metric = (uint16_t)(beacon.metric + 1);
        clock_time_t now = clock_time();
        /* Parent stale detection: no beacon from current parent for too long */
        bool parent_stale = false;
        if (!linkaddr_cmp(&conn->parent, &linkaddr_null))
        {
                clock_time_t ls = prr_last_seen_time(&conn->parent);
                if (ls > 0 && (now - ls) > PARENT_TIMEOUT)
                {
                        parent_stale = true;
                        LOG(TAG_STAB, "parent stale: last_seen=%lu ago > timeout=%lu",
                            (unsigned long)(now - ls), (unsigned long)PARENT_TIMEOUT);
                }
        }
        if (conn->beacon_seqn < beacon.seqn)
        {
                /* New tree epoch: update seq/metric; switch parent only if better hops or none */
                uint16_t old_metric = conn->metric;
                conn->beacon_seqn = beacon.seqn;
                conn->metric = new_metric;

                if (linkaddr_cmp(&conn->parent, &linkaddr_null))
                {
                        linkaddr_copy(&conn->parent, sender);
                        conn->parent_lock_until = now + MIN_PARENT_DWELL;
                        LOG(TAG_STAB, "new-tree adopt parent=%02u:%02u metric=%u dwell_until=%lu",
                            conn->parent.u8[0], conn->parent.u8[1], (unsigned)conn->metric, (unsigned long)conn->parent_lock_until);
                        if (TOPOLOGY_REPORT)
                        {
                                conn->treport_hold = 1;
                                ctimer_stop(&conn->treport_hold_timer);
                                ctimer_set(&conn->treport_hold_timer, TOPOLOGY_REPORT_HOLD_TIME,
                                           topology_report_hold_cb, conn);
                        }
                }
                else if (new_metric < old_metric)
                {
                        /* Only accept improved hops if link PRR is decent */
                        uint8_t prr_sender = prr_percent(sender);
                        if (prr_sender < PRR_IMPROVE_MIN && !linkaddr_cmp(&conn->parent, &linkaddr_null) && !parent_stale)
                        {
                                LOG(TAG_STAB, "improve-hop blocked: sender prr=%u < min=%u (keep %02u:%02u)",
                                    (unsigned)prr_sender, (unsigned)PRR_IMPROVE_MIN, conn->parent.u8[0], conn->parent.u8[1]);
                        }
                        else if (!linkaddr_cmp(&conn->parent, sender))
                        {
                                linkaddr_copy(&conn->parent, sender);
                                conn->parent_lock_until = now + MIN_PARENT_DWELL;
                                LOG(TAG_COLLECT, "parent set (new tree) to %02u:%02u (metric=%u)",
                                    conn->parent.u8[0], conn->parent.u8[1], (unsigned)conn->metric);
                                LOG(TAG_STAB, "dwell set until %lu (improved hops on new-tree)", (unsigned long)conn->parent_lock_until);
                                if (TOPOLOGY_REPORT)
                                {
                                        conn->treport_hold = 1;
                                        ctimer_stop(&conn->treport_hold_timer);
                                        ctimer_set(&conn->treport_hold_timer, TOPOLOGY_REPORT_HOLD_TIME,
                                                   topology_report_hold_cb, conn);
                                }
                        }
                }
                else if (parent_stale)
                {
                        /* Parent is stale: adopt sender even if hops not better (we already passed RSSI filter) */
                        if (!linkaddr_cmp(&conn->parent, sender))
                        {
                                linkaddr_copy(&conn->parent, sender);
                                conn->parent_lock_until = now + MIN_PARENT_DWELL;
                                LOG(TAG_COLLECT, "parent set (new tree, stale) to %02u:%02u (metric=%u)",
                                    conn->parent.u8[0], conn->parent.u8[1], (unsigned)conn->metric);
                                LOG(TAG_STAB, "parent stale -> adopt sender; dwell until %lu", (unsigned long)conn->parent_lock_until);
                                if (TOPOLOGY_REPORT)
                                {
                                        conn->treport_hold = 1;
                                        ctimer_stop(&conn->treport_hold_timer);
                                        ctimer_set(&conn->treport_hold_timer, TOPOLOGY_REPORT_HOLD_TIME,
                                                   topology_report_hold_cb, conn);
                                }
                        }
                }
                else
                {
                        LOG(TAG_STAB, "new-tree keep parent=%02u:%02u my_metric=%u sender_hops=%u",
                            conn->parent.u8[0], conn->parent.u8[1], (unsigned)conn->metric, (unsigned)new_metric);
                }
        }
        else
        {
                if (new_metric < conn->metric)
                {
                        /* strictly better hops: require minimal PRR to avoid flapping */
                        uint8_t prr_sender = prr_percent(sender);
                        if (prr_sender < PRR_IMPROVE_MIN && !linkaddr_cmp(&conn->parent, &linkaddr_null) && !parent_stale)
                        {
                                LOG(TAG_STAB, "improve-hop blocked: sender prr=%u < min=%u (keep %02u:%02u)",
                                    (unsigned)prr_sender, (unsigned)PRR_IMPROVE_MIN, conn->parent.u8[0], conn->parent.u8[1]);
                        }
                        else
                        {
                                conn->metric = new_metric;
                                if (!linkaddr_cmp(&conn->parent, sender))
                                {
                                        linkaddr_copy(&conn->parent, sender);
                                        LOG(TAG_COLLECT, "parent set to %02u:%02u (new_metric=%u)",
                                            conn->parent.u8[0], conn->parent.u8[1], (unsigned)conn->metric);
                                        conn->parent_lock_until = now + MIN_PARENT_DWELL;
                                        LOG(TAG_STAB, "dwell set until %lu (better hops)", (unsigned long)conn->parent_lock_until);
                                }
                        }
                }
                else if (new_metric == conn->metric)
                {
                        /* tie-break by PRR with hysteresis */
                        uint8_t prr_sender = prr_percent(sender);
                        uint8_t prr_parent = prr_percent(&conn->parent);
                        if (!linkaddr_cmp(&conn->parent, &linkaddr_null) && now < conn->parent_lock_until && !parent_stale)
                        {
                                LOG(TAG_STAB, "dwell active: keep parent until %lu (prr_parent=%u prr_sender=%u)",
                                    (unsigned long)conn->parent_lock_until, (unsigned)prr_parent, (unsigned)prr_sender);
                        }
                        else if (prr_sender < PRR_ABS_MIN)
                        {
                                LOG(TAG_STAB, "tie: sender prr=%u < abs_min=%u; keep parent", (unsigned)prr_sender, (unsigned)PRR_ABS_MIN);
                        }
                        else if (linkaddr_cmp(&conn->parent, &linkaddr_null) || (prr_sender >= (uint8_t)(prr_parent + PRR_HYSTERESIS)))
                        {
                                if (!linkaddr_cmp(&conn->parent, sender))
                                {
                                        linkaddr_copy(&conn->parent, sender);
                                        LOG(TAG_COLLECT, "parent tie-break to %02u:%02u (metric=%u prr_parent=%u prr_sender=%u)",
                                            conn->parent.u8[0], conn->parent.u8[1], (unsigned)conn->metric, (unsigned)prr_parent, (unsigned)prr_sender);
                                        conn->parent_lock_until = now + MIN_PARENT_DWELL;
                                        LOG(TAG_STAB, "dwell set until %lu (tie-break)", (unsigned long)conn->parent_lock_until);
                                }
                        }
                        else
                        {
                                LOG(TAG_COLLECT, "keep parent (tie) my_metric=%u prr_parent=%u prr_sender=%u",
                                    (unsigned)conn->metric, (unsigned)prr_parent, (unsigned)prr_sender);
                        }
                }
                else
                {
                        LOG(TAG_COLLECT, "ignore beacon (worse hops: my=%u, neigh+1=%u)", (unsigned)conn->metric, (unsigned)new_metric);
                        return;
                }

                if (TOPOLOGY_REPORT)
                {
                        conn->treport_hold = 1;
                        ctimer_stop(&conn->treport_hold_timer);
                        ctimer_set(&conn->treport_hold_timer, TOPOLOGY_REPORT_HOLD_TIME,
                                   topology_report_hold_cb, conn);
                }
        }

        /* forward beacon after a small random delay */
        ctimer_set(&conn->beacon_timer, BEACON_FORWARD_DELAY, beacon_timer_cb, conn);
        LOG(TAG_COLLECT, "schedule beacon forward after %u ticks", (unsigned)BEACON_FORWARD_DELAY);
}

/* ------------------------------------ Send / Receive ------------------------------------ */

/**
 * @brief Sends UL (uplink) data from a NODE to its parent.
 * @param conn The collect connection structure.
 * @return Non-zero on success; 0 if there is no parent.
 * @note Can piggyback (node, parent) information for the SINK to learn the topology.
 */
int my_collect_send(struct my_collect_conn *conn)
{
        struct upward_data_packet_header hdr = {
            .source = linkaddr_node_addr,
            .hops = 0,
            .piggy_len = (PIGGYBACKING == 1) ? 1 : 0};
        enum packet_type pt = upward_data_packet;

        if (linkaddr_cmp(&conn->parent, &linkaddr_null))
        {
                LOG(TAG_UL, "drop (no parent)");
                return 0; /* no parent */
        }

        if (PIGGYBACKING == 1)
        {
                tree_connection tc = {.node = linkaddr_node_addr, .parent = conn->parent};
                tc.node.u8[1] = 0x00;
                tc.parent.u8[1] = 0x00;

                srdcp_piggy_neighbor_item nei_items[SRDCP_PIGGY_MAX_NEIGHBORS];
                uint8_t neighbor_count = piggy_collect_neighbors(nei_items, SRDCP_PIGGY_MAX_NEIGHBORS);

                uint8_t queue_load = srdcp_app_queue_load_percent();
                if (queue_load > 100)
                        queue_load = 100;

                if (neighbor_count > 0)
                {
                        uint8_t i;
                        for (i = 0; i < neighbor_count; i++)
                        {
                                nei_items[i].load = queue_load;
                        }
                }

                srdcp_node_status status = {
                    .node = linkaddr_node_addr,
                    .battery_mv = srdcp_app_battery_mv(),
                    .queue_load = queue_load,
                    .metric = (uint8_t)((conn->metric <= 0xFF) ? conn->metric : 0xFF),
                    .ul_delay = srdcp_app_last_ul_delay_ticks(),
                    .dl_delay = srdcp_app_last_dl_delay_ticks(),
                    .flags = 0};

                size_t neighbor_payload_len = 0;
                if (neighbor_count > 0)
                {
                        neighbor_payload_len = sizeof(linkaddr_t) + sizeof(uint8_t) + sizeof(uint8_t) +
                                               neighbor_count * sizeof(srdcp_piggy_neighbor_item);
                }
                const size_t status_payload_len = sizeof(srdcp_node_status);

                size_t tlv_total = 0;
                if (neighbor_payload_len > 0)
                        tlv_total += sizeof(srdcp_piggy_tlv) + neighbor_payload_len;
                tlv_total += sizeof(srdcp_piggy_tlv) + status_payload_len;

                const size_t header_total =
                    sizeof(enum packet_type) + sizeof(upward_data_packet_header) + sizeof(tree_connection) + tlv_total;

                if (!packetbuf_hdralloc((int)header_total))
                {
                        LOG(TAG_PIGGY, "drop (hdralloc fail) header_total=%u", (unsigned)header_total);
                        return 0;
                }

                uint8_t *ptr = packetbuf_hdrptr();
                memcpy(ptr, &pt, sizeof(enum packet_type));
                ptr += sizeof(enum packet_type);
                memcpy(ptr, &hdr, sizeof(upward_data_packet_header));
                ptr += sizeof(upward_data_packet_header);
                memcpy(ptr, &tc, sizeof(tree_connection));
                ptr += sizeof(tree_connection);

                if (neighbor_payload_len > 0)
                {
                        srdcp_piggy_tlv tlv = {.type = SRDCP_PIGGY_TLV_NEIGHBORS, .length = (uint8_t)neighbor_payload_len};
                        memcpy(ptr, &tlv, sizeof(tlv));
                        ptr += sizeof(tlv);

                        memcpy(ptr, &linkaddr_node_addr, sizeof(linkaddr_t));
                        ptr += sizeof(linkaddr_t);
                        *ptr++ = neighbor_count;
                        *ptr++ = queue_load;
                        LOG(TAG_PIGGY, "UL piggy neighbors count=%u queue=%u",
                            (unsigned)neighbor_count, (unsigned)queue_load);
                        uint8_t i;
                        for (i = 0; i < neighbor_count; i++)
                        {
                                memcpy(ptr, &nei_items[i], sizeof(srdcp_piggy_neighbor_item));
                                ptr += sizeof(srdcp_piggy_neighbor_item);
                        }
                }

                {
                        srdcp_piggy_tlv tlv = {.type = SRDCP_PIGGY_TLV_STATUS, .length = (uint8_t)status_payload_len};
                        memcpy(ptr, &tlv, sizeof(tlv));
                        ptr += sizeof(tlv);
                        memcpy(ptr, &status, sizeof(srdcp_node_status));
                        ptr += sizeof(srdcp_node_status);
                        LOG(TAG_PIGGY, "UL piggy status batt=%u queue=%u metric=%u",
                            (unsigned)status.battery_mv, (unsigned)status.queue_load,
                            (unsigned)status.metric);
                }
        }
        else
        {
                packetbuf_hdralloc(sizeof(enum packet_type) +
                                   sizeof(upward_data_packet_header));
                memcpy(packetbuf_hdrptr(), &pt, sizeof(enum packet_type));
                memcpy(packetbuf_hdrptr() + sizeof(enum packet_type),
                       &hdr, sizeof(upward_data_packet_header));
        }
        return unicast_send(&conn->uc, &conn->parent);
}

/**
 * @brief Sends DL (downlink) via SRDCP (source routing) from the SINK to a destination.
 * @param conn The collect connection structure (at the SINK).
 * @param dest The destination address.
 * @return Non-zero on success; 0 if no route is found.
 * @details Finds a route using the parent dictionary at the SINK and packs the full path into the header.
 */
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
        downward_data_packet_header hdr = {.hops = 0, .path_len = path_len};

        packetbuf_hdralloc(sizeof(enum packet_type) +
                           sizeof(downward_data_packet_header) +
                           sizeof(linkaddr_t) * path_len);
        memcpy(packetbuf_hdrptr(), &pt, sizeof(enum packet_type));
        memcpy(packetbuf_hdrptr() + sizeof(enum packet_type),
               &hdr, sizeof(downward_data_packet_header));

        int i;
        for (i = path_len - 1; i >= 0; i--)
        {
                memcpy(packetbuf_hdrptr() + sizeof(enum packet_type) +
                           sizeof(downward_data_packet_header) +
                           sizeof(linkaddr_t) * (path_len - (i + 1)),
                       &conn->routing_table.tree_path[i],
                       sizeof(linkaddr_t));
        }
        return unicast_send(&conn->uc, &conn->routing_table.tree_path[path_len - 1]);
}

/**
 * @brief Handles an incoming unicast packet based on its SRDCP payload type.
 * @param uc_conn Pointer to the unicast connection.
 * @param sender  The sender's address.
 * @details upward_data_packet: forward upwards (or deliver to app at SINK).
 *          topology_report: SINK updates its dictionary; NODE forwards to parent.
 *          downward_data_packet: execute source routing downwards.
 */
void uc_recv(struct unicast_conn *uc_conn, const linkaddr_t *sender)
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
                                send_topology_report(conn, 1); /* forwarding */
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

static uint8_t piggy_collect_neighbors(srdcp_piggy_neighbor_item *items, uint8_t max_items)
{
        if (max_items == 0)
                return 0;

        prr_entry_t *candidates[PRR_NEI_MAX];
        uint8_t cand_count = 0;
        clock_time_t now = clock_time();

        memset(candidates, 0, sizeof(candidates));

        uint8_t i;
        for (i = 0; i < PRR_NEI_MAX; i++)
        {
                if (!prr_tab[i].used)
                        continue;
                if ((now - prr_tab[i].last_seen) > SRDCP_NEIGHBOR_STALE_TICKS)
                        continue;
                candidates[cand_count++] = &prr_tab[i];
                if (cand_count == PRR_NEI_MAX)
                        break;
        }

        uint8_t produced = 0;
        while (produced < max_items)
        {
                int best = -1;
                uint8_t best_prr = 0;
                int8_t best_rssi = -128;

                uint8_t idx;
                for (idx = 0; idx < cand_count; idx++)
                {
                        prr_entry_t *cand = candidates[idx];
                        if (!cand)
                                continue;
                        uint8_t prr = prr_percent(&cand->addr);
                        if (prr == 0)
                                continue;
                        if (best < 0 || prr > best_prr || (prr == best_prr && cand->last_rssi > best_rssi))
                        {
                                best = idx;
                                best_prr = prr;
                                best_rssi = cand->last_rssi;
                        }
                }

                if (best < 0)
                        break;

                prr_entry_t *sel = candidates[best];
                items[produced].neighbor = sel->addr;
                items[produced].rssi = sel->last_rssi;
                items[produced].prr = best_prr;
                items[produced].metric = (uint8_t)((sel->last_metric <= 0xFF) ? sel->last_metric : 0xFF);
                items[produced].load = 0; /* filled by caller */
                candidates[best] = NULL;
                produced++;
        }

        return produced;
}

static srdcp_graph_node *graph_lookup_or_create(srdcp_graph_state *graph, const linkaddr_t *node)
{
        int free_idx = -1;
        int i;
        for (i = 0; i < MAX_NODES; i++)
        {
                if (graph->nodes[i].used && linkaddr_cmp(&graph->nodes[i].node, node))
                        return &graph->nodes[i];
                if (!graph->nodes[i].used && free_idx < 0)
                        free_idx = i;
        }
        if (free_idx < 0)
                return NULL;
        srdcp_graph_node *entry = &graph->nodes[free_idx];
        memset(entry, 0, sizeof(*entry));
        entry->used = 1;
        linkaddr_copy(&entry->node, node);
        entry->neighbor_count = 0;
        entry->status_last_update = 0;
        return entry;
}

static void graph_update_neighbors(struct my_collect_conn *conn, const linkaddr_t *owner,
                                   const srdcp_piggy_neighbor_item *items, uint8_t count,
                                   uint8_t queue_load)
{
        if (!conn->is_sink)
                return;
        srdcp_graph_node *node = graph_lookup_or_create(&conn->graph, owner);
        if (!node)
                return;

        uint8_t capped = count;
        if (capped > SRDCP_GRAPH_MAX_NEIGHBORS)
                capped = SRDCP_GRAPH_MAX_NEIGHBORS;
        node->neighbor_count = capped;
        clock_time_t now = clock_time();

        uint8_t i;
        for (i = 0; i < capped; i++)
        {
                (void)graph_lookup_or_create(&conn->graph, &items[i].neighbor);
                node->neighbors[i].neighbor = items[i].neighbor;
                node->neighbors[i].rssi = items[i].rssi;
                node->neighbors[i].prr = items[i].prr;
                node->neighbors[i].metric = items[i].metric;
                node->neighbors[i].load = (items[i].load > 0) ? items[i].load : queue_load;
                node->neighbors[i].last_update = now;
        }
        for (; i < SRDCP_GRAPH_MAX_NEIGHBORS; i++)
        {
                memset(&node->neighbors[i], 0, sizeof(node->neighbors[i]));
                node->neighbors[i].neighbor = linkaddr_null;
        }

        LOG(TAG_GRAPH, "nei update owner=%02u:%02u count=%u",
            owner->u8[0], owner->u8[1], (unsigned)capped);
}

static void graph_update_status(struct my_collect_conn *conn, const srdcp_node_status *status)
{
        if (!conn->is_sink)
                return;
        srdcp_graph_node *node = graph_lookup_or_create(&conn->graph, &status->node);
        if (!node)
                return;

        node->status = *status;
        node->status_last_update = clock_time();

        LOG(TAG_GRAPH, "status update node=%02u:%02u batt=%u queue=%u metric=%u",
            status->node.u8[0], status->node.u8[1],
            (unsigned)status->battery_mv, (unsigned)status->queue_load,
            (unsigned)status->metric);
}

/**
 * @brief Checks if a node address is already in the piggyback block.
 * @param piggy_len The number of piggyback entries currently in the packet.
 * @param node      The node address to check.
 * @return true if it already exists; false otherwise.
 */
bool check_address_in_piggyback_block(uint8_t piggy_len, linkaddr_t node)
{
        printf("Checking piggy address: %02u:%02u\n", node.u8[0], node.u8[1]);
        uint8_t i;
        tree_connection tc;
        for (i = 0; i < piggy_len; i++)
        {
                memcpy(&tc,                                                                                                              // NOLINT(bugprone-sizeof-expression)
                       packetbuf_dataptr() + sizeof(enum packet_type) + sizeof(upward_data_packet_header) + sizeof(tree_connection) * i, /* correct offset by i */
                       sizeof(tree_connection));
                /* Sanitize high byte for stable comparison */
                tc.node.u8[1] = 0x00;
                if (linkaddr_cmp(&tc.node, &node))
                {
                        printf("ERROR: Checking piggy address found: %02u:%02u\n", node.u8[0], node.u8[1]);
                        return true;
                }
        }
        return false;
}

/**
 * @brief Processes an incoming UL packet and forwards it to the parent.
 * @param conn   The collect connection structure.
 * @param sender The sender's address (ignored for internal processing).
 * @details SINK: strips the header, applies piggybacked info, and delivers the payload to the app.
 *          NODE: increments hops; may insert its (node, parent) info into the piggyback block if valid.
 */
void forward_upward_data(struct my_collect_conn *conn, const linkaddr_t *sender)
{
        (void)sender;
        upward_data_packet_header hdr;
        memcpy(&hdr, packetbuf_dataptr() + sizeof(enum packet_type),
               sizeof(upward_data_packet_header));

        if (conn->is_sink == 1)
        {
                size_t base_hdr = sizeof(enum packet_type) + sizeof(upward_data_packet_header);
                size_t tc_bytes = (size_t)hdr.piggy_len * sizeof(tree_connection);
                uint8_t *data_ptr = (uint8_t *)packetbuf_dataptr();
                size_t datalen = packetbuf_datalen();
                LOG(TAG_PIGGY, "sink pre-strip datalen=%u base=%u tc=%u piggy_len=%u",
                    (unsigned)datalen, (unsigned)base_hdr, (unsigned)tc_bytes, (unsigned)hdr.piggy_len);

                if (PIGGYBACKING == 1 && datalen >= base_hdr)
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
                        uint8_t *tc_ptr = data_ptr + base_hdr;
                        for (i = 0; i < hdr.piggy_len; i++)
                        {
                                if ((size_t)(base_hdr + (i + 1) * sizeof(tree_connection)) > datalen)
                                        break;
                                memcpy(&tc, tc_ptr + sizeof(tree_connection) * i, sizeof(tree_connection));
                                tc.node.u8[1] = 0x00;
                                tc.parent.u8[1] = 0x00;
                                if (tc.node.u8[0] != 0 && tc.parent.u8[0] != 0)
                                {
                                        dict_add(&conn->routing_table, tc.node, tc.parent);
                                }
                        }
                }

                size_t consumed = base_hdr + tc_bytes;
                size_t remaining = (datalen > consumed) ? (datalen - consumed) : 0;

                size_t ctrl_bytes = 0;
                if (remaining >= sizeof(srdcp_piggy_tlv))
                {
                        uint8_t *tlv_ptr = data_ptr + consumed;
                        uint8_t parsed = 0;
                        while (remaining >= sizeof(srdcp_piggy_tlv) && parsed < 2)
                        {
                                srdcp_piggy_tlv tlv;
                                memcpy(&tlv, tlv_ptr, sizeof(tlv));
                                size_t total_len = sizeof(tlv) + tlv.length;
                                if (tlv.length > (remaining - sizeof(tlv)))
                                {
                                        LOG(TAG_PIGGY, "skip TLV type=%u truncated len=%u rem=%u",
                                            (unsigned)tlv.type, (unsigned)tlv.length, (unsigned)remaining);
                                        break;
                                }
                                if (tlv.type == SRDCP_PIGGY_TLV_NEIGHBORS)
                                {
                                        if (tlv.length >= (sizeof(linkaddr_t) + 2))
                                        {
                                                const uint8_t *payload = tlv_ptr + sizeof(tlv);
                                                linkaddr_t owner;
                                                memcpy(&owner, payload, sizeof(linkaddr_t));
                                                owner.u8[1] = 0x00;
                                                uint8_t count = *(payload + sizeof(linkaddr_t));
                                                uint8_t queue_load = *(payload + sizeof(linkaddr_t) + 1);
                                                size_t base = sizeof(linkaddr_t) + 2;
                                                size_t avail_items = (tlv.length - base) / sizeof(srdcp_piggy_neighbor_item);
                                                if (count > avail_items)
                                                        count = (uint8_t)avail_items;
                                                if (count > 0)
                                                {
                                                        srdcp_piggy_neighbor_item buf[SRDCP_PIGGY_MAX_NEIGHBORS];
                                                        if (count > SRDCP_PIGGY_MAX_NEIGHBORS)
                                                                count = SRDCP_PIGGY_MAX_NEIGHBORS;
                                                        memcpy(buf, payload + base, count * sizeof(srdcp_piggy_neighbor_item));
                                                        graph_update_neighbors(conn, &owner, buf, count, queue_load);
                                                }
                                        }
                                        tlv_ptr += total_len;
                                        remaining -= total_len;
                                        ctrl_bytes += total_len;
                                        parsed++;
                                        continue;
                                }
                                if (tlv.type == SRDCP_PIGGY_TLV_STATUS)
                                {
                                        if (tlv.length == sizeof(srdcp_node_status))
                                        {
                                                const uint8_t *payload = tlv_ptr + sizeof(tlv);
                                                srdcp_node_status status;
                                                memcpy(&status, payload, sizeof(status));
                                                status.node.u8[1] = 0x00;
                                                graph_update_status(conn, &status);
                                        }
                                        tlv_ptr += total_len;
                                        remaining -= total_len;
                                        ctrl_bytes += total_len;
                                        parsed++;
                                        continue;
                                }
                                /* Unknown TLV -> stop parsing and leave payload intact */
                                break;
                        }
                }

                packetbuf_hdrreduce((int)base_hdr);
                if (tc_bytes > 0)
                        packetbuf_hdrreduce((int)tc_bytes);
                if (ctrl_bytes > 0)
                        packetbuf_hdrreduce((int)ctrl_bytes);
                LOG(TAG_PIGGY, "sink post-strip datalen=%u ctrl_bytes=%u",
                    (unsigned)packetbuf_datalen(), (unsigned)ctrl_bytes);

                conn->callbacks->recv(&hdr.source, hdr.hops + 1);
        }
        else
        {
                hdr.hops++;

                if (PIGGYBACKING == 1 && !linkaddr_cmp(&conn->parent, &linkaddr_null) &&
                    !check_address_in_piggyback_block(hdr.piggy_len, linkaddr_node_addr))
                {
                        size_t old_len = packetbuf_datalen();
                        size_t insert_offset = sizeof(enum packet_type) +
                                               sizeof(upward_data_packet_header) +
                                               (size_t)hdr.piggy_len * sizeof(tree_connection);
                        if (insert_offset <= old_len &&
                            (old_len + sizeof(tree_connection)) <= PACKETBUF_SIZE)
                        {
                                packetbuf_set_datalen(old_len + sizeof(tree_connection));
                                uint8_t *data = packetbuf_dataptr();
                                memmove(data + insert_offset + sizeof(tree_connection),
                                        data + insert_offset,
                                        old_len - insert_offset);
                                tree_connection tc = {.node = linkaddr_node_addr, .parent = conn->parent};
                                tc.node.u8[1] = 0x00;
                                tc.parent.u8[1] = 0x00;
                                memcpy(data + insert_offset, &tc, sizeof(tree_connection));
                                hdr.piggy_len = (uint8_t)(hdr.piggy_len + 1);
                        }
                        else
                        {
                                LOG(TAG_PIGGY, "cannot add tree_connection (offset=%u len=%u)",
                                    (unsigned)insert_offset, (unsigned)old_len);
                        }
                }
                memcpy(packetbuf_dataptr() + sizeof(enum packet_type), &hdr, sizeof(upward_data_packet_header));

                unicast_send(&conn->uc, &conn->parent);
        }
}

/**
 * @brief Processes a downward SRDCP (source-routed) packet.
 * @param conn   The collect connection structure.
 * @param sender The sender's address (not used).
 * @details If this is the final destination, deliver to the app. Otherwise, pop the
 *          next-hop, decrement path_len, update the header, and forward to the next hop.
 */
void forward_downward_data(struct my_collect_conn *conn, const linkaddr_t *sender)
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
                        packetbuf_hdrreduce(sizeof(enum packet_type) +
                                            sizeof(downward_data_packet_header) +
                                            sizeof(linkaddr_t));
                        conn->callbacks->sr_recv(conn, hdr.hops + 1);
                }
                else
                {
                        packetbuf_hdrreduce(sizeof(linkaddr_t));
                        hdr.path_len = hdr.path_len - 1;
                        enum packet_type pt = downward_data_packet;
                        memcpy(packetbuf_dataptr(), &pt, sizeof(enum packet_type));
                        memcpy(packetbuf_dataptr() + sizeof(enum packet_type),
                               &hdr, sizeof(downward_data_packet_header));
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

/* --------------------------------------------------------------------
 * Default weak hooks (may be overridden by applications)
 * ------------------------------------------------------------------ */

__attribute__((weak)) void srdcp_app_beacon_observed(const linkaddr_t *sender,
                                                     uint16_t metric,
                                                     int16_t rssi,
                                                     uint8_t lqi)
{
        (void)sender;
        (void)metric;
        (void)rssi;
        (void)lqi;
}

__attribute__((weak)) uint16_t srdcp_app_battery_mv(void)
{
        return 0;
}

__attribute__((weak)) uint8_t srdcp_app_queue_load_percent(void)
{
        return 0;
}

__attribute__((weak)) uint16_t srdcp_app_last_ul_delay_ticks(void)
{
        return 0;
}

__attribute__((weak)) uint16_t srdcp_app_last_dl_delay_ticks(void)
{
        return 0;
}
