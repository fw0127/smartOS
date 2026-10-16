# smartOs on the Arduino Nano (ATmega328P, 16 MHz), bare metal.
#
#   make                 build contract.hex and demo.hex, print sizes
#   make flash-contract  upload the contract suite, then watch the serial port
#   make flash-demo      upload the kernel demo
#   make monitor         serial monitor (screen), 115200
#   make sim-contract    run in the simavr simulator instead (brew install simavr)
#
# Toolchain on macOS:  brew tap osx-cross/avr && brew install avr-gcc avrdude
# The Arduino IDE also ships avr-gcc and avrdude; point AVR_PREFIX at them.
#
# Clones with the old bootloader upload at 57600 (Arduino IDE: "ATmega328P
# (Old Bootloader)"); Optiboot boards at 115200: make flash-demo UPLOAD_BAUD=115200

MCU          ?= atmega328p
F_CPU        ?= 16000000UL
AVR_PREFIX   ?=
CC           := $(AVR_PREFIX)avr-gcc
OBJCOPY      := $(AVR_PREFIX)avr-objcopy
SIZE         := $(AVR_PREFIX)avr-size
AVRDUDE      ?= avrdude
PORT         ?= $(firstword $(wildcard /dev/cu.wchusbserial* /dev/cu.usbserial-* /dev/ttyUSB0))
UPLOAD_BAUD  ?= 57600

SOS_ROOT := ../..
include $(SOS_ROOT)/kernel.mk

CFLAGS := -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -std=c99 \
          -Wall -Wextra -Werror -Wno-unused-parameter \
          -ffunction-sections -fdata-sections \
          -I. $(SOS_INC) -I$(SOS_ROOT)/platform/contract_tests
LDFLAGS := -mmcu=$(MCU) -Wl,--gc-sections

BUILD := build

all: $(BUILD)/contract.hex $(BUILD)/demo.hex
	@$(SIZE) -C --mcu=$(MCU) $(BUILD)/contract.elf $(BUILD)/demo.elf

HDRS := $(wildcard *.h) $(wildcard $(SOS_ROOT)/kernel/inc/*.h) $(wildcard $(SOS_ROOT)/kernel/src/*.h) \
        $(SOS_ROOT)/platform/sos_platform.h $(SOS_ROOT)/platform/contract_tests/sos_contract_test.h

$(BUILD)/contract.elf: avr_contract.c avr_platform.c $(SOS_ROOT)/platform/contract_tests/sos_contract_test.c $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(filter %.c,$^)

$(BUILD)/demo.elf: avr_demo.c avr_platform.c $(SOS_SRC) $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(filter %.c,$^)

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

$(BUILD):
	mkdir -p $@

flash-%: $(BUILD)/%.hex
	@test -n "$(PORT)" || (echo "no serial port found; pass PORT=/dev/cu.xxx" && false)
	$(AVRDUDE) -p $(MCU) -c arduino -P $(PORT) -b $(UPLOAD_BAUD) -D -U flash:w:$<:i

monitor:
	screen $(PORT) 115200

# Run a firmware in the simavr simulator (no board needed); Ctrl-C to stop.
#   make sim-contract   expect "CONTRACT PASS" after ~4 s of simulated time
sim-%: $(BUILD)/%.elf
	simavr -m $(MCU) -f $(patsubst %UL,%,$(F_CPU)) $<

clean:
	rm -rf $(BUILD)

.PHONY: all clean monitor
