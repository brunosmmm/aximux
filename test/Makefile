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

DUT = aximux__$(SIG_COUNT)__$(ALT_SIG_COUNT)
TOPLEVEL = $(DUT)
MODULE ?= test_muxinfo_simple
VERILOG_SOURCES += $(addprefix $(SOURCE_PATH)/,$(SOURCES))

# Default matrix modules (AXIMUX-0001)
MATRIX_MODULES = \
	test_muxinfo_simple \
	test_srcsel_simple \
	test_bidirectional_routing \
	test_sw_direction

.PHONY: test clean-sim matrix

# Prefer matrix over cocotb's default `all`/`results.xml` single-module smoke.
.DEFAULT_GOAL := test

# Wipe sim_build so a simulator major upgrade cannot leave a stale VVP behind.
clean-sim:
	@rm -rf sim_build results.xml

# Primary entry: wipe stale VVP once, then force each matrix module to run.
test: clean-sim
	@set -e; \
	fail=0; \
	for mod in $(MATRIX_MODULES); do \
		echo "==== MATRIX: $$mod ===="; \
		rm -f results.xml; \
		$(MAKE) --no-print-directory MODULE=$$mod results.xml || fail=1; \
		fails=$$(python3 -c "import xml.etree.ElementTree as ET; print(sum(1 for _ in ET.parse('results.xml').getroot().iter('failure')))"); \
		if [ "$$fails" != "0" ]; then echo "ERROR: $$mod had $$fails failure(s)"; fail=1; fi; \
	done; \
	if [ $$fail -ne 0 ]; then echo "==== AXIMUX matrix FAILED ===="; exit 1; fi
	@echo "==== AXIMUX matrix complete (SIG=$(SIG_COUNT) ALT=$(ALT_SIG_COUNT)) ===="

matrix: test

clean:: clean-sim
	@rm -rf dump.fst $(TOPLEVEL).fst __pycache__/
