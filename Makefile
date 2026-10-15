# smartOs host build: tests for 16/32/64-bit tick widths and the example.
# The kernel itself has no build logic; see kernel.mk for the file list.

CC      ?= cc
BUILD   := build
WARN    := -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion \
           -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror
SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer -g

include kernel.mk

HOST_SRC := platform/host/host_platform.c
TEST_SRC := test/test_main.c test/test_util.c test/test_unit.c test/test_sched.c test/test_error.c \
            test/test_ring.c test/test_supervise.c
INC      := $(SOS_INC) -Iplatform/host

WIDTHS := 16 32 64

.PHONY: all test test-san test-thread test-tsan test-gen example example-pipeline clean
all: test test-thread test-gen example

# Kernel and host platform get the strict warning set; tests a relaxed one.
define TEST_template
$(BUILD)/test_$(1): $(SOS_SRC) $(HOST_SRC) $(TEST_SRC) $(wildcard kernel/inc/*.h kernel/src/*.h platform/*.h platform/host/*.h test/*.h)
	@mkdir -p $(BUILD)/obj$(1)
	for f in $(SOS_SRC); do $(CC) $(WARN) $(EXTRA) -DSOS_PLATFORM_TICK_BITS=$(1) $(INC) -Itest \
	    -c $$$$f -o $(BUILD)/obj$(1)/$$$$(basename $$$$f .c).o || exit 1; done
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

# Thread platform: real time, producer threads as asynchronous contexts.
THREAD_INC := $(SOS_INC) -Iplatform/host_thread -Itest
THREAD_SRC := platform/host_thread/thread_platform.c
test-thread: $(BUILD)/test_thread
	./$(BUILD)/test_thread
$(BUILD)/test_thread: $(SOS_SRC) $(THREAD_SRC) test/test_thread.c $(wildcard kernel/inc/*.h kernel/src/*.h platform/*.h platform/host_thread/*.h)
	@mkdir -p $(BUILD)/objthr
	for f in $(SOS_SRC) $(THREAD_SRC); do $(CC) $(WARN) $(EXTRA) $(THREAD_INC) -c $$f \
	    -o $(BUILD)/objthr/$$(basename $$f .c).o || exit 1; done
	$(CC) -std=c11 -Wall -Wextra -Werror $(EXTRA) $(THREAD_INC) test/test_thread.c $(BUILD)/objthr/*.o -pthread -o $@

test-tsan:
	rm -rf $(BUILD)/objthr $(BUILD)/test_thread
	$(MAKE) test-thread EXTRA="-fsanitize=thread -g -O1"
	rm -rf $(BUILD)/objthr $(BUILD)/test_thread

example: $(BUILD)/manual_cfg
$(BUILD)/manual_cfg: examples/manual_cfg/main.c examples/manual_cfg/sos_cfg.h $(SOS_SRC) $(HOST_SRC)
	@mkdir -p $(BUILD)
	$(CC) $(WARN) $(INC) -Iexamples/manual_cfg $(SOS_SRC) $(HOST_SRC) examples/manual_cfg/main.c -o $@

# Generator: unit tests, then the pipeline example end to end.
PYTHON   ?= python3
SOSGEN   := $(PYTHON) tools/sosgen
PIPE_DIR := examples/layered_pipeline
PIPE_GEN := $(BUILD)/gen/pipeline
test-gen:
	$(PYTHON) -m unittest discover -s tools/sosgen -p 'test_*.py' -q
	$(MAKE) example-pipeline

$(PIPE_GEN)/sos_cfg_gen.c: $(PIPE_DIR)/app.toml tools/sosgen/sosgen.py
	$(SOSGEN) $< -o $(PIPE_GEN) --tick-bits 32 --ticks-per-sec 1000000 --report $(PIPE_GEN)/report.txt
example-pipeline: $(PIPE_GEN)/sos_cfg_gen.c
	$(CC) $(WARN) $(SOS_INC) -Iplatform/host -I$(PIPE_GEN) -I$(PIPE_DIR) $(SOS_SRC) $(HOST_SRC) \
	    $(PIPE_GEN)/sos_cfg_gen.c $(PIPE_DIR)/app.c $(PIPE_DIR)/main.c -o $(BUILD)/pipeline
	./$(BUILD)/pipeline

clean:
	rm -rf $(BUILD)
