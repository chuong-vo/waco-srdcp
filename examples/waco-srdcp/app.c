#include "contiki.h"
#include "lib/random.h"
#include "net/rime/rime.h"
#include "leds.h"
#include "net/netstack.h"
#include <stdio.h>
#include "core/net/linkaddr.h"
#include "my_collect.h"
/*---------------------------------------------------------------------------*/
#define APP_UPWARD_TRAFFIC 1
#define APP_DOWNWARD_TRAFFIC 1
/*---------------------------------------------------------------------------*/
#define APP_NODES 10
/*---------------------------------------------------------------------------*/
#define MSG_PERIOD (30 * CLOCK_SECOND)  // send every 30 seconds
#define SR_MSG_PERIOD (10 * CLOCK_SECOND)  // send every 10 seconds
#define COLLECT_CHANNEL 0xAA
/*---------------------------------------------------------------------------*/
static linkaddr_t sink = {{0x01, 0x00}}; // node 1 will be our sink
/*---------------------------------------------------------------------------*/
PROCESS(app_process, "App process");
AUTOSTART_PROCESSES(&app_process);
/*---------------------------------------------------------------------------*/
/* Application packet */
typedef struct {
  uint16_t seqn;
}
__attribute__((packed))
test_msg_t;
/*---------------------------------------------------------------------------*/
static struct my_collect_conn my_collect;
static void recv_cb(const linkaddr_t *originator, uint8_t hops);
/*
 * Source Routing Callback
 * This function is called upon receiving a message from the sink in a node.
 * Params:
 *  ptr: a pointer to the connection of the collection protocol
 *  hops: number of hops of the route followed by the packet to reach the destination
 */
static void sr_recv_cb(struct my_collect_conn *ptr, uint8_t hops);
/*---------------------------------------------------------------------------*/
static struct my_collect_callbacks sink_cb = {
  .recv = recv_cb,
  .sr_recv = NULL,
};
/*---------------------------------------------------------------------------*/
static struct my_collect_callbacks node_cb = {
  .recv = NULL,
  .sr_recv = sr_recv_cb,
};
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(app_process, ev, data)
{
  static struct etimer periodic;
  static struct etimer rnd;
  static test_msg_t msg = {.seqn=0};
  static uint8_t dest_low = 2;
  // static linkaddr_t dest = {{0x00, 0x00}};
  static linkaddr_t dest;
  dest.u8[0] = 0x00;
  dest.u8[1] = 0x00;
  static int ret;

  PROCESS_BEGIN();

  if(linkaddr_cmp(&sink, &linkaddr_node_addr)) {
    printf("App: I am sink %02x:%02x\n", linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
    my_collect_open(&my_collect, COLLECT_CHANNEL, true, &sink_cb);
#if APP_DOWNWARD_TRAFFIC == 1
    /* Wait a bit longer at the beginning to gather enough topology information */
    etimer_set(&periodic, 75 * CLOCK_SECOND);
    // etimer_set(&periodic, 120 * CLOCK_SECOND);
    while(1) {
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic));
      /* Fixed interval */
      etimer_set(&periodic, SR_MSG_PERIOD);
      /* Random shift within the first half of the interval */
      etimer_set(&rnd, random_rand() % (SR_MSG_PERIOD / 2));
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&rnd));

      /* Set application data packet */
      packetbuf_clear();
      memcpy(packetbuf_dataptr(), &msg, sizeof(msg));
      packetbuf_set_datalen(sizeof(msg));

      /* Change the Destination Link Address to a different node */
      dest.u8[0] = dest_low;

      /* Send the packet downwards */
      printf("App: sink sending seqn %d to %02x:%02x\n",
        msg.seqn, dest.u8[0], dest.u8[1]);
      ret = sr_send(&my_collect, &dest);

      /* Check that the packet could be sent */
      if(ret == 0) {
        printf("App: sink could not send seqn %d to %02x:%02x\n",
          msg.seqn, dest.u8[0], dest.u8[1]);
      }

      /* Update sequence number and next destination address */
      msg.seqn++;
      dest_low++;
      if(dest_low > APP_NODES) {
        dest_low = 2;
      }
    }
#endif /* APP_DOWNWARD_TRAFFIC == 1 */
  }
  else {
    printf("App: I am normal node %02x:%02x\n", linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
    my_collect_open(&my_collect, COLLECT_CHANNEL, false, &node_cb);
#if APP_UPWARD_TRAFFIC == 1
    etimer_set(&periodic, MSG_PERIOD);
    while(1) {
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic));
      /* Fixed interval */
      etimer_reset(&periodic);
      /* Random shift within the interval */
      etimer_set(&rnd, random_rand() % (MSG_PERIOD/2));
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&rnd));

      packetbuf_clear();
      memcpy(packetbuf_dataptr(), &msg, sizeof(msg));
      packetbuf_set_datalen(sizeof(msg));
      printf("App: Send seqn %d\n", msg.seqn);
      my_collect_send(&my_collect);
      msg.seqn ++;
    }
#endif /* APP_UPWARD_TRAFFIC == 1 */
  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
static void recv_cb(const linkaddr_t *originator, uint8_t hops) {
  test_msg_t msg;
  if (packetbuf_datalen() != sizeof(msg)) {
    printf("App: wrong length: %d\n", packetbuf_datalen());
    return;
  }
  memcpy(&msg, packetbuf_dataptr(), sizeof(msg));
  printf("App: Recv from %02x:%02x seqn %u hops %u\n",
    originator->u8[0], originator->u8[1], msg.seqn, hops);
}
/*---------------------------------------------------------------------------*/
static void sr_recv_cb(struct my_collect_conn *ptr, uint8_t hops)
{
  test_msg_t sr_msg;
  if (packetbuf_datalen() != sizeof(test_msg_t)) {
    printf("App: sr_recv wrong length: %d\n", packetbuf_datalen());
    return;
  }
  memcpy(&sr_msg, packetbuf_dataptr(), sizeof(test_msg_t));
  printf("App: sr_recv from sink seqn %u hops %u node metric %u\n",
    sr_msg.seqn, hops, ptr->metric);
}
/*---------------------------------------------------------------------------*/
