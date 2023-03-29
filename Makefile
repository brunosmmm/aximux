
OUTPUT_LOCATION := $(shell pwd)/output

all: generate driver

generate:
	OUTPUT_LOCATION=$(OUTPUT_LOCATION) $(MAKE) -C generator

driver:
	$(MAKE) -C driver

clean:
	$(MAKE) -C generator clean
	$(MAKE) -C driver clean

.PHONY: generate driver clean
