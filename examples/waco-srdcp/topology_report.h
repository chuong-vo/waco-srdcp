#ifndef TOPOLOGY_REPORT_H
#define TOPOLOGY_REPORT_H

#include <stdbool.h>
#include <stdint.h>
#include "core/net/linkaddr.h"
#include "my_collect.h"

/* Callback: hết thời gian hold thì phát gói topology riêng */
void topology_report_hold_cb(void *ptr);

/* Gửi topology report:
 *  - forward = 0: node tự tạo gói (pt+len ở HEADER, 1 tc ở DATA) và gửi lên parent
 *  - forward = 1: đang forward gói của child; nếu treport_hold==1 và chưa có mình
 *                 trong block DATA thì append thêm 1 tc vào DATA và tăng len ở HEADER,
 *                 sau đó gửi lên parent.
 */
void send_topology_report(my_collect_conn *conn, uint8_t forward);

/* Ở sink: đọc HEADER (pt+len), bỏ header, duyệt block DATA gồm len tree_connection,
 * vệ sinh và nạp dict (parent map) rồi in trạng thái.
 */
void deliver_topology_report_to_sink(my_collect_conn *conn);

#endif /* TOPOLOGY_REPORT_H */
