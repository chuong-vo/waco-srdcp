# ---- Project / Contiki ----
CONTIKI            = ../../..
CONTIKI_WITH_RIME  = 1
# Dự án mặc định; có thêm các biến thể 15/30 nút qua wrapper C files
CONTIKI_PROJECT    ?= example-contikimac-srdcp example-contikimac-srdcp-15 example-contikimac-srdcp-30
TARGET ?= sky

# ---- App & sources ----
all: $(CONTIKI_PROJECT)

# Hỗ trợ build trực tiếp các biến thể:
#   make contikimac-srdcp-15.sky TARGET=sky
#   make contikimac-srdcp-30.sky TARGET=sky

# Nếu 3 file SRDCP nằm cùng thư mục với Makefile:
PROJECT_SOURCEFILES += my_collect.c routing_table.c topology_report.c

# (Nếu muốn bật các flag này thì dùng CFLAGS và KHÔNG có dấu cách quanh '=')
# CFLAGS += -DENABLE_COLLECT_VIEW=1
CFLAGS += -DLOG_TOPO=0
CFLAGS += -DLOG_APP=1
CFLAGS += -DLOG_COLLECT=1
CFLAGS += -DLOG_WUR=0

# ---- Config ----
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

# Bật powertrace và collect-view nếu bạn thật sự dùng
APPS = serial-shell powertrace collect-view

include $(CONTIKI)/Makefile.include