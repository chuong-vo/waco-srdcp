#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

/* ===== Radio / 802.15.4 ===== */
#define CC2420_CONF_CHANNEL 26       /* kênh RF cho Sky/CC2420 */
#define IEEE802154_CONF_PANID 0xABCD /* PAN ID đồng nhất toàn mạng */
#define CC2420_CONF_AUTOACK 1        /* Auto-ACK ở PHY */
#define WURRDC_CONF_802154_AUTOACK 1 /* Cho wurrdc dùng Auto-ACK của 802.15.4 */

#undef NETSTACK_CONF_RDC
#undef NETSTACK_CONF_MAC
#undef NETSTACK_CONF_FRAMER
#undef NETSTACK_CONF_LLSEC
/* Keep RF on channel 26 and single-channel hopping for fairness */
#undef CC2420_CONF_CHANNEL
#define CC2420_CONF_CHANNEL 26
#undef IEEE802154_CONF_PANID
#define IEEE802154_CONF_PANID 0xABCD

#define NETSTACK_CONF_MAC csma_driver
#define NETSTACK_CONF_FRAMER framer_802154
#define NETSTACK_CONF_LLSEC nullsec_driver
// #define NETSTACK_CONF_RDC contikimac_driver
// #define NETSTACK_CONF_RDC nullrdc_driver
// #define NETSTACK_CONF_RDC cxmac_driver
#define NETSTACK_CONF_RDC wurrdc_driver

/* ===== Duty-cycling main radio (thông tin in log) =====
 * Với WuR, main radio phần lớn được bật theo WuS, nhưng driver vẫn in "channel check rate".
 * 64 Hz là hợp lý; nếu cần phản ứng nhanh hơn có thể 128 Hz (tốn năng lượng hơn).
 */
#undef NETSTACK_CONF_RDC_CHANNEL_CHECK_RATE
#define NETSTACK_CONF_RDC_CHANNEL_CHECK_RATE 128

/* ===== Buffering & tiện ích debug ===== */
#ifndef QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM 64 /* tăng nếu topo lớn/nhiều control frames */
#endif
#define RIMESTATS_CONF_ENABLED 1 /* giúp thống kê retransmissions/ACKs */
#undef WURRDC_CONF_DATA_WINDOW
#define WURRDC_CONF_DATA_WINDOW (CLOCK_SECOND / 15) /* ~100 ms nhận dữ liệu sau WuS */
#undef WURRDC_CONF_ACK_WAIT_TIME
#define WURRDC_CONF_ACK_WAIT_TIME (RTIMER_SECOND / 1250)
#undef WURRDC_CONF_AFTER_ACK_DETECTED_WAIT_TIME
#define WURRDC_CONF_AFTER_ACK_DETECTED_WAIT_TIME (RTIMER_SECOND / 1000)
#undef RSSI_THRESHOLD
#define RSSI_THRESHOLD -90
#undef PRR_ABS_MIN
#define PRR_ABS_MIN 85
#undef PRR_IMPROVE_MIN
#define PRR_IMPROVE_MIN 70
#undef CC2420_CONF_TXPOWER
#define CC2420_CONF_TXPOWER 27
#undef UIP_CONF_IPV6
#define UIP_CONF_IPV6 0
/* ===== Dự phòng chuyển nhanh sang ContikiMAC (tắt WuR) nếu cần so sánh năng lượng ===== */
/* // #undef  NETSTACK_CONF_RDC
 * // #define NETSTACK_CONF_RDC          contikimac_driver
 * // #undef  CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION
 * // #define CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION 1
 */

#endif /* PROJECT_CONF_H_ */
