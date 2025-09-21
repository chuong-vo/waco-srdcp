#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

#include <stdint.h>

/* ==== RPL MOP fallback (cho codebase cũ) ==== */
#ifndef RPL_MOP_NO_DOWNWARD_ROUTES
#define RPL_MOP_NO_DOWNWARD_ROUTES 0
#endif
#ifndef RPL_MOP_NON_STORING
#define RPL_MOP_NON_STORING 1
#endif
#ifndef RPL_MOP_STORING
#define RPL_MOP_STORING 2
#endif
#ifndef RPL_MOP_STORING_MULTICAST
#define RPL_MOP_STORING_MULTICAST 3
#endif

/* ==== Radio / 802.15.4 ==== */
#define CC2420_CONF_CHANNEL 26
#define IEEE802154_CONF_PANID 0xABCD
#define CC2420_CONF_AUTOACK 1
#ifndef CC2420_CONF_TXPOWER
#define CC2420_CONF_TXPOWER 27
#endif

/* ==== Netstack: TSCH + no RDC + 802.15.4e + no LLSEC ==== */
#undef NETSTACK_CONF_MAC
#define NETSTACK_CONF_MAC tschmac_driver
#undef NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC nordc_driver
#undef NETSTACK_CONF_FRAMER
#define NETSTACK_CONF_FRAMER framer_802154
#undef NETSTACK_CONF_LLSEC
#define NETSTACK_CONF_LLSEC nullsec_driver

#undef FRAME802154_CONF_VERSION
#define FRAME802154_CONF_VERSION FRAME802154_IEEE802154E_2012

/* ==== TSCH runtime & schedule (siêu tiết kiệm) ==== */
#undef TSCH_CONF_AUTOSTART
#define TSCH_CONF_AUTOSTART 1
#undef TSCH_LOG_CONF_LEVEL
#define TSCH_LOG_CONF_LEVEL 0

/* Giữ minimal để không phải thêm code; nhưng khóa kích thước lịch thật nhỏ */
#undef TSCH_SCHEDULE_CONF_WITH_6TISCH_MINIMAL
#define TSCH_SCHEDULE_CONF_WITH_6TISCH_MINIMAL 1
#ifndef TSCH_SCHEDULE_CONF_MAX_LINKS
#define TSCH_SCHEDULE_CONF_MAX_LINKS 8 /* giảm mạnh số link tối đa */
#endif
#ifndef TSCH_SCHEDULE_CONF_MAX_SLOTFRAMES
#define TSCH_SCHEDULE_CONF_MAX_SLOTFRAMES 1
#endif

/* Hopping 16 kênh (RAM ~0) */
#undef TSCH_CONF_DEFAULT_HOPPING_SEQUENCE
#define TSCH_CONF_DEFAULT_HOPPING_SEQUENCE (uint8_t[]){ \
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}

#ifndef TSCH_RPL_CONF_ENABLED
#define TSCH_RPL_CONF_ENABLED 1
#endif
#ifndef TSCH_CONF_MAC_MAX_FRAME_RETRIES
#define TSCH_CONF_MAC_MAX_FRAME_RETRIES 5
#endif

/* ==== TSCH queue (power-of-two, cực nhỏ) ==== */
#ifndef TSCH_QUEUE_CONF_NUM_PER_NEIGHBOR
#define TSCH_QUEUE_CONF_NUM_PER_NEIGHBOR 4
#endif
#ifndef TSCH_QUEUE_CONF_MAX_NEIGHBOR_QUEUES
#define TSCH_QUEUE_CONF_MAX_NEIGHBOR_QUEUES 8
#endif

/* ==== Platform specifics ==== */
#undef DCOSYNCH_CONF_ENABLED
#define DCOSYNCH_CONF_ENABLED 0
#undef CC2420_CONF_SFD_TIMESTAMPS
#define CC2420_CONF_SFD_TIMESTAMPS 1

/* ==== IPv6 / uIP (cắt xuống mức tối thiểu) ==== */
#undef UIP_CONF_IPV6
#define UIP_CONF_IPV6 1
#undef UIP_CONF_TCP
#define UIP_CONF_TCP 0
#undef UIP_CONF_UDP_CONNS
#define UIP_CONF_UDP_CONNS 1 /* từ 2 xuống 1 để tiết kiệm RAM */
#undef UIP_CONF_IPV6_QUEUE_PKT
#define UIP_CONF_IPV6_QUEUE_PKT 0
#ifndef UIP_CONF_ND6_RA_RDNSS
#define UIP_CONF_ND6_RA_RDNSS 0
#endif

/* ==== RPL (classic, storing để nhẹ RAM) ==== */
#undef RPL_CONF_MOP
#define RPL_CONF_MOP RPL_MOP_STORING
#ifdef RPL_CONF_WITH_DAO_ACK
#undef RPL_CONF_WITH_DAO_ACK
#define RPL_CONF_WITH_DAO_ACK 0
#endif
#undef RPL_CONF_OF_OCP
#define RPL_CONF_OF_OCP RPL_OCP_MRHOF
#undef RPL_CONF_MIN_HOPRANKINC
#define RPL_CONF_MIN_HOPRANKINC 256
#undef RPL_CONF_DIO_INTERVAL_MIN
#define RPL_CONF_DIO_INTERVAL_MIN 8
#undef RPL_CONF_DIO_INTERVAL_DOUBLINGS
#define RPL_CONF_DIO_INTERVAL_DOUBLINGS 10
#ifndef RPL_CONF_STATS
#define RPL_CONF_STATS 0
#endif

/* ==== 6LoWPAN (tránh fragment) ==== */
#undef SICSLOWPAN_CONF_FRAG
#define SICSLOWPAN_CONF_FRAG 0
#ifndef SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS
#define SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS 0
#endif

/* ==== Bảng láng giềng / route / queuebuf (cực nhỏ) ==== */
#undef NBR_TABLE_CONF_MAX_NEIGHBORS
#define NBR_TABLE_CONF_MAX_NEIGHBORS 8
#undef UIP_CONF_MAX_ROUTES
#define UIP_CONF_MAX_ROUTES 8

#ifndef QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM 8
#else
#undef QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM 8
#endif

/* ==== App pacing & cắt mảng app (nếu app đọc macro) ==== */
#ifndef APP_PKT_GAP_MS
#define APP_PKT_GAP_MS 120
#endif

/* Nếu app có mảng thống kê lớn, ép nhỏ lại ở đây */
#ifdef PDR_MAX_SRC
#undef PDR_MAX_SRC
#endif
#define PDR_MAX_SRC 6

#ifdef MAP_MAX_NODES
#undef MAP_MAX_NODES
#endif
#define MAP_MAX_NODES 6

/* Tắt rimestats */
#undef RIMESTATS_CONF_ENABLED
#define RIMESTATS_CONF_ENABLED 0

#endif /* PROJECT_CONF_H_ */
