#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

/* ===== Radio / 802.15.4 ===== */
#define CC2420_CONF_CHANNEL 26
#define IEEE802154_CONF_PANID 0xABCD
#define CC2420_CONF_AUTOACK 1
#define WURRDC_CONF_802154_AUTOACK 1

/* ===== Netstack selection ===== */
#undef NETSTACK_CONF_RDC
#undef NETSTACK_CONF_MAC
#undef NETSTACK_CONF_FRAMER
#undef NETSTACK_CONF_LLSEC

#define NETSTACK_CONF_MAC csma_driver
#define NETSTACK_CONF_FRAMER framer_802154
#define NETSTACK_CONF_LLSEC nullsec_driver
/* RPL + IPv6 on Sky is memory tight. Use nullrdc to reduce footprint. */
#define NETSTACK_CONF_RDC wurrdc_driver
#undef NULLRDC_CONF_802154_AUTOACK
#define NULLRDC_CONF_802154_AUTOACK 1

#undef NETSTACK_CONF_RDC_CHANNEL_CHECK_RATE
#define NETSTACK_CONF_RDC_CHANNEL_CHECK_RATE 128

/* IPv6 + RPL enabled */
#undef UIP_CONF_IPV6
#define UIP_CONF_IPV6 1
/* Trim memory usage for Sky */
#undef UIP_CONF_TCP
#define UIP_CONF_TCP 0
#undef UIP_CONF_UDP_CONNS
#define UIP_CONF_UDP_CONNS 4
#undef UIP_CONF_IPV6_QUEUE_PKT
#define UIP_CONF_IPV6_QUEUE_PKT 0
#undef NBR_TABLE_CONF_MAX_NEIGHBORS
#define NBR_TABLE_CONF_MAX_NEIGHBORS 5
#undef UIP_CONF_MAX_ROUTES
#define UIP_CONF_MAX_ROUTES 5
/* Avoid 6LoWPAN fragmentation to save RAM on Sky */
#undef SICSLOWPAN_CONF_FRAG
#define SICSLOWPAN_CONF_FRAG 0

/* Buffering & debug */
#ifndef QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM 4
#endif
#define RIMESTATS_CONF_ENABLED 0

#endif /* PROJECT_CONF_H_ */
