#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

/* ===== Radio / 802.15.4 ===== */
#define CC2420_CONF_CHANNEL 26       /* kênh RF cho Sky/CC2420 */
#define IEEE802154_CONF_PANID 0xABCD /* PAN ID đồng nhất toàn mạng */
#define CC2420_CONF_AUTOACK 1        /* Auto-ACK ở PHY */
#define WURRDC_CONF_802154_AUTOACK 1 /* Cho wurrdc dùng Auto-ACK của 802.15.4 */

/* ===== Stack selection (switchable via CFLAGS) =====
 * Mặc định: WaCo với WuR (wurrdc + csma).
 * Có thể override bằng CFLAGS:
 *   -DUSE_WURRDC=1
 *   -DUSE_CONTIKIMAC=1
 *   -DUSE_NULLRDC=1
 *   -DUSE_CXMAC=1
 *   -DUSE_TSCH=1  (THỬ NGHIỆM: cần app khởi tạo TSCH coordinator/join)
 */
#undef NETSTACK_CONF_RDC
#undef NETSTACK_CONF_MAC
#undef NETSTACK_CONF_FRAMER
#undef NETSTACK_CONF_LLSEC

// #if defined(USE_TSCH)
/* TSCH (experimental with SRDCP): tschmac + nordc, 802.15.4e-2012 */
// #define NETSTACK_CONF_MAC tschmac_driver
// #define NETSTACK_CONF_RDC nordc_driver
// #define NETSTACK_CONF_FRAMER framer_802154
// #define NETSTACK_CONF_LLSEC nullsec_driver
// /* TSCH requires 802.15.4e frame version and a schedule (e.g., 6TiSCH minimal). */
// #undef FRAME802154_CONF_VERSION
// #define FRAME802154_CONF_VERSION FRAME802154_IEEE802154E_2012
// #undef TSCH_CONF_AUTOSTART
// #define TSCH_CONF_AUTOSTART 1
// #undef TSCH_SCHEDULE_CONF_WITH_6TISCH_MINIMAL
// #define TSCH_SCHEDULE_CONF_WITH_6TISCH_MINIMAL 1
// /* Useful for CC2420 (Sky/Z1): timestamps on, disable DCO sync */
// #undef CC2420_CONF_SFD_TIMESTAMPS
// #define CC2420_CONF_SFD_TIMESTAMPS 1
// #undef DCOSYNCH_CONF_ENABLED
// #define DCOSYNCH_CONF_ENABLED 0
/* Keep RF on channel 26 and single-channel hopping for fairness */
#undef CC2420_CONF_CHANNEL
#define CC2420_CONF_CHANNEL 26
#undef IEEE802154_CONF_PANID
#define IEEE802154_CONF_PANID 0xABCD
// #undef TSCH_CONF_DEFAULT_HOPPING_SEQUENCE
// #define TSCH_CONF_DEFAULT_HOPPING_SEQUENCE (uint8_t[]){26}
// #undef TSCH_CONF_JOIN_HOPPING_SEQUENCE
// #define TSCH_CONF_JOIN_HOPPING_SEQUENCE (uint8_t[]){26}
// #else
/* Non-TSCH: use standard 802.15.4 framer and CSMA */
#define NETSTACK_CONF_MAC csma_driver
#define NETSTACK_CONF_FRAMER framer_802154
#define NETSTACK_CONF_LLSEC nullsec_driver
#define NETSTACK_CONF_RDC contikimac_driver
// #define NETSTACK_CONF_RDC nullrdc_driver
// #define NETSTACK_CONF_RDC cxmac_driver
// #define NETSTACK_CONF_RDC wurrdc_driver

/* ===== Duty-cycling main radio (thông tin in log) =====
 * Với WuR, main radio phần lớn được bật theo WuS, nhưng driver vẫn in "channel check rate".
 * 64 Hz là hợp lý; nếu cần phản ứng nhanh hơn có thể 128 Hz (tốn năng lượng hơn).
 */
#undef NETSTACK_CONF_RDC_CHANNEL_CHECK_RATE
#define NETSTACK_CONF_RDC_CHANNEL_CHECK_RATE 128

/* ===== Buffering & tiện ích debug ===== */
#ifndef QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM 16 /* tăng nếu topo lớn/nhiều control frames */
#endif
#define RIMESTATS_CONF_ENABLED 1 /* giúp thống kê retransmissions/ACKs */
#undef UIP_CONF_IPV6
#define UIP_CONF_IPV6 0
/* ===== Dự phòng chuyển nhanh sang ContikiMAC (tắt WuR) nếu cần so sánh năng lượng ===== */
/* // #undef  NETSTACK_CONF_RDC
 * // #define NETSTACK_CONF_RDC          contikimac_driver
 * // #undef  CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION
 * // #define CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION 1
 */

#endif /* PROJECT_CONF_H_ */
