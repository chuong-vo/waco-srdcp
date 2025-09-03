# ---- Project / Contiki ----
CONTIKI            = ../..
CONTIKI_WITH_RIME  = 1
CONTIKI_PROJECT    = example-runicast-srdcp
TARGET ?= sky

# ---- App & sources ----
all: $(CONTIKI_PROJECT)

# Nếu 3 file SRDCP nằm cùng thư mục với Makefile:
PROJECT_SOURCEFILES += my_collect.c routing_table.c topology_report.c

# CFLAG += -DENABLE_COLLECT_VIEW = 1
# CFLAG += -DLOG_TOPO = 1
# CFLAG += -DLOG_APP = 1
# CFLAG += -DLOG_COLLECT = 1
# CFLAG += -DLOG_WUR = 1 
# ---- Config ----
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

# Bật powertrace
APPS = serial-shell powertrace collect-view

include $(CONTIKI)/Makefile.include
