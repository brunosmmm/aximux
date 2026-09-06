
OUTPUT_LOCATION := $(shell pwd)/output

all: generate driver

generate:
	OUTPUT_LOCATION=$(OUTPUT_LOCATION) $(MAKE) -C generator

driver:
	$(MAKE) -C driver

# QEMU virt + aximux MMIO harness (AXIMUX-0003). First run builds QEMU + guest.
test-qemu:
	$(MAKE) -C sim/qemu test

clean:
	OUTPUT_LOCATION=$(OUTPUT_LOCATION) $(MAKE) -C generator clean
	$(MAKE) -C driver clean

.PHONY: generate driver test-qemu clean
