#ifndef MY_COLLECT_H
#define MY_COLLECT_H

#include <stdbool.h>
#include <stdlib.h>
#include "contiki.h"
#include "net/rime/rime.h"
#include "net/netstack.h"
#include "core/net/linkaddr.h"

// Allow or not to send topology reports.
#define TOPOLOGY_REPORT 1
#define PIGGYBACKING 1

#define MAX_NODES 30
#define MAX_PATH_LENGTH 10

#define BEACON_INTERVAL (CLOCK_SECOND * 10)
#define BEACON_FORWARD_DELAY (random_rand() % (CLOCK_SECOND * 4))
// Used for topology reports
#define TOPOLOGY_REPORT_HOLD_TIME (CLOCK_SECOND * 15)

#define RSSI_THRESHOLD -95
#define MAX_RETRANSMISSIONS 1

static const linkaddr_t sink_addr = {{0x01, 0x00}}; // node 1 will be our sink

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
        // true if this node is the sink
        uint8_t is_sink; // 1: is_sink, 0: not_sink
        // tree table (used only in the sink)
        TreeDict routing_table;

        // 1: Wait to send topology report (may be able to append to incoming t-report)
        // 0: Send topology report right away
        uint8_t treport_hold;
        struct ctimer treport_hold_timer;
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

#endif // MY_COLLECT_H
