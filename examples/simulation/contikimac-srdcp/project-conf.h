#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

/* ========================================================================== */
/* ======================= Radio & PHY Layer Settings ======================= */
/* ========================================================================== */

/* RF channel 26 for CC2420 */
#undef CC2420_CONF_CHANNEL
#define CC2420_CONF_CHANNEL 26
/* Network's PAN ID */
#undef IEEE802154_CONF_PANID
#define IEEE802154_CONF_PANID 0xABCD
/* Use hardware auto-acknowledgements */
#undef CC2420_CONF_AUTOACK
#define CC2420_CONF_AUTOACK 1
/* Let wurrdc use the 802.15.4 auto-ack feature */
#define WURRDC_CONF_802154_AUTOACK 1

/* ========================================================================== */
/* ========================= Network Stack Settings ========================= */
/* ========================================================================== */

/* --- MAC Layer --- */
#undef NETSTACK_CONF_MAC
#define NETSTACK_CONF_MAC csma_driver

/* --- Framer --- */
#undef NETSTACK_CONF_FRAMER
#define NETSTACK_CONF_FRAMER framer_802154

/* --- Link-layer Security --- */
#undef NETSTACK_CONF_LLSEC
#define NETSTACK_CONF_LLSEC nullsec_driver

/* --- RDC (Radio Duty Cycling) Layer --- */
/* By default, wurrdc_driver is used.
 * You can override this in the Makefile with CFLAGS, for example:
 * CFLAGS += -DNETSTACK_CONF_RDC=contikimac_driver
 */
#ifndef NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC contikimac_driver
#endif

/* ========================================================================== */
/* ======================== Buffering & Debugging =========================== */
/* ========================================================================== */

/* Increase queue buffers for larger topologies or more control frames */
#ifndef QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM 16 /* tăng nếu topo lớn/nhiều control frames */
#endif

/* Enable Rime statistics to count retransmissions/ACKs */
#define RIMESTATS_CONF_ENABLED 1

/* Disable IPv6 for Rime-based applications */
#undef UIP_CONF_IPV6
#define UIP_CONF_IPV6 0

#endif /* PROJECT_CONF_H_ */
