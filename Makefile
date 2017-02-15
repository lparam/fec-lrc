MAJOR = 0
MINOR = 1
PATCH = 0
NAME = fec-lrc

ifdef O
ifeq ("$(origin O)", "command line")
BUILD_DIR := $(O)
endif
endif

ifneq ($(BUILD_DIR),)
saved-output := $(BUILD_DIR)

# Attempt to create a output directory.
$(shell [ -d ${BUILD_DIR} ] || mkdir -p ${BUILD_DIR})

# Verify if it was successful.
BUILD_DIR := $(shell cd $(BUILD_DIR) && /bin/pwd)
$(if $(BUILD_DIR),,$(error output directory "$(saved-output)" does not exist))
endif # ifneq ($(BUILD_DIR),)

OBJTREE	:= $(if $(BUILD_DIR),$(BUILD_DIR),$(CURDIR))
SRCTREE	:= $(CURDIR)
export SRCTREE OBJTREE

#########################################################################

ifdef HOST
CROSS_COMPILE = $(HOST)-
endif

# for OpenWrt
ifdef CROSS
CROSS_COMPILE = $(CROSS)
HOST = $(patsubst %-,%,$(CROSS_COMPILE))
endif

ifneq (,$(findstring openwrt,$(CROSS_COMPILE)))
OPENWRT = 1
endif

ifdef CROSS_COMPILE
CPPFLAGS = -DCROSS_COMPILE
endif

CFLAGS = \
	-g \
	-Os	\
	-gdwarf \
	-std=gnu99 \
	-Wall \
	$(PLATFORM_CFLAGS)

CFLAGS += -fno-omit-frame-pointer

ifneq (,$(findstring android,$(CROSS_COMPILE)))
CPPFLAGS += -DANDROID
ANDROID = 1
endif

ifneq (,$(findstring mingw32,$(CROSS_COMPILE)))
MINGW32 = 1
endif

#########################################################################

CPPFLAGS += -I3rd/lrc-erasure-code/include
CFLAGS += $(CPPFLAGS) -fPIC -shared
LIBS = $(OBJTREE)/3rd/lrc-erasure-code/src/.libs/liblrc.a
LDFLAGS = $(LIBS)

all: liblrc libfeclrc

$(OBJTREE)/3rd/lrc-erasure-code/Makefile:
	$(Q)mkdir -p $(OBJTREE)/3rd/lrc-erasure-code
	$(Q)cd $(OBJTREE)/3rd/lrc-erasure-code && $(SRCTREE)/3rd/lrc-erasure-code/configure --enable-static --host=$(HOST) LDFLAGS= && $(MAKE)

liblrc: $(OBJTREE)/3rd/lrc-erasure-code/Makefile

libfeclrc:
	gcc $(CFLAGS) src/util.c src/array.c src/fec.c -o $@.so $(LDFLAGS)
