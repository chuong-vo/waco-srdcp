# ---- Project / Contiki ----
CONTIKI            = ../..
CONTIKI_WITH_RIME  = 1
CONTIKI_PROJECT    = example-runicast-srdcp
TARGET ?= sky

# ---- App & sources ----
all: $(CONTIKI_PROJECT)

# Nếu 3 file SRDCP nằm cùng thư mục với Makefile:
PROJECT_SOURCEFILES += my_collect.c routing_table.c topology_report.c

# Nếu bạn để SRDCP trong thư mục con (ví dụ ./srdcp/), bật 2 dòng dưới và
# đổi đường dẫn tệp cho đúng:
# PROJECTDIRS        += srdcp
# PROJECT_SOURCEFILES += srdcp/my_collect.c srdcp/routing_table.c srdcp/topology_report.c

# ---- Config ----
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

# Bật powertrace
APPS = serial-shell powertrace collect-view

include $(CONTIKI)/Makefile.include
