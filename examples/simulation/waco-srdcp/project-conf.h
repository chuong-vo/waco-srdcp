#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

/* ========================================================================== */
/* ======================= Radio & PHY Layer Settings ======================= */
/* ========================================================================== */

/* RF channel 26 for CC2420 */
#ifndef CC2420_CONF_CHANNEL
#define CC2420_CONF_CHANNEL 26
#endif
/* Network's PAN ID */
#ifndef IEEE802154_CONF_PANID
#define IEEE802154_CONF_PANID 0xABCD
#endif
/* Use hardware auto-acknowledgements */
#ifndef CC2420_CONF_AUTOACK
#define CC2420_CONF_AUTOACK 1
#endif
/* Let wurrdc use the 802.15.4 auto-ack feature */
#ifndef WURRDC_CONF_802154_AUTOACK
#define WURRDC_CONF_802154_AUTOACK 1
#endif

/* ========================================================================== */
/* ========================= Network Stack Settings ========================= */
/* ========================================================================== */

/* --- MAC Layer --- */
#ifndef NETSTACK_CONF_MAC
#define NETSTACK_CONF_MAC csma_driver
#endif

/* --- Framer --- */
#ifndef NETSTACK_CONF_FRAMER
#define NETSTACK_CONF_FRAMER framer_802154
#endif

/* --- Link-layer Security --- */
#ifndef NETSTACK_CONF_LLSEC
#define NETSTACK_CONF_LLSEC nullsec_driver
#endif

/* --- RDC (Radio Duty Cycling) Layer --- */
/* By default, wurrdc_driver is used.
 * You can override this in the Makefile with CFLAGS, for example:
 * CFLAGS += -DNETSTACK_CONF_RDC=contikimac_driver
 */
#ifndef NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC wurrdc_driver
#endif

/* ========================================================================== */
/* ======================== Buffering & Debugging =========================== */
/* ========================================================================== */

/* Increase queue buffers for larger topologies or more control frames */
#ifndef QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM 16 /* tăng nếu topo lớn/nhiều control frames */
#endif

/* Enable Rime statistics to count retransmissions/ACKs */
#ifndef RIMESTATS_CONF_ENABLED
#define RIMESTATS_CONF_ENABLED 1
#endif

/* Disable IPv6 for Rime-based applications */
#ifndef UIP_CONF_IPV6
#define UIP_CONF_IPV6 0
#endif

#endif /* PROJECT_CONF_H_ */
