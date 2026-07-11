# SPDX-License-Identifier: GPL-2.0-only

SELFTEST_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
KERNEL_ROOT := $(abspath $(SELFTEST_DIR)/../../..)

CFLAGS += -D__EXPORTED_HEADERS__ -I$(KERNEL_ROOT)/include/uapi
CFLAGS += -Wall -Wextra -O2 $(KHDR_INCLUDES)

TEST_GEN_PROGS := edu_chat_test edu_chat_client

ifneq ($(wildcard ../lib.mk),)
include ../lib.mk
else
all: $(TEST_GEN_PROGS)

%: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TEST_GEN_PROGS)

.PHONY: all clean
endif
