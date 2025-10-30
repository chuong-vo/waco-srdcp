CONTIKI = ../../..
TARGET ?= sky

# Project name (build 5/15/30-node variants like SRDCP)
CONTIKI_PROJECT = example-contikimac-rpl example-contikimac-rpl-15 example-contikimac-rpl-30

all: $(CONTIKI_PROJECT)

# App sources
PROJECT_SOURCEFILES +=

# Logging toggles (similar to waco-srdcp)
CFLAGS += \
  -DLOG_APP=1 \
  -DLOG_APP_MINIMAL=1 \
  -DLOG_COLLECT=0 \
  -DLOG_TOPO=0 \
  -DLOG_WUR=0
# LOG_APP_MINIMAL=1: Only log metrics (APP-UL/DL send/got, STAT,*), disable debug

# Use our project conf
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

# Apps: keep minimal to fit Sky memory (PowerTracker plugin collects radio energy)
APPS = powertrace

include $(CONTIKI)/Makefile.include

# Radio / buffers
CFLAGS += \
  -DCC2420_CONF_TXPOWER=27 \
  -DQUEUEBUF_CONF_NUM=16

# RDC tuning
CFLAGS += -DNETSTACK_CONF_RDC_CHANNEL_CHECK_RATE=32

# Giãn tải app UL/DL để giảm va chạm/tràn đệm
CFLAGS += -DMSG_PERIOD=\(30*CLOCK_SECOND\)
CFLAGS += -DSR_MSG_PERIOD=\(45*CLOCK_SECOND\)

# RPL control tweaks (optimized for faster convergence)
CFLAGS += \
  -DRPL_CONF_WITH_DIS=1 \
  -DRPL_CONF_DIS_START_DELAY=\(3*CLOCK_SECOND\) \
  -DRPL_CONF_DIS_INTERVAL=\(30*CLOCK_SECOND\) \
  -DRPL_CONF_PROBING_INTERVAL=\(30*CLOCK_SECOND\) \
  -DRPL_CONF_DAO_DELAY=0
# Optimized for faster routes: DIO min=11 (2s), DIS=20s, probing=20s, DAO delay=0

# Force MSP430 (not MSP430X) to prevent runtime errors
# This prevents compiler from generating MSP430X instructions for division

# CSMA retry/backoff
CFLAGS += \
  -DCSMA_CONF_MAX_FRAME_RETRIES=5 \
  -DCSMA_CONF_MIN_BE=1 \
  -DCSMA_CONF_MAX_BE=5

# Giữ fragmentation bật để chở SRH khi cần
CFLAGS += -DSICSLOWPAN_CONF_FRAG=1
