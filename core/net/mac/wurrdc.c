/*
 * Wake-up Radio RDC layer implementation that uses framer for headers +
 * CSMA MAC layer
 * \authors
 *         Timofei Istomin <timofei.istomin@unitn.it>
 *         Rajeev Piyare   <piyare@fbk.eu>
 */

#include "net/mac/mac-sequence.h"
#include "net/mac/wurrdc.h"
#include "net/packetbuf.h"
#include "net/queuebuf.h"
#include "net/netstack.h"
#include "wur.h"
#include "net/rime/rimestats.h"
#include "dev/leds.h"
#include "sys/rtimer.h"
#include "clock.h" /* for clock_delay() */
// #include "dev/sensors.h" /* SENSORS_ACTIVATE(), sensors_event */

#include <string.h>
#include <stdio.h>

#if CONTIKI_TARGET_COOJA
#include "lib/simEnvChange.h"
#endif /* CONTIKI_TARGET_COOJA */

/* ===== Friendly logging toggle (no behavior change) ===== */
#ifndef LOG_WUR
#define LOG_WUR 0 /* 1: enable friendly logs; 0: disable */
#endif

#if LOG_WUR
#define WUR_LOG(...) printf(__VA_ARGS__)
#else
#define WUR_LOG(...)
#endif

static inline void addr_print(const linkaddr_t *a)
{
  WUR_LOG("%02x:%02x", a->u8[0], a->u8[1]);
}

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

#ifdef WURRDC_CONF_ADDRESS_FILTER
#define WURRDC_ADDRESS_FILTER WURRDC_CONF_ADDRESS_FILTER
#else
#define WURRDC_ADDRESS_FILTER 1
#endif /* WURRDC_CONF_ADDRESS_FILTER */

#ifndef WURRDC_802154_AUTOACK
#ifdef WURRDC_CONF_802154_AUTOACK
#define WURRDC_802154_AUTOACK WURRDC_CONF_802154_AUTOACK
#else
#define WURRDC_802154_AUTOACK 0
#endif /* WURRDC_CONF_802154_AUTOACK */
#endif /* WURRDC_802154_AUTOACK */

#ifndef WURRDC_802154_AUTOACK_HW
#ifdef WURRDC_CONF_802154_AUTOACK_HW
#define WURRDC_802154_AUTOACK_HW WURRDC_CONF_802154_AUTOACK_HW
#else
#define WURRDC_802154_AUTOACK_HW 0
#endif /* WURRDC_CONF_802154_AUTOACK_HW */
#endif /* WURRDC_802154_AUTOACK_HW */

#if WURRDC_802154_AUTOACK
#include "dev/watchdog.h"

#ifdef WURRDC_CONF_ACK_WAIT_TIME
#define ACK_WAIT_TIME WURRDC_CONF_ACK_WAIT_TIME
#else /* WURRDC_CONF_ACK_WAIT_TIME */
#define ACK_WAIT_TIME (RTIMER_SECOND / 2500)
#endif /* WURRDC_CONF_ACK_WAIT_TIME */
#ifdef WURRDC_CONF_AFTER_ACK_DETECTED_WAIT_TIME
#define AFTER_ACK_DETECTED_WAIT_TIME WURRDC_CONF_AFTER_ACK_DETECTED_WAIT_TIME
#else /* WURRDC_CONF_AFTER_ACK_DETECTED_WAIT_TIME */
#define AFTER_ACK_DETECTED_WAIT_TIME (RTIMER_SECOND / 1500)
#endif /* WURRDC_CONF_AFTER_ACK_DETECTED_WAIT_TIME */
#endif /* WURRDC_802154_AUTOACK */

#ifdef WURRDC_CONF_SEND_802154_ACK
#define WURRDC_SEND_802154_ACK WURRDC_CONF_SEND_802154_ACK
#else /* WURRDC_CONF_SEND_802154_ACK */
#define WURRDC_SEND_802154_ACK 0
#endif /* WURRDC_CONF_SEND_802154_ACK */

#if WURRDC_SEND_802154_ACK
#include "net/mac/frame802154.h"
#endif /* WURRDC_SEND_802154_ACK */

#define ACK_LEN 3

/* Exposed by the WuR driver */
uint8_t WUR_RX_LENGTH;
uint8_t WUR_RX_BUFFER[LINKADDR_SIZE]; /* RX buffer for the WUR address */

uint8_t WUR_TX_LENGTH;
uint8_t WUR_TX_BUFFER[LINKADDR_SIZE]; /* TX buffer for the WUR address */

/*---------------------------------------------------------------------------*/
static void on(void) { NETSTACK_RADIO.on(); }
/*---------------------------------------------------------------------------*/
static void off(void) { NETSTACK_RADIO.off(); }
/*---------------------------------------------------------------------------*/

static int
send_one_packet(mac_callback_t sent, void *ptr)
{
  int ret;
  int last_sent_ok = 0;

  /* Fill WuS TX buffer without aliasing tricks */
  const linkaddr_t *dst = packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
  memcpy(WUR_TX_BUFFER, dst->u8, LINKADDR_SIZE);
  WUR_TX_LENGTH = LINKADDR_SIZE;

  /* Friendly log for WuS target */
  WUR_LOG("WuS TX: sending wake-up signal to ");
  addr_print((const linkaddr_t *)&WUR_TX_BUFFER);
  WUR_LOG("\n");

  /* Send the wake-up trigger (GPIO pulse) */
  wur_set_tx();
  clock_delay(100);
  wur_clear_tx();
  clock_delay(1000);

  WUR_LOG("Main radio: ON (preparing data TX)\n");
  on(); /* turn on the radio to send the data packet */

  packetbuf_set_addr(PACKETBUF_ADDR_SENDER, &linkaddr_node_addr);

#if WURRDC_802154_AUTOACK || WURRDC_802154_AUTOACK_HW
  packetbuf_set_attr(PACKETBUF_ATTR_MAC_ACK, 1);
#endif

  if (NETSTACK_FRAMER.create() < 0)
  {
    /* Failed to allocate space for headers */
    PRINTF("wurrdc: send failed, too large header\n");
    ret = MAC_TX_ERR_FATAL;
  }
  else
  {
#if WURRDC_802154_AUTOACK
    int is_broadcast;
    uint8_t dsn = ((uint8_t *)packetbuf_hdrptr())[2] & 0xff;

    NETSTACK_RADIO.prepare(packetbuf_hdrptr(), packetbuf_totlen());
    is_broadcast = packetbuf_holds_broadcast();

    if (NETSTACK_RADIO.receiving_packet() ||
        (!is_broadcast && NETSTACK_RADIO.pending_packet()))
    {
      /* Currently receiving a packet over air or a packet is pending. */
      ret = MAC_TX_COLLISION;
      off();
    }
    else
    {
      if (!is_broadcast)
      {
        RIMESTATS_ADD(reliabletx);
      }

      switch (NETSTACK_RADIO.transmit(packetbuf_totlen()))
      {
      case RADIO_TX_OK:
        if (is_broadcast)
        {
          off();
          ret = MAC_TX_OK;
        }
        else
        {
          rtimer_clock_t wt;

          /* Wait a short while for ACK energy to appear */
          wt = RTIMER_NOW();
          watchdog_periodic();
          while (RTIMER_CLOCK_LT(RTIMER_NOW(), wt + ACK_WAIT_TIME))
          {
#if CONTIKI_TARGET_COOJA
            simProcessRunValue = 1;
            cooja_mt_yield();
#endif
          }

          clock_delay(100); /* (~283us) seems sufficient to RX ACK */
          off();
          ret = MAC_TX_NOACK;

          if (!is_broadcast && (NETSTACK_RADIO.receiving_packet() ||
                                NETSTACK_RADIO.pending_packet() ||
                                NETSTACK_RADIO.channel_clear() == 0))
          {
            int len;
            uint8_t ackbuf[ACK_LEN];

            if (AFTER_ACK_DETECTED_WAIT_TIME > 0)
            {
              wt = RTIMER_NOW();
              watchdog_periodic();
              while (RTIMER_CLOCK_LT(RTIMER_NOW(), wt + AFTER_ACK_DETECTED_WAIT_TIME))
              {
#if CONTIKI_TARGET_COOJA
                simProcessRunValue = 1;
                cooja_mt_yield();
#endif
              }
            }

            if (NETSTACK_RADIO.pending_packet())
            {
              len = NETSTACK_RADIO.read(ackbuf, ACK_LEN);
              if (len == ACK_LEN && ackbuf[2] == dsn)
              {
                /* Ack received */
                RIMESTATS_ADD(ackrx);
                ret = MAC_TX_OK;
              }
              else
              {
                /* Not an ack or not for us: collision */
                ret = MAC_TX_COLLISION;
              }
            }
          }
          else
          {
            PRINTF("wurrdc tx noack\n");
          }
        }
        break;

      case RADIO_TX_COLLISION:
        ret = MAC_TX_COLLISION;
        off();
        break;

      default:
        ret = MAC_TX_ERR;
        off();
        break;
      }
    }
#else /* ! WURRDC_802154_AUTOACK */

    switch (NETSTACK_RADIO.send(packetbuf_hdrptr(), packetbuf_totlen()))
    {
    case RADIO_TX_OK:
      ret = MAC_TX_OK;
      break;
    case RADIO_TX_COLLISION:
      ret = MAC_TX_COLLISION;
      break;
    case RADIO_TX_NOACK:
      ret = MAC_TX_NOACK;
      break;
    default:
      ret = MAC_TX_ERR;
      break;
    }

#endif /* ! WURRDC_802154_AUTOACK */
  }

  if (ret == MAC_TX_OK)
  {
    last_sent_ok = 1;
  }
  mac_call_sent_callback(sent, ptr, ret, 1);

  return last_sent_ok;
}
/*---------------------------------------------------------------------------*/
static void
send_packet(mac_callback_t sent, void *ptr)
{
  send_one_packet(sent, ptr);
}
/*---------------------------------------------------------------------------*/
static void
send_list(mac_callback_t sent, void *ptr, struct rdc_buf_list *buf_list)
{
  while (buf_list != NULL)
  {
    /* Backup next pointer; may be cleared by mac_call_sent_callback() */
    struct rdc_buf_list *next = buf_list->next;
    int last_sent_ok;

    queuebuf_to_packetbuf(buf_list->buf);
    last_sent_ok = send_one_packet(sent, ptr);

    /* If TX failed, back off and let upper layers retry to preserve order */
    if (!last_sent_ok)
    {
      return;
    }
    buf_list = next;
  }
}
/*---------------------------------------------------------------------------*/
static void
packet_input(void)
{
#if WURRDC_SEND_802154_ACK
  int original_datalen = packetbuf_datalen();
  uint8_t *original_dataptr = packetbuf_dataptr();
#endif

#if WURRDC_802154_AUTOACK
  if (packetbuf_datalen() == ACK_LEN)
  {
    /* Ignore ack packets */
    PRINTF("wurrdc: ignored ack\n");
  }
  else
#endif /* WURRDC_802154_AUTOACK */
    if (NETSTACK_FRAMER.parse() < 0)
    {
      PRINTF("wurrdc: failed to parse %u\n", packetbuf_datalen());
#if WURRDC_ADDRESS_FILTER
    }
    else if (!linkaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
                           &linkaddr_node_addr) &&
             !packetbuf_holds_broadcast())
    {
      PRINTF("wurrdc: not for us\n");
#endif /* WURRDC_ADDRESS_FILTER */
    }
    else
    {
      int duplicate = 0;

#if (WURRDC_802154_AUTOACK || WURRDC_802154_AUTOACK_HW) && RDC_WITH_DUPLICATE_DETECTION
      /* Check for duplicate packet. */
      duplicate = mac_sequence_is_duplicate();
      if (duplicate)
      {
        /* Drop the packet. */
        PRINTF("wurrdc: drop duplicate link layer packet %u\n",
               packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO));
      }
      else
      {
        mac_sequence_register_seqno();
      }
#endif /* DUP DETECTION */

#if WURRDC_SEND_802154_ACK
      /* (Optional) Send SW 802.15.4 ACK */
      {
        frame802154_t info154;
        frame802154_parse(original_dataptr, original_datalen, &info154);
        if (info154.fcf.frame_type == FRAME802154_DATAFRAME &&
            info154.fcf.ack_required != 0 &&
            linkaddr_cmp((linkaddr_t *)&info154.dest_addr, &linkaddr_node_addr))
        {
          uint8_t ackdata[ACK_LEN] = {FRAME802154_ACKFRAME, 0, info154.seq};
          NETSTACK_RADIO.send(ackdata, ACK_LEN);
        }
      }
#endif /* WURRDC_SEND_802154_ACK */

      /* WUR optimisation: early OFF for unicast-to-this-node (with clear log) */
      if (linkaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER), &linkaddr_node_addr))
      {
        WUR_LOG("Main radio: OFF (unicast for this node — turning off early)\n");
        off();
      }

      if (!duplicate)
      {
        NETSTACK_MAC.input();
      }
    }

  /* Post-RX radio off (friendly log) — idempotent if already OFF */
  WUR_LOG("Main radio: OFF (after RX processing)\n");
  off();
}
/*---------------------------------------------------------------------------*/

PROCESS(wur_process, "wur event handler process");

static void
init(void)
{
  printf("wurrdc: Initialized\n");
  wur_init();

  process_start(&wur_process, NULL);
  on();
}
/*---------------------------------------------------------------------------*/
static int turn_on(void) { return NETSTACK_RADIO.on(); }
static int turn_off(int keep_radio_on) { return keep_radio_on ? NETSTACK_RADIO.on() : NETSTACK_RADIO.off(); }
static unsigned short channel_check_interval(void) { return 0; }
/*---------------------------------------------------------------------------*/
const struct rdc_driver wurrdc_driver = {
    "wurrdc",
    init,
    send_packet,
    send_list,
    packet_input,
    turn_on,
    turn_off,
    channel_check_interval,
};
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(wur_process, ev, data)
{
  static struct etimer timer;

  PROCESS_BEGIN();
  SENSORS_ACTIVATE(wur_sensor); /* enable wake-up radio */

  etimer_set(&timer, CLOCK_SECOND / 64);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
  WUR_LOG("Main radio: OFF (startup cooldown)\n");
  off();

  while (1)
  {
    PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event && data == &wur_sensor);

    if ((WUR_RX_LENGTH == LINKADDR_SIZE) &&
        (linkaddr_cmp((linkaddr_t *)&WUR_RX_BUFFER, &linkaddr_null) || /* broadcast WuS */
         linkaddr_cmp((linkaddr_t *)&WUR_RX_BUFFER, &linkaddr_node_addr)))
    { /* unicast WuS */
      WUR_LOG("WuR event: received WuS for ");
      addr_print((linkaddr_t *)&WUR_RX_BUFFER);
      WUR_LOG("\n");

      WUR_LOG("Main radio: ON (waiting for data after WuS)\n");
      on();                  /* turn on the radio on the receiver side */
      etimer_set(&timer, 3); /* timeout for the reception of data packet (keep original) */
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
      WUR_LOG("Main radio: OFF (WuS data window elapsed)\n");
      off(); /* turn off radio after reception */
    }
  }
  PROCESS_END();
}
