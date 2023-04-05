include $(shell cocotb-config --makefiles)/Makefile.sim

SIG_COUNT ?= 8
ALT_SIG_COUNT ?= 4

TOPLEVEL_LANG = verilog
SOURCE_PATH = ../output
SOURCES = aximux__$(SIG_COUNT)__$(ALT_SIG_COUNT).v axi4liteslave__$(SIG_COUNT)__$(ALT_SIG_COUNT).v

SIM ?= icarus
WAVES ?= 0

COCOTB_HDL_TIMEUNIT = 1ns
COCOTB_HDL_TIMEPRECISION = 1ps

DUT      = aximux__$(SIG_COUNT)__$(ALT_SIG_COUNT)

TOPLEVEL = $(DUT)
MODULE   = test_aximux
VERILOG_SOURCES += $(addprefix $(SOURCE_PATH)/,$(SOURCES))

clean::
	@rm -rf dump.fst $(TOPLEVEL).fst
