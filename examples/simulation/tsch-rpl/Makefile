CONTIKI            = ../../..
TARGET            ?= z1

CONTIKI_PROJECT    = example-tsch-rpl example-tsch-rpl-15 example-tsch-rpl-30

all: $(CONTIKI_PROJECT)

PROJECT_SOURCEFILES +=

MODULES += core/net/mac/tsch

CFLAGS += -DLOG_APP=1
CFLAGS += -DLOG_COLLECT=1
CFLAGS += -DLOG_TOPO=1
CFLAGS += -DLOG_WUR=0

CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

# No extra apps (PowerTracker plugin reads radio stats)

include $(CONTIKI)/Makefile.include
