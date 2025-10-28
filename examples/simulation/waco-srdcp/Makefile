# ---- Project / Contiki -----------------------------------------------------
CONTIKI           = ../../..
CONTIKI_WITH_RIME = 1

# Dự án mặc định; có thêm các biến thể 15/30 nút qua wrapper C files
CONTIKI_PROJECT ?= example-waco-srdcp example-waco-srdcp-15 example-waco-srdcp-30
TARGET          ?= sky

# ---- App & sources ---------------------------------------------------------
all: $(CONTIKI_PROJECT)

# Hỗ trợ build trực tiếp các biến thể:
#   make example-waco-srdcp-15.sky TARGET=sky
#   make example-waco-srdcp-30.sky TARGET=sky

# Nếu 3 file SRDCP nằm cùng thư mục với Makefile
PROJECT_SOURCEFILES += my_collect.c routing_table.c topology_report.c

# ---- Logging toggles -------------------------------------------------------
# CFLAGS += -DENABLE_COLLECT_VIEW=1
CFLAGS += \
  -DLOG_TOPO=1 \
  -DLOG_APP=1 \
  -DLOG_COLLECT=1 \
  -DLOG_WUR=0

# ---- Global config ---------------------------------------------------------
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

# Không kéo thêm ứng dụng phụ trợ mặc định
APPS = powertrace

# Parent/lựa chọn link: tránh flapping, yêu cầu PRR tốt hơn khi đổi
CFLAGS += \
  -DPRR_ABS_MIN=85 \
  -DPRR_IMPROVE_MIN=65 \
  -DPRR_HYSTERESIS=30

# Lọc RSSI thấp (beacon) sớm
CFLAGS += -DRSSI_THRESHOLD=-90

# Giữ parent tối thiểu 60s để ổn định cây
CFLAGS += -DMIN_PARENT_DWELL=\(60*CLOCK_SECOND\)
CFLAGS += -DBEACON_INTERVAL=\(30*CLOCK_SECOND\)

# Định tuyến DL từ đồ thị piggyback: bỏ cạnh PRR thấp, ưu tiên PRR mạnh hơn
CFLAGS += \
  -DSRDCP_GRAPH_MIN_PRR=60 \
  -DSRDCP_GRAPH_PRR_WEIGHT=8 \
  -DSRDCP_GRAPH_LOAD_WEIGHT=2

# MAC/RDC tuning
CFLAGS += -DNETSTACK_CONF_RDC_CHANNEL_CHECK_RATE=32
CFLAGS += \
  -DCSMA_CONF_MAX_FRAME_RETRIES=5 \
  -DCSMA_CONF_MIN_BE=1 \
  -DCSMA_CONF_MAX_BE=5

# Buffer + TX power
CFLAGS += \
  -DQUEUEBUF_CONF_NUM=16 \
  -DCC2420_CONF_TXPOWER=27

# Giảm overhead control/data: piggyback neighbors gọn + giảm nhẹ tải ứng dụng
CFLAGS += -DSRDCP_PIGGY_MAX_NEIGHBORS=4
CFLAGS += -DMSG_PERIOD=\(30*CLOCK_SECOND\)
CFLAGS += -DSR_MSG_PERIOD=\(45*CLOCK_SECOND\)

include $(CONTIKI)/Makefile.include
