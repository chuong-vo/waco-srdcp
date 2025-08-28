#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

/* ===== Radio / 802.15.4 ===== */
#define CC2420_CONF_CHANNEL 26       /* kênh RF cho Sky/CC2420 */
#define IEEE802154_CONF_PANID 0xABCD /* PAN ID đồng nhất toàn mạng */
#define CC2420_CONF_AUTOACK 1        /* Auto-ACK ở PHY */
#define WURRDC_CONF_802154_AUTOACK 1 /* Cho wurrdc dùng Auto-ACK của 802.15.4 */

/* ===== Stack selection ===== */
#undef NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC wurrdc_driver /* WaCo: Wake-up RDC */
// #define NETSTACK_CONF_RDC contikimac_driver
#undef NETSTACK_CONF_MAC
#define NETSTACK_CONF_MAC csma_driver /* CSMA cho main radio */
#undef NETSTACK_CONF_FRAMER
#define NETSTACK_CONF_FRAMER framer_802154 /* khung 802.15.4 */
#undef NETSTACK_CONF_LLSEC
#define NETSTACK_CONF_LLSEC nullsec_driver /* không mã hóa, dễ debug */

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

/* ===== Dự phòng chuyển nhanh sang ContikiMAC (tắt WuR) nếu cần so sánh năng lượng ===== */
/* // #undef  NETSTACK_CONF_RDC
 * // #define NETSTACK_CONF_RDC          contikimac_driver
 * // #undef  CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION
 * // #define CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION 1
 */

#endif /* PROJECT_CONF_H_ */
