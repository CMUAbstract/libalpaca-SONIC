EXEC = libchain.a

OBJECTS = \
	chain.o \

CONFIG_PRINTF_LIB = libmspconsole


include $(MAKER_ROOT)/Makefile.gcc
include $(MAKER_ROOT)/Makefile.console
include $(MAKER_ROOT)/Makefile.count

SRC_ROOT = ../../src

CFLAGS += \
	-I $(LIBMSP_ROOT)/src/include \
	-I $(SRC_ROOT)/include/libchain \

ifeq ($(LIBCHAIN_ENABLE_DIAGNOSTICS),1)
CFLAGS += -DLIBCHAIN_ENABLE_DIAGNOSTICS
endif

VPATH = $(SRC_ROOT)
