# smartOs host build: tests for 16/32/64-bit tick widths and the example.
# The kernel itself has no build logic; see kernel.mk for the file list.

CC      ?= cc
BUILD   := build
WARN    := -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion \
           -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror
SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer -g

include kernel.mk

HOST_SRC := platform/host/host_platform.c
TEST_SRC := test/test_main.c test/test_util.c test/test_unit.c test/test_sched.c test/test_error.c
INC      := $(SOS_INC) -Iplatform/host

WIDTHS := 16 32 64

.PHONY: all test test-san example clean
all: test example

# Kernel and host platform get the strict warning set; tests a relaxed one.
define TEST_template
$(BUILD)/test_$(1): $(SOS_SRC) $(HOST_SRC) $(TEST_SRC) $(wildcard kernel/inc/*.h kernel/src/*.h platform/*.h platform/host/*.h test/*.h)
	@mkdir -p $(BUILD)/obj$(1)
	$(CC) $(WARN) $(EXTRA) -DSOS_PLATFORM_TICK_BITS=$(1) $(INC) -Itest -c kernel/src/sos_sched.c   -o $(BUILD)/obj$(1)/sos_sched.o
	$(CC) $(WARN) $(EXTRA) -DSOS_PLATFORM_TICK_BITS=$(1) $(INC) -Itest -c kernel/src/sos_time.c    -o $(BUILD)/obj$(1)/sos_time.o
	$(CC) $(WARN) $(EXTRA) -DSOS_PLATFORM_TICK_BITS=$(1) $(INC) -Itest -c kernel/src/sos_event.c   -o $(BUILD)/obj$(1)/sos_event.o
	$(CC) $(WARN) $(EXTRA) -DSOS_PLATFORM_TICK_BITS=$(1) $(INC) -Itest -c kernel/src/sos_error.c   -o $(BUILD)/obj$(1)/sos_error.o
	$(CC) $(WARN) $(EXTRA) -DSOS_PLATFORM_TICK_BITS=$(1) $(INC) -Itest -c kernel/src/sos_bitmap.c  -o $(BUILD)/obj$(1)/sos_bitmap.o
	$(CC) $(WARN) $(EXTRA) -DSOS_PLATFORM_TICK_BITS=$(1) $(INC) -Itest -c $(HOST_SRC)              -o $(BUILD)/obj$(1)/host_platform.o
	$(CC) -std=c99 -Wall -Wextra -Werror $(EXTRA) -DSOS_PLATFORM_TICK_BITS=$(1) $(INC) -Itest \
	    $(TEST_SRC) $(BUILD)/obj$(1)/*.o -o $$@
endef
$(foreach w,$(WIDTHS),$(eval $(call TEST_template,$(w))))

test: $(foreach w,$(WIDTHS),$(BUILD)/test_$(w))
	@for w in $(WIDTHS); do ./$(BUILD)/test_$$w || exit 1; done

test-san:
	$(MAKE) clean
	$(MAKE) test EXTRA="$(SAN)"

example: $(BUILD)/manual_cfg
$(BUILD)/manual_cfg: examples/manual_cfg/main.c examples/manual_cfg/sos_cfg.h $(SOS_SRC) $(HOST_SRC)
	@mkdir -p $(BUILD)
	$(CC) $(WARN) $(INC) -Iexamples/manual_cfg $(SOS_SRC) $(HOST_SRC) examples/manual_cfg/main.c -o $@

clean:
	rm -rf $(BUILD)
