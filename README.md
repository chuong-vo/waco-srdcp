# WaCo × SRDCP – Ví dụ thu thập dữ liệu trên Contiki/COOJA

Tài liệu này mô tả bản tích hợp **SRDCP** (Source-Routing Data Collection
Protocol) với **WaCo** (Wake-up Radio) chạy trên Contiki OS cho nền tảng
`sky`. Bản demo phục vụ đánh giá PDR hai chiều, bảng láng giềng, thay đổi
tuyến và năng lượng (powertrace) trong mô phỏng COOJA.

- **Sink cố định**: mote ID = 1 (địa chỉ 1.0).
- **Ứng dụng chính**: thư mục `examples/waco-srdcp/`.
- **RDC mặc định**: `wurrdc_driver` để tận dụng wake-up radio; cấu hình
  ContikiMAC/TSCH được giữ lại để tham khảo.

---

## 1. Tính năng nổi bật

- **Uplink (UL) many-to-one**: tất cả node định kỳ gửi dữ liệu lên sink; sink
  tính PDR theo từng nguồn.
- **Downlink (DL) source routing**: sink xây dựng đường đi dựa trên bảng parent
  gửi kèm (piggyback/topology report) và phát unicast từng chặng tới node đích.
- **Lựa chọn parent ổn định**: beacon bị lọc theo RSSI, cập nhật PRR integer và
  áp dụng hysteresis + dwell time để hạn chế flapping.
- **Hook quan sát beacon**: ứng dụng nhận metric/RSSI/LQI/PRR để cập nhật bảng
  hàng xóm, hiển thị CSV hoặc debug.
- **Powertrace bật sẵn**: `powertrace_start()` chạy mỗi 10 giây, log dạng CSV.
- **Topology report**: phát hiện chuyển parent và tự gộp vào dữ liệu UL để sink
  cập nhật bảng định tuyến xuống.

---

## 2. Cấu trúc thư mục liên quan

```
examples/waco-srdcp/
  example-runicast-srdcp.c      # Ứng dụng UL/DL, bảng láng giềng, powertrace
  example-runicast-srdcp-15.c   # Wrapper đặt APP_NODES = 15
  example-runicast-srdcp-30.c   # Wrapper đặt APP_NODES = 30
  my_collect.c / my_collect.h   # Lõi collect SRDCP, estimator PRR, logic parent
  topology_report.c             # Quản lý piggyback/hold topology report
  routing_table.c               # Bảng parent tại sink + source route builder
  project-conf.h                # Chọn driver radio, kênh, PAN ID, log liên quan
  Makefile                      # Kéo nguồn tuỳ biến, bật các cờ log
  sim/                          # Kịch bản COOJA và script chạy batch (xem §9)

core/net/mac/wurrdc.c           # Hook LOG_WUR dựa trên printf
examples/tsch-rpl/              # Bộ ví dụ TSCH tham khảo (không kích hoạt mặc định)
```

---

## 3. Yêu cầu môi trường

- Ubuntu 20.04 trở lên (đã kiểm với 24.04).
- Java + Ant để chạy COOJA (`sudo apt install default-jdk ant`).
- Bộ công cụ MSP430 GCC phục vụ build `TARGET=sky`.
- Repo Contiki đã kèm WaCo và mã SRDCP (không cần tải thêm).

---

## 4. Hướng dẫn nhanh

### 4.1 Biên dịch firmware

```bash
cd examples/waco-srdcp
make clean
make example-runicast-srdcp.sky TARGET=sky
```

Các biến thể `example-runicast-srdcp-15.c` / `-30.c` chỉ thay giá trị
`APP_NODES` và include file gốc.

### 4.2 Khởi động COOJA

```bash
cd ../../tools/cooja
ant run
```

Trong COOJA:
1. Tạo **New Simulation** (kênh radio trùng `project-conf.h`, mặc định 26).
2. Thêm **Sky Mote**, nạp file `examples/waco-srdcp/example-runicast-srdcp.sky`.
3. Đặt mote đầu tiên ID = 1 (sink). Các mote khác là nguồn UL.
4. Mở **Mote Output / Log Listener** để theo dõi và lưu log CSV.

---

## 5. Tuỳ chỉnh cấu hình

### 5.1 Cờ log tại build time

Trong `examples/waco-srdcp/Makefile`:

- `LOG_APP=1` – bật log CSV của ứng dụng.
- `LOG_COLLECT=1` – bật log beacon/parent trong `my_collect.c`.
- `LOG_TOPO=0`, `LOG_WUR=0` – tắt mặc định (bật lại bằng
  `CFLAGS='-DLOG_TOPO=1 -DLOG_WUR=1'`).

Có thể đặt `#define LOG_WUR 1` trực tiếp trong `project-conf.h` nếu muốn cố định.

### 5.2 Tham số ứng dụng (`example-runicast-srdcp.c`)

| Ký hiệu | Mặc định | Ý nghĩa |
| --- | --- | --- |
| `APP_NODES` | 5 | Số node đích DL luân phiên |
| `MSG_PERIOD` | 30 giây | Chu kỳ UL |
| `SR_MSG_PERIOD` | 60 giây | Chu kỳ phát DL tại sink |
| `NEI_MAX` / `NEI_TOPK` | 32 / 5 | Kích thước bảng láng giềng và số entry in TOPK |
| `NEI_PRINT_PERIOD` | 60 giây | Khoảng in bảng láng giềng CSV |
| `PDR_PRINT_PERIOD` | 60 giây | Khoảng in thống kê PDR UL |
| `NEI_CREDIT_MAX`, `NEI_CREDIT_INIT` | 10 / 3 | Tham số aging cho hàng xóm |

### 5.3 Macro collect/SRDCP (`my_collect.h`, `my_collect.c`)

Tất cả có thể override bằng `CFLAGS+=-DMACRO=giá_trị`.

| Macro | Giá trị | Công dụng |
| --- | --- | --- |
| `RSSI_THRESHOLD` | -95 dBm | Bỏ qua beacon quá yếu |
| `BEACON_INTERVAL` | 16 × `CLOCK_SECOND` | Chu kỳ beacon tại sink |
| `BEACON_FORWARD_DELAY` | random 0..0,5s | Trễ chuyển tiếp beacon |
| `TOPOLOGY_REPORT_HOLD_TIME` | 5 s | Đợi piggyback topology report |
| `PRR_NEI_MAX` | 24 | Số hàng xóm theo dõi PRR |
| `PRR_IMPROVE_MIN` | 60 % | Ngưỡng PRR khi hops cải thiện |
| `PRR_ABS_MIN` | 80 % | PRR tối thiểu để tie-break |
| `PRR_HYSTERESIS` | 25 điểm % | PRR ứng viên phải hơn parent |
| `MIN_PARENT_DWELL` | 30 s | Thời gian khoá không đổi parent |
| `PARENT_TIMEOUT` | 4 × `BEACON_INTERVAL` | Đánh dấu parent stale |
| `MAX_PATH_LENGTH` | 32 | Chiều dài đường DL tối đa |
| `MAX_NODES` | 30 | Số entry bảng parent tại sink |
| `MAX_RETRANSMISSIONS` | 1 | Số lần runicast retry |

### 5.4 Cấu hình radio (`project-conf.h`)

- MAC: `csma_driver` + RDC `wurrdc_driver`, framer 802.15.4, PAN ID `0xABCD`,
  kênh RF 26.
- Bật `RIMESTATS_CONF_ENABLED`, `QUEUEBUF_CONF_NUM = 16`, `UIP_CONF_IPV6 = 0`.
- Có sẵn block cấu hình TSCH (comment) nếu muốn thử nghiệm.

---

## 6. Cơ chế chọn parent trong SRDCP

Beacon được xử lý tại `examples/waco-srdcp/my_collect.c` với các bước:

1. **Lọc RSSI** – bỏ mọi beacon có RSSI dưới `RSSI_THRESHOLD`.
2. **Estimator PRR** – cập nhật bộ đếm `expected/received` dựa trên `tx_seq` để
   tính PRR dạng số nguyên (0..100%).
3. **Tính metric** – hop ứng viên = `beacon.metric + 1`.
4. **Kiểm tra stale** – nếu quá `PARENT_TIMEOUT` không thấy beacon từ parent
   hiện tại, đánh dấu stale.

Quy tắc ra quyết định:

- **Epoch mới (seqn tăng)**
  - Chưa có parent → nhận ngay, đặt `parent_lock_until = now + MIN_PARENT_DWELL`.
  - Hops tốt hơn → chỉ đổi nếu PRR ứng viên ≥ `PRR_IMPROVE_MIN` hoặc parent cũ
    đang stale.
  - Parent stale → có thể đổi dù hops không cải thiện.
- **Cùng epoch**
  - Hops tốt hơn → giống trên, đồng thời cập nhật `metric`.
  - Hops bằng nhau → so kè PRR: ứng viên phải ≥ `PRR_ABS_MIN` và hơn parent ít
    nhất `PRR_HYSTERESIS`, đồng thời *dwell window* đã hết.
  - Hops xấu hơn → bỏ qua.
- Sau mỗi lần đổi parent, cờ `treport_hold` bật để chuẩn bị piggyback topology
  report; nếu hết thời gian hold sẽ gửi thủ công (`topology_report.c`).

Ứng dụng có thể truy vấn PRR hiện tại qua
`my_collect_prr_percent(const linkaddr_t *)` và định nghĩa hook yếu:

```c
void srdcp_app_beacon_observed(const linkaddr_t *sender,
                               uint16_t metric, int16_t rssi, uint8_t lqi);
```

Hook này được dùng trong `example-runicast-srdcp.c` để cập nhật bảng hàng xóm.

---

## 7. Log và số liệu xuất ra

- **Tag thường gặp**: `SRDCP`, `COLLECT`, `PRR`, `STAB`, `UL`, `UC`, `TOPO`,
  `BEACON`, `PIGGY`, `POWERTRACE`, `LOG_WUR` (nếu bật).
- **Log CSV**:
  - PDR UL theo từng nguồn tại sink.
  - PDR DL tại node đích (theo sequence).
  - Bảng láng giềng: hop metric, PRR, RSSI, LQI, last_seen, parent/metric hiện tại.
  - Thay đổi parent/metric/retries.
  - Powertrace mỗi 10 giây.

Để giữ powertrace chính xác, cân nhắc tắt `LOG_WUR` hoặc tăng chu kỳ in khi mô
phỏng mạng lớn.

---

## 8. Kinh nghiệm tinh chỉnh

- **Mạng tĩnh / ít di động**: giữ nguyên ngưỡng mặc định để tối ưu ổn định.
- **Mạng di động cao**: giảm `BEACON_INTERVAL`, `PARENT_TIMEOUT`,
  `MIN_PARENT_DWELL`, `PRR_IMPROVE_MIN`, `PRR_HYSTERESIS` để theo kịp thay đổi
  (đổi lại tốn overhead và dễ rung parent).
- **Mạng dày**: tăng `PRR_NEI_MAX` khi số hàng xóm > 24 để tránh estimator bị
  ghi đè liên tục.

Mỗi profile có thể build riêng bằng `CFLAGS` để so sánh.

---

## 9. Kịch bản mô phỏng & script batch

`examples/waco-srdcp/sim/` chứa:

- Kịch bản COOJA dạng random/grid/chain cho 5/15/30 node (radio range 50 m).
- Script shell `mc_*` chạy COOJA ở chế độ headless, tự lưu log và gom CSV.

Ví dụ chạy 20 seed random 15 node:

```bash
cd examples/waco-srdcp/sim
./mc_random_15nodes.sh 20
```

Kết quả được đặt trong `sim/out/…` kèm log UL/DL, neighbour và tổng hợp năng
lượng. Có thể nhân bản script để tuỳ biến số node hoặc sinh toạ độ mới.

---

## 10. Khắc phục sự cố thường gặp

- **Không thấy log WuR**: bật `LOG_WUR` ở Makefile hoặc `project-conf.h`.
- **Cảnh báo thiếu `memcpy`/kiểu số**: thêm `<string.h>` hoặc `<stdint.h>` khi
  tách module ra ngoài.
- **`undefined reference to srdcp_app_beacon_observed`**: đảm bảo ứng dụng định
  nghĩa hoặc dùng stub yếu trong `my_collect.c`.
- **Powertrace nhiễu**: giảm log chi tiết (đặc biệt `LOG_WUR`, `LOG_TOPO`) hoặc
  tăng khoảng in CSV.

---

## 11. Nguồn tham khảo & giấy phép

- WaCo: <https://github.com/waco-sim/waco>
- SRDCP: <https://github.com/StefanoFioravanzo/SRDCP>

Repo hiện tại kế thừa giấy phép của Contiki/WaCo/SRDCP. Tham khảo các file
LICENSE tương ứng đi kèm dự án gốc.

