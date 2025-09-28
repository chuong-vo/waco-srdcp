#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

/* ===== Radio / 802.15.4 ===== */
#define CC2420_CONF_CHANNEL 26
#define IEEE802154_CONF_PANID 0xABCD
#define CC2420_CONF_AUTOACK 1
#define WURRDC_CONF_802154_AUTOACK 1

/* ===== RPL operating mode (compat fallback for older headers) ===== */
#ifndef RPL_MOP_NON_STORING
#define RPL_MOP_NON_STORING 1
#endif

/* ===== Netstack selection ===== */
#undef NETSTACK_CONF_RDC
#undef NETSTACK_CONF_MAC
#undef NETSTACK_CONF_FRAMER
#undef NETSTACK_CONF_LLSEC

#define NETSTACK_CONF_MAC csma_driver
#define NETSTACK_CONF_FRAMER framer_802154
#define NETSTACK_CONF_LLSEC nullsec_driver
/* RPL + IPv6 on Sky is memory tight. Use nullrdc to reduce footprint. */
// #define NETSTACK_CONF_RDC wurrdc_driver
#define NETSTACK_CONF_RDC contikimac_driver

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
#define NBR_TABLE_CONF_MAX_NEIGHBORS 16 /* đủ dư địa cho root + mạng dày hơn */
/* RPL non-storing: no per-node routing table, root keeps link table */
#undef UIP_CONF_MAX_ROUTES
#define UIP_CONF_MAX_ROUTES 0
#undef RPL_CONF_MOP
#define RPL_CONF_MOP RPL_MOP_NON_STORING
#undef RPL_CONF_WITH_STORING
#define RPL_CONF_WITH_STORING 0
#ifndef RPL_CONF_WITH_NON_STORING
#define RPL_CONF_WITH_NON_STORING 1
#endif
#undef RPL_NS_CONF_LINK_NUM
#define RPL_NS_CONF_LINK_NUM 32
/* Non-storing DL thêm SRH => gói IPv6 có thể > 1 khung 802.15.4.
 * Bật fragment để tránh drop DL khi đường dài. */
#undef SICSLOWPAN_CONF_FRAG
#define SICSLOWPAN_CONF_FRAG 1

/* Bật log RPL ở mức DEBUG để theo dõi DAO/DIO chi tiết */
#undef LOG_CONF_LEVEL_RPL
#define LOG_CONF_LEVEL_RPL LOG_LEVEL_DBG
#undef LOG_CONF_LEVEL_MAIN
#define LOG_CONF_LEVEL_MAIN LOG_LEVEL_DBG

/* ===== Chu kỳ UL/DL và beacon (RPL DIO) ===== */
#ifndef UL_PERIOD
#define UL_PERIOD (30 * CLOCK_SECOND)
#endif
#ifndef DL_PERIOD
#define DL_PERIOD (60 * CLOCK_SECOND)
#endif
/* DIO gửi định kỳ ~ 2^(MIN..MIN+DOUBLINGS) ms, tương đương "beacon" trong RPL */
#ifndef RPL_CONF_DIO_INTERVAL_MIN
#define RPL_CONF_DIO_INTERVAL_MIN 14
#endif
#ifndef RPL_CONF_DIO_INTERVAL_DOUBLINGS
#define RPL_CONF_DIO_INTERVAL_DOUBLINGS 4
#endif

/* Buffering & debug */
#ifndef UIP_CONF_BUFFER_SIZE
#define UIP_CONF_BUFFER_SIZE 140 /* đủ chỗ chèn SRH cho non-storing */
#endif
#ifndef QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM 16
#endif
#define RIMESTATS_CONF_ENABLED 0

#endif /* PROJECT_CONF_H_ */
