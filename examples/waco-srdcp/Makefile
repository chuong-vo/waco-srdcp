# ---- Project / Contiki ----
CONTIKI            = ../..
CONTIKI_WITH_RIME  = 1
CONTIKI_PROJECT    = example-runicast-srdcp
TARGET ?= sky

# ---- App & sources ----
all: $(CONTIKI_PROJECT)

# Nếu 3 file SRDCP nằm cùng thư mục với Makefile:
PROJECT_SOURCEFILES += my_collect.c routing_table.c topology_report.c

# (Nếu muốn bật các flag này thì dùng CFLAGS và KHÔNG có dấu cách quanh '=')
CFLAGS += -DENABLE_COLLECT_VIEW=1
# CFLAGS += -DLOG_TOPO=1
# CFLAGS += -DLOG_APP=1
# CFLAGS += -DLOG_COLLECT=1
# CFLAGS += -DLOG_WUR=1

# ---- Config ----
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

# Bật powertrace và collect-view nếu bạn thật sự dùng
APPS = serial-shell powertrace collect-view

include $(CONTIKI)/Makefile.include
Makefile
        modified:   example-runicast-srdcp.c
        modified:   example-waco-srdcp.csc
        modified:   my_collect.c
        modified:   project-conf.h
        modified:   routing_table.c
        modified:   topology_report.c
