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

/* ========================================================================== */
/* ========================= Network Stack Settings ========================= */
/* ========================================================================== */

/* --- MAC, Framer, and LLSEC Layers --- */
#define NETSTACK_CONF_MAC csma_driver
#define NETSTACK_CONF_FRAMER framer_802154
#define NETSTACK_CONF_LLSEC nullsec_driver

/* --- RDC (Radio Duty Cycling) Layer --- */
/* RPL with IPv6 is memory-intensive on Sky motes.
 * ContikiMAC is used by default.
 * You can override this in the Makefile with CFLAGS.
 */
#ifndef NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC contikimac_driver
#endif

/* Set ContikiMAC channel check rate (default is 8 Hz) */
#ifndef NETSTACK_CONF_RDC_CHANNEL_CHECK_RATE
#define NETSTACK_CONF_RDC_CHANNEL_CHECK_RATE 128
#endif
/* ========================================================================== */
/* ===================== IPv6 & RPL Protocol Settings ===================== */
/* ========================================================================== */

/* Enable IPv6 and RPL */
#define UIP_CONF_IPV6 1

/* --- RPL Configuration --- */
/* Default to Non-Storing mode.
 * Non-Storing mode is lighter on RAM for intermediate nodes as they only
 * need to know their parent, while the root builds the full source-routing path.
 */
#ifndef RPL_CONF_MOP
#define RPL_CONF_MOP RPL_MOP_NON_STORING
#endif
/* Set the Objective Function to MRHOF */
#define RPL_CONF_OF_OCP RPL_OCP_MRHOF
/* RPL DIO timer settings to reduce control traffic in stable networks */
#define RPL_CONF_DIO_INTERVAL_MIN 12
#define RPL_CONF_DIO_INTERVAL_DOUBLINGS 8
/* Minimum hop rank increase */
#define RPL_CONF_MIN_HOPRANKINC 256

/* ========================================================================== */
/* =================== Memory & Buffer Optimization for Sky ================= */
/* ========================================================================== */

/* Disable TCP to save code space */
#define UIP_CONF_TCP 0
/* Undefine first to avoid redefinition warnings */
#undef UIP_CONF_UDP_CONNS
/* Reduce the number of UDP connections */
#define UIP_CONF_UDP_CONNS 2
/* Disable packet queue in uIP to save RAM */
#define UIP_CONF_IPV6_QUEUE_PKT 0
/* Further adjust neighbor and route table sizes to fit RAM on Sky motes */
#undef NBR_TABLE_CONF_MAX_NEIGHBORS
#define NBR_TABLE_CONF_MAX_NEIGHBORS 8
#undef UIP_CONF_MAX_ROUTES
#define UIP_CONF_MAX_ROUTES 8
/* Disable 6LoWPAN fragmentation to save RAM */
/* Enable 6LoWPAN fragmentation to handle larger packets (e.g. RPL source routing) */
#define SICSLOWPAN_CONF_FRAG 1
/* Further adjust queue buffer size to fit RAM */
#undef QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM 12

#endif /* PROJECT_CONF_H_ */
