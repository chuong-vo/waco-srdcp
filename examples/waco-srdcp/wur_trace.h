// wur_trace.h
#ifndef WUR_TRACE_H
#define WUR_TRACE_H

#include "contiki.h"
#include "core/net/linkaddr.h"
#include <stdio.h>

/* Bật/tắt log bằng cờ biên dịch:
 *   CFLAGS += -DWUR_DEBUG=1 -DWUR_LOG_TS=1
 */
#ifndef WUR_DEBUG
#define WUR_DEBUG 1
#endif

#if WUR_DEBUG
#define WUR_TS() do { \
  if(WUR_LOG_TS) printf("[t=%lu.%03lu] ", (unsigned long)clock_seconds(), \
    (unsigned long)((clock_time() % CLOCK_SECOND) * 1000UL / CLOCK_SECOND)); \
} while(0)
#else
#define WUR_TS() do {} while(0)
#endif

/* In kèm địa chỉ node hiện tại cho dễ theo dõi */
#define WUR_NODE() (linkaddr_node_addr)

/* Nhãn thống nhất cho WuR */
#if WUR_DEBUG
#define WUR_LOG(fmt, ...)   do { WUR_TS(); printf("[WuR][%02x:%02x] " fmt, \
                                      WUR_NODE().u8[0], WUR_NODE().u8[1], ##__VA_ARGS__); } while(0)
#define WUR_WARN(fmt, ...)  do { WUR_TS(); printf("[WuR][%02x:%02x][WARN] " fmt, \
                                      WUR_NODE().u8[0], WUR_NODE().u8[1], ##__VA_ARGS__); } while(0)
#define WUR_ERR(fmt, ...)   do { WUR_TS(); printf("[WuR][%02x:%02x][ERR ] " fmt, \
                                      WUR_NODE().u8[0], WUR_NODE().u8[1], ##__VA_ARGS__); } while(0)
#else
#define WUR_LOG(...)  do {} while(0)
#define WUR_WARN(...) do {} while(0)
#define WUR_ERR(...)  do {} while(0)
#endif

/* Lý do drop/không đánh thức để bạn grep nhanh */
typedef enum {
  WUR_DROP_NONE = 0,
  WUR_DROP_NO_PREAMBLE,
  WUR_DROP_ADDR_MISMATCH,
  WUR_DROP_CRC_FAIL,
  WUR_DROP_WEAK_RSSI,
  WUR_DROP_TIMEOUT,
  WUR_DROP_BUSY,
} wur_drop_reason_t;

static inline const char *wur_drop_str(wur_drop_reason_t r) {
  switch(r) {
    case WUR_DROP_NONE:          return "none";
    case WUR_DROP_NO_PREAMBLE:   return "no_preamble";
    case WUR_DROP_ADDR_MISMATCH: return "addr_mismatch";
    case WUR_DROP_CRC_FAIL:      return "crc_fail";
    case WUR_DROP_WEAK_RSSI:     return "weak_rssi";
    case WUR_DROP_TIMEOUT:       return "timeout";
    case WUR_DROP_BUSY:          return "busy";
    default:                     return "unknown";
  }
}

#endif /* WUR_TRACE_H */
