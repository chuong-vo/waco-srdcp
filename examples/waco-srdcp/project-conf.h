#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

/* Bật Auto-ACK cho CC2420 + WUR RDC */
#define CC2420_CONF_AUTOACK        1
#define WURRDC_CONF_802154_AUTOACK 1
#undef NETSTACK_CONF_RDC_CHANNEL_CHECK_RATE
// default is 8
#define NETSTACK_CONF_RDC_CHANNEL_CHECK_RATE 64
/* Sử dụng Wake-up Radio RDC (WaCo) */
#undef NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC  wurrdc_driver
// #define NETSTACK_CONF_RDC  contikimac_driver
#endif /* PROJECT_CONF_H_ */
