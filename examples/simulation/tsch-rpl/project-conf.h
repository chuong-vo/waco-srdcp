#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

/* ========================================================================== */
/* ======================= Radio & PHY Layer Settings ======================= */
/* ========================================================================== */

/* RF channel 26 for CC2420 */
#define CC2420_CONF_CHANNEL 26
/* Network's PAN ID */
#define IEEE802154_CONF_PANID 0xABCD
/* Use hardware auto-acknowledgements */
#define CC2420_CONF_AUTOACK 1
/* Set transmit power (default is 31, max) */
#ifndef CC2420_CONF_TXPOWER
#define CC2420_CONF_TXPOWER 27
#endif

/* ========================================================================== */
/* ========================= Network Stack Settings ========================= */
/* ========================================================================== */

/* --- Netstack drivers for TSCH --- */
#undef NETSTACK_CONF_MAC
#define NETSTACK_CONF_MAC tschmac_driver
#undef NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC nordc_driver
#undef NETSTACK_CONF_FRAMER
#define NETSTACK_CONF_FRAMER framer_802154
#undef NETSTACK_CONF_LLSEC
#define NETSTACK_CONF_LLSEC nullsec_driver

/* Use the IEEE 802.15.4e-2012 frame version for TSCH */
#undef FRAME802154_CONF_VERSION
#define FRAME802154_CONF_VERSION FRAME802154_IEEE802154E_2012

/* ========================================================================== */
/* ======================== TSCH Protocol Settings ========================== */
/* ========================================================================== */

/* --- TSCH Core --- */
#undef TSCH_CONF_AUTOSTART
#define TSCH_CONF_AUTOSTART 1
#define TSCH_LOG_CONF_LEVEL 0
#undef TSCH_CONF_MAC_MAX_FRAME_RETRIES
#define TSCH_CONF_MAC_MAX_FRAME_RETRIES 5

/* --- TSCH Schedule --- */
/* Use the minimal schedule to save RAM, but keep the schedule size small */
#define TSCH_SCHEDULE_CONF_WITH_6TISCH_MINIMAL 1
#ifndef TSCH_SCHEDULE_CONF_MAX_LINKS
#define TSCH_SCHEDULE_CONF_MAX_LINKS 8 /* Drastically reduce max links */
#endif
#ifndef TSCH_SCHEDULE_CONF_MAX_SLOTFRAMES
#define TSCH_SCHEDULE_CONF_MAX_SLOTFRAMES 1
#endif

/* --- TSCH Queues --- */
/* Keep queue sizes small and power-of-two for efficiency */
#undef TSCH_QUEUE_CONF_NUM_PER_NEIGHBOR
#ifndef TSCH_QUEUE_CONF_NUM_PER_NEIGHBOR
#define TSCH_QUEUE_CONF_NUM_PER_NEIGHBOR 2 /* Aggressive reduction for RAM */
#endif
#ifndef TSCH_QUEUE_CONF_MAX_NEIGHBOR_QUEUES
#undef TSCH_QUEUE_CONF_MAX_NEIGHBOR_QUEUES
#define TSCH_QUEUE_CONF_MAX_NEIGHBOR_QUEUES 8
#endif

/* --- Platform-specific TSCH settings for Sky motes --- */
#undef CC2420_CONF_SFD_TIMESTAMPS
#define CC2420_CONF_SFD_TIMESTAMPS 1

/* ========================================================================== */
/* ===================== IPv6 & RPL Protocol Settings ===================== */
/* ========================================================================== */

/* --- IPv6 Core --- */
#undef UIP_CONF_IPV6
#define UIP_CONF_IPV6 1
#undef UIP_CONF_TCP
#define UIP_CONF_TCP 0

/* --- RPL Configuration --- */
/* Default to Non-Storing mode as requested.
 * Switching to Storing mode. This increases route memory on nodes
 * but allows disabling fragmentation, saving significant overall RAM.
 */
#undef RPL_CONF_MOP
/* #define RPL_CONF_MOP RPL_MOP_NON_STORING */
#define RPL_CONF_MOP RPL_MOP_STORING_NO_MULTICAST

/* Other RPL settings */
#undef RPL_CONF_WITH_DAO_ACK
#define RPL_CONF_WITH_DAO_ACK 0
#undef RPL_CONF_OF_OCP
#define RPL_CONF_OF_OCP RPL_OCP_MRHOF
#undef RPL_CONF_MIN_HOPRANKINC
/* #define RPL_CONF_MIN_HOPRANKINC 256 */
#undef RPL_CONF_DIO_INTERVAL_MIN
#define RPL_CONF_DIO_INTERVAL_MIN 12
#undef RPL_CONF_DIO_INTERVAL_DOUBLINGS
#define RPL_CONF_DIO_INTERVAL_DOUBLINGS 8
#ifndef RPL_CONF_STATS
#define RPL_CONF_STATS 0
#endif

/* Disable all logging to save ROM */
#ifndef LOG_CONF_LEVEL_RPL
#define LOG_CONF_LEVEL_RPL LOG_LEVEL_NONE
#endif

/* Disable further statistics to save ROM */
#undef UIP_CONF_STATISTICS
#define UIP_CONF_STATISTICS 0
#undef PROCESS_CONF_STATS
#define PROCESS_CONF_STATS 0

/* Disable the Contiki shell to save a significant amount of ROM */
#define SHELL_CONF_ENABLED 0

/* ========================================================================== */
/* =================== Memory & Buffer Optimization for Sky =================== */
/* ========================================================================== */

/* --- uIP Buffers --- */
#undef UIP_CONF_UDP_CONNS
#define UIP_CONF_UDP_CONNS 1 /* Reduce from 2 to 1 to save RAM */
#undef UIP_CONF_IPV6_QUEUE_PKT
#define UIP_CONF_IPV6_QUEUE_PKT 0

/* --- 6LoWPAN --- */
#undef SICSLOWPAN_CONF_FRAG
/* Fragmentation can be disabled in RPL Storing mode, saving RAM */
#define SICSLOWPAN_CONF_FRAG 0
#ifndef SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS
#define SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS 0
#endif

/* --- Neighbor and Route Tables --- */
#undef NBR_TABLE_CONF_MAX_NEIGHBORS
#define NBR_TABLE_CONF_MAX_NEIGHBORS 8
#undef UIP_CONF_MAX_ROUTES
/* Storing mode requires routes on intermediate nodes */
#define UIP_CONF_MAX_ROUTES 8

/* --- General Queue Buffers --- */
#ifndef QUEUEBUF_CONF_NUM
#undef QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM 8 /* Can be larger in Storing mode */
#endif

#endif /* PROJECT_CONF_H_ */
