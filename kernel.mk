# smartOs kernel file list: add these sources and include directories to any
# build system. The user's build must also provide on the include path:
#   sos_cfg.h           kernel parameters (may be empty)
#   sos_platform_cfg.h  platform constants, next to the platform adaptation
SOS_ROOT ?= .
SOS_SRC  := $(SOS_ROOT)/kernel/src/sos_sched.c \
            $(SOS_ROOT)/kernel/src/sos_time.c \
            $(SOS_ROOT)/kernel/src/sos_event.c \
            $(SOS_ROOT)/kernel/src/sos_error.c \
            $(SOS_ROOT)/kernel/src/sos_bitmap.c
SOS_INC  := -I$(SOS_ROOT)/kernel/inc -I$(SOS_ROOT)/platform
