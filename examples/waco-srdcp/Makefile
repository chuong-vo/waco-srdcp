CONTIKI = ../..
CONTIKI_WITH_RIME = 1
all: example-runicast-srdcp
PROJECT_SOURCEFILES += my_collect.c routing_table.c topology_report.c
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"
APPS += powertrace
CFLAGS += -DWUR_DEBUG=1 -DWUR_LOG_TS=1

include $(CONTIKI)/Makefile.include