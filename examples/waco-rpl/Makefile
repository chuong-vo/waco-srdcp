CONTIKI            = ../..
TARGET            ?= sky

# Project name (build 5/15/30-node variants like SRDCP)
CONTIKI_PROJECT    = example-waco-rpl example-waco-rpl-15 example-waco-rpl-30

all: $(CONTIKI_PROJECT)

# App sources
PROJECT_SOURCEFILES +=

# Logging toggles (similar to waco-srdcp)
CFLAGS += -DLOG_APP=1
CFLAGS += -DLOG_COLLECT=1
CFLAGS += -DLOG_TOPO=1
CFLAGS += -DLOG_WUR=0

# Use our project conf
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

# Apps: keep minimal to fit Sky memory (PowerTracker plugin collects radio energy)
APPS = powertrace

include $(CONTIKI)/Makefile.include
