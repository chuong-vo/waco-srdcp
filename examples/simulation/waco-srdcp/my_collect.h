#ifndef MY_COLLECT_H
#define MY_COLLECT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "contiki.h"
#include "net/rime/rime.h"
#include "net/netstack.h"
#include "core/net/linkaddr.h"

// Allow or not to send topology reports.
#ifndef TOPOLOGY_REPORT
#define TOPOLOGY_REPORT 1
#endif
#ifndef PIGGYBACKING
#define PIGGYBACKING 1
#endif

#define MAX_NODES 30
/* Fast-convergence profile: allow long paths for 30-node chains */
#define MAX_PATH_LENGTH 32

/* Piggyback TLV identifiers */
#define SRDCP_PIGGY_TLV_NEIGHBORS 1
#define SRDCP_PIGGY_TLV_STATUS 2

/* Limit number of neighbor samples piggybacked per UL */
#ifndef SRDCP_PIGGY_MAX_NEIGHBORS
#define SRDCP_PIGGY_MAX_NEIGHBORS 3
#endif

/* Graph storage at sink */
#ifndef SRDCP_GRAPH_MAX_NEIGHBORS
#define SRDCP_GRAPH_MAX_NEIGHBORS 4
#endif

/* Edge cost weights (hop dominates, then PRR, then load) */
#ifndef SRDCP_GRAPH_HOP_WEIGHT
#define SRDCP_GRAPH_HOP_WEIGHT 1000
#endif

/* Max bytes reserved for piggyback control block */
#ifndef SRDCP_PIGGY_CTRL_MAX
#define SRDCP_PIGGY_CTRL_MAX 96
#endif

#ifndef BEACON_INTERVAL
/* Fast-convergence: more frequent beacons */
#define BEACON_INTERVAL (8 * CLOCK_SECOND)
#endif
/* Max random forward delay = BEACON_FWD_JITTER_TICKS */
#ifndef BEACON_FWD_JITTER_TICKS
#define BEACON_FWD_JITTER_TICKS CLOCK_SECOND / 2
#endif
#ifndef BEACON_FORWARD_DELAY
#define BEACON_FORWARD_DELAY (random_rand() % BEACON_FWD_JITTER_TICKS)
#endif
// Used for topology reports
#ifndef TOPOLOGY_REPORT_HOLD_TIME
/* Fast-convergence: shorter hold to still piggyback but update quickly */
#define TOPOLOGY_REPORT_HOLD_TIME (CLOCK_SECOND * 5)
#endif

#ifndef RSSI_THRESHOLD
#define RSSI_THRESHOLD -95
#endif
#ifndef MAX_RETRANSMISSIONS
#define MAX_RETRANSMISSIONS 1
#endif

#ifndef SRDCP_INFO_MAX_AGE
#define SRDCP_INFO_MAX_AGE (5 * BEACON_INTERVAL)
#endif

// static const linkaddr_t sink_addr = {{0x01, 0x00 } }; // node 1 will be our sink
extern const linkaddr_t sink_addr;
enum packet_type
{
        upward_data_packet = 0,
        downward_data_packet = 1,
        topology_report = 2
};

// --------------------------------------------------------------------
//                              DICT STRUCTS
// --------------------------------------------------------------------

typedef struct DictEntry
{
        linkaddr_t key;   // the address of the node
        linkaddr_t value; // the address of the parent
} DictEntry;

typedef struct TreeDict
{
        int len;
        DictEntry entries[MAX_NODES];
        linkaddr_t tree_path[MAX_PATH_LENGTH];
} TreeDict;

/* ---- Piggyback TLV payloads ---- */

struct srdcp_piggy_tlv
{
        uint8_t type;
        uint8_t length;
} __attribute__((packed));
typedef struct srdcp_piggy_tlv srdcp_piggy_tlv;

struct srdcp_piggy_neighbor_item
{
        linkaddr_t neighbor;
        int8_t rssi;
        uint8_t prr;
        uint8_t metric;
        uint8_t load;
} __attribute__((packed));
typedef struct srdcp_piggy_neighbor_item srdcp_piggy_neighbor_item;

struct srdcp_piggy_neighbors_payload
{
        linkaddr_t owner;
        uint8_t count;
        uint8_t queue_load;
        srdcp_piggy_neighbor_item items[];
} __attribute__((packed));
typedef struct srdcp_piggy_neighbors_payload srdcp_piggy_neighbors_payload;

struct srdcp_node_status
{
        linkaddr_t node;
        uint16_t battery_mv;
        uint8_t queue_load;
        uint8_t metric;
        uint16_t ul_delay;
        uint16_t dl_delay;
        uint8_t flags;
} __attribute__((packed));
typedef struct srdcp_node_status srdcp_node_status;

typedef struct
{
        linkaddr_t neighbor;
        int8_t rssi;
        uint8_t prr;
        uint8_t metric;
        uint8_t load;
        clock_time_t last_update;
} srdcp_graph_edge;

typedef struct
{
        uint8_t used;
        linkaddr_t node;
        srdcp_node_status status;
        clock_time_t status_last_update;
        srdcp_graph_edge neighbors[SRDCP_GRAPH_MAX_NEIGHBORS];
        uint8_t neighbor_count;
} srdcp_graph_node;

typedef struct
{
        srdcp_graph_node nodes[MAX_NODES];
} srdcp_graph_state;

// --------------------------------------------------------------------

/* Connection object */
struct my_collect_conn
{
        // broadcast connection object
        struct broadcast_conn bc;
        // unicast connection object
        struct unicast_conn uc;
        const struct my_collect_callbacks *callbacks;
        // address of parent node
        linkaddr_t parent;
        struct ctimer beacon_timer;
        // metric: hop count (0 if sink)
        uint16_t metric;
        // sequence number of the tree protocol
        uint16_t beacon_seqn;
        // per-sender beacon sequence (for PRR estimation at neighbors)
        uint16_t beacon_tx_seq;
        // true if this node is the sink
        uint8_t is_sink; // 1: is_sink, 0: not_sink
        // tree table (used only in the sink)
        TreeDict routing_table;
        // graph/telemetry state (sink only)
        srdcp_graph_state graph;

        // 1: Wait to send topology report (may be able to append to incoming t-report)
        // 0: Send topology report right away
        uint8_t treport_hold;
        struct ctimer treport_hold_timer;

        /* Stabilization: do not switch parent during dwell window */
        clock_time_t parent_lock_until;
};
typedef struct my_collect_conn my_collect_conn;

struct my_collect_callbacks
{
        void (*recv)(const linkaddr_t *originator, uint8_t hops);
        void (*sr_recv)(struct my_collect_conn *ptr, uint8_t hops);
};

/* Initialize a collect connection
 *  - conn -- a pointer to a connection object
 *  - channels -- starting channel C (the collect uses two: C and C+1)
 *  - is_sink -- initialize in either sink or router mode
 *  - callbacks -- a pointer to the callback structure */
void my_collect_open(struct my_collect_conn *, uint16_t, bool, const struct my_collect_callbacks *);

// -------- COMMUNICATION FUNCTIONS --------

int my_collect_send(struct my_collect_conn *c);
void bc_recv(struct broadcast_conn *conn, const linkaddr_t *sender);
void uc_recv(struct unicast_conn *c, const linkaddr_t *from);
void send_beacon(struct my_collect_conn *);
void send_topology_report(my_collect_conn *, uint8_t);
void forward_upward_data(my_collect_conn *conn, const linkaddr_t *sender);
void forward_downward_data(my_collect_conn *, const linkaddr_t *);

/*
   Source routing send function:
   Params:
    c: pointer to the collection connection structure
    dest: pointer to the destination address
   Returns non-zero if the packet could be sent, zero otherwise.
 */
int sr_send(struct my_collect_conn *, const linkaddr_t *);

void beacon_timer_cb(void *ptr);

/* ---- Telemetry helpers (for apps) ---- */
/* Return integer PRR percent (0..100) observed for a neighbor based on beacons. */
uint8_t my_collect_prr_percent(const linkaddr_t *addr);

// -------- MESSAGE STRUCTURES --------

struct tree_connection
{
        linkaddr_t node;
        linkaddr_t parent;
} __attribute__((packed));
typedef struct tree_connection tree_connection;

// Beacon message structure
struct beacon_msg
{
        uint16_t seqn;
        /* Per-sender beacon counter to estimate PRR on neighbors */
        uint16_t tx_seq;
        uint16_t metric;
} __attribute__((packed));
typedef struct beacon_msg beacon_msg;

struct upward_data_packet_header
{ // Header structure for data packets
        linkaddr_t source;
        uint8_t hops;
        uint8_t piggy_len; // 0 in case there is no piggybacking
} __attribute__((packed));
typedef struct upward_data_packet_header upward_data_packet_header;

struct downward_data_packet_header
{
        uint8_t hops;
        uint8_t path_len;
} __attribute__((packed));
typedef struct downward_data_packet_header downward_data_packet_header;

/* --------------------------------------------------------------------
 * Application hook: notify app that a beacon was observed
 *  - sender: neighbor address who sent the beacon
 *  - metric: neighbor's hop-count to sink (0 for sink itself)
 *  - rssi, lqi: link quality observed for this beacon
 *
 * NOTE: This does not change SRDCP logic; it's for telemetry only.
 * The app may ignore this by not implementing the symbol; a weak
 * default definition is provided in my_collect.c.
 * ------------------------------------------------------------------ */

__attribute__((weak)) void srdcp_app_beacon_observed(const linkaddr_t *sender,
                                                     uint16_t metric,
                                                     int16_t rssi,
                                                     uint8_t lqi);

__attribute__((weak)) uint16_t srdcp_app_battery_mv(void);
__attribute__((weak)) uint8_t srdcp_app_queue_load_percent(void);
__attribute__((weak)) uint16_t srdcp_app_last_ul_delay_ticks(void);
__attribute__((weak)) uint16_t srdcp_app_last_dl_delay_ticks(void);

#endif // MY_COLLECT_H
