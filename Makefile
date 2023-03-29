
all: generate driver

generate:
	$(MAKE) -C generator

driver:
	$(MAKE) -C driver

clean:
	$(MAKE) -C generator clean
	$(MAKE) -C driver clean

.PHONY: generate driver clean
