# WaCo × SRDCP – Runicast Example for Contiki/COOJA


## Mục tiêu
- Tích hợp **SRDCP** vào **WaCo** chạy trên Contiki (TARGET=sky).
- Thực nghiệm 2 chiều: **UL** (nodes → sink) và **DL** (sink → node chọn) dùng **source routing**.
- Thu **metrics phục vụ báo cáo**: PDR UL/DL, bảng láng giềng (hop/metric/RSSI/LQI), thay đổi tuyến/parent/metric/retries, **powertrace** (năng lượng).
- Hook **beacon SRDCP** để thống kê metric ở phía node.

---

## 1) Tính năng chính
- **UL (many-to-one)**: node gửi dữ liệu lên sink (1.0). Sink tính PDR-UL theo từng nguồn.
- **DL (one-to-one source-route)**: sink xây tuyến bằng `routing_table.c` và gửi xuống node đích; node đích tính PDR-DL theo seq.
- **Energy**: `powertrace_start()` bật mặc định (chu kỳ 10s).
- **Log CSV qua `printf`** (để Log Listener lưu file):
  - `UL`: PDR tại sink, theo source.
  - `DL`: PDR tại node đích, theo dest/seq.
  - Bảng láng giềng: **hops ↑, RSSI ↓, last_seen ↓**.
- **Wake-up Radio**: có thể bật log WUR (mặc định tắt để tránh nhiễu powertrace).

---

## 2) Cấu trúc thư mục & các file đã chỉnh
```
examples/waco-srdcp/
  example-runicast-srdcp.c    # App UL/DL + log CSV + powertrace
  my_collect.c / my_collect.h # API collect + hook quan sát beacon
  topology_report.c           # In bảng láng giềng/topology
  routing_table.c             # Xây định tuyến nguồn (source route) cho DL
  project-conf.h              # Chọn wurrdc_driver, channel, log toggles, ...

core/net/mac/
  wurrdc.c                    # LOG_WUR macro (không dùng wur_trace.h)
```

> **Sink mặc định:** **Node ID = 1** trong COOJA → địa chỉ **1.0**.  
> App có thể dùng `sink_addr` (khai báo trong `my_collect.h`).

---

## 3) Yêu cầu môi trường
- Ubuntu 20.04+ (đã test với 24.04).
- Java + Ant để chạy **COOJA** (`tools/cooja`).
- MSP430 toolchain (build cho TARGET=sky).
- Contiki có tích hợp WaCo

---

## 4) Build & Chạy
```bash
# 1) Về thư mục ví dụ
cd examples/waco-srdcp

# 2) Build cho sky
make clean && make example-runicast-srdcp.sky TARGET=sky

# 3) Mở COOJA
cd ../../tools/cooja && ant run
```

Trong COOJA:
1. Tạo **New Simulation** (radio channel khớp `project-conf.h`, ví dụ 26).
2. **Add motes** → **Sky Mote** → nạp firmware `examples/waco-srdcp/example-runicast-srdcp.sky`.
3. Đặt mote đầu tiên **Node ID = 1** (sink 1.0).
4. Thêm các mote thường: 2.0, 3.0, …
5. Mở **Mote Output / Log Listener** và **Radio Messages** để theo dõi & lưu log CSV.

---

## 5) Tuỳ chọn cấu hình (quan trọng)
- **Bật/Tắt log** (mặc định **tắt** để không nhiễu powertrace):
- **Ví dụ**
  - Trong `project-conf.h` thêm:
    ```c
    #define LOG_WUR 1
    ```
  - Hoặc tại dòng lệnh:
    ```bash
    make example-runicast-srdcp.sky TARGET=sky CFLAGS='-DLOG_WUR=1'
    ```
- **Các macro log hỗ trợ**
  - `LOG_TOPO:` log báo cáo topology (neighbor table, đường đi, route info).
  - `LOG_APP:` log ứng dụng (luồng dữ liệu từ app, sự kiện chính trong demo).
  - `LOG_COLLECT:` log dữ liệu thu thập (upward traffic từ node → sink).
  - `LOG_WUR:` log hoạt động Wake-up Radio (bật/tắt, gói wake-up).
- **Kỳ gửi & số node DL**: trong `example-runicast-srdcp.c`
  - `MSG_PERIOD` (chu kỳ gửi UL)
  - `SR_MSG_PERIOD`
  - `APP_NODES` (số node quay vòng DL hoặc danh sách đích)
- **Độ dài đường đi tối đa**: `MAX_PATH_LENGTH` trong `routing_table.c`.
- **Chu kỳ in bảng láng giềng / PDR**: `NEI_PRINT_PERIOD`, `PDR_PRINT_PERIOD`.

---

## 6) Ghi log & thu thập số liệu
- **Powertrace**: đã bật trong app (chu kỳ mặc định 10s).  
  Lưu log qua **Log Listener → Save to file** để phân tích năng lượng.
- **Tag log** (gợi ý lọc):
  - `SRDCP`, `UC` (runicast), `COLLECT` (UL), `TOPO` (neighbor/topology), `UL` (tổng hợp UL), `BEACON` (quan sát beacon), `SRDCP/PIGGY` (nếu có gói piggyback).
- **Đầu ra tối thiểu cho báo cáo**:
  - Bảng PDR-UL theo từng source tại sink.
  - Bảng PDR-DL theo từng dest tại node đích (tính theo seq).
  - Bảng láng giềng có **hop(metric)/RSSI/LQI/last_seen**.
  - Log thay đổi tuyến/parent/metric/retries.
  - Bảng powertrace tổng hợp theo thời gian.

---

## 7) Quy trình thí nghiệm mẫu (gợi ý)
1. **Baseline**: LOG_WUR=0, N=5 nodes, `MSG_PERIOD = 30 s`, kênh 26, TX power mặc định.
2. **Bật WUR log**: LOG_WUR=1 → so sánh độ nhiễu log lên powertrace, điều chỉnh kỳ in nếu cần.
3. **Tăng mật độ**: N=10–15 nodes, giữ `MSG_PERIOD`, đo PDR/energy.
4. **Nhiễu kênh**: đổi kênh hoặc chèn interferer (tuỳ setup) để đo PDR/parent switch.
5. **Tối ưu**: thay đổi `MAX_PATH_LENGTH`, `NEI_PRINT_PERIOD`, `PDR_PRINT_PERIOD` để cân bằng log/độ mượt.

---

## 9) Sơ đồ

### 9.1 Kiến trúc bậc cao
<img width="860" height="584" alt="image" src="https://github.com/user-attachments/assets/68d432d0-77c6-4209-8037-3f25ced58fa9" />


### 9.2 Trình tự UL/DL + Wake-up
<img width="633" height="605" alt="image" src="https://github.com/user-attachments/assets/3ef0e478-eadd-4e0c-bf16-b1f4012475fe" />


---

## 10) Lỗi thường gặp & khắc phục
- **Không thấy log WUR** → bật `LOG_WUR` (mục 5).
- **Cảnh báo `implicit declaration of memcpy`** → thêm `#include <string.h>`.
- **Thiếu kiểu `uint16_t/uint8_t`** → thêm `#include <stdint.h>`.
- **`undefined reference to srdcp_app_beacon_observed`** → đảm bảo app định nghĩa hoặc thêm **stub yếu** trong `my_collect.c`.
- **PDR/Powertrace nhiễu do log quá nhiều** → tắt `LOG_WUR` hoặc tăng chu kỳ in `NEI_PRINT_PERIOD`/`PDR_PRINT_PERIOD`.

---

## 12) Nguồn tham khảo
- WaCo: https://github.com/waco-sim/waco  
- SRDCP: https://github.com/StefanoFioravanzo/SRDCP  
