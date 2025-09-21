#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

#include <stdint.h>

/* ===== Radio / IEEE 802.15.4 ===== */
#define CC2420_CONF_CHANNEL        26
#define IEEE802154_CONF_PANID      0xABCD
#define CC2420_CONF_AUTOACK        1

/* ===== Enable TSCH stack ===== */
#undef  NETSTACK_CONF_MAC
#define NETSTACK_CONF_MAC          tschmac_driver
#undef  NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC          nordc_driver
#undef  NETSTACK_CONF_FRAMER
#define NETSTACK_CONF_FRAMER       framer_802154
#undef  NETSTACK_CONF_LLSEC
#define NETSTACK_CONF_LLSEC        nullsec_driver

#undef  FRAME802154_CONF_VERSION
#define FRAME802154_CONF_VERSION   FRAME802154_IEEE802154E_2012

#define RPL_CALLBACK_PARENT_SWITCH     tsch_rpl_callback_parent_switch
#define RPL_CALLBACK_NEW_DIO_INTERVAL  tsch_rpl_callback_new_dio_interval
#define TSCH_CALLBACK_JOINING_NETWORK  tsch_rpl_callback_joining_network
#define TSCH_CALLBACK_LEAVING_NETWORK  tsch_rpl_callback_leaving_network

#undef  TSCH_CONF_AUTOSTART
#define TSCH_CONF_AUTOSTART        0
#undef  TSCH_LOG_CONF_LEVEL
#define TSCH_LOG_CONF_LEVEL        0
#undef  TSCH_SCHEDULE_CONF_WITH_6TISCH_MINIMAL
#define TSCH_SCHEDULE_CONF_WITH_6TISCH_MINIMAL 1
#undef  TSCH_CONF_DEFAULT_HOPPING_SEQUENCE
#define TSCH_CONF_DEFAULT_HOPPING_SEQUENCE (uint8_t[]){ 26 }

/* cc2420 platform specifics */
#undef  DCOSYNCH_CONF_ENABLED
#define DCOSYNCH_CONF_ENABLED      0
#undef  CC2420_CONF_SFD_TIMESTAMPS
#define CC2420_CONF_SFD_TIMESTAMPS 1

/* ===== IPv6 + RPL tuning (Sky friendly) ===== */
#undef UIP_CONF_IPV6
#define UIP_CONF_IPV6              1
#undef UIP_CONF_TCP
#define UIP_CONF_TCP               0
#undef UIP_CONF_UDP_CONNS
#define UIP_CONF_UDP_CONNS         4
#undef UIP_CONF_IPV6_QUEUE_PKT
#define UIP_CONF_IPV6_QUEUE_PKT    0
#undef NBR_TABLE_CONF_MAX_NEIGHBORS
#define NBR_TABLE_CONF_MAX_NEIGHBORS 8
#undef UIP_CONF_MAX_ROUTES
#define UIP_CONF_MAX_ROUTES        8
#undef SICSLOWPAN_CONF_FRAG
#define SICSLOWPAN_CONF_FRAG       0

/* Buffers & stats */
#ifndef QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM          8
#endif
#define RIMESTATS_CONF_ENABLED     0

#endif /* PROJECT_CONF_H_ */
