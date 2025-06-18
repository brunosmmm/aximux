SIG_COUNT ?= 8
ALT_SIG_COUNT ?= 4

OUTPUT_LOCATION ?= .
GENERATED_FILES := aximux__$(SIG_COUNT)__$(ALT_SIG_COUNT).v axi4liteslave__$(SIG_COUNT)__$(ALT_SIG_COUNT).v aximux__$(SIG_COUNT)__$(ALT_SIG_COUNT).tcl
OUTPUT_FILES := $(addprefix $(OUTPUT_LOCATION)/,$(GENERATED_FILES))

all: $(OUTPUT_LOCATION) $(OUTPUT_FILES)

$(OUTPUT_LOCATION):
	mkdir -p $@

$(OUTPUT_LOCATION)/aximux__$(SIG_COUNT)__$(ALT_SIG_COUNT).v: mux_gen.py
	poetry run python mux_gen.py $(SIG_COUNT) $(ALT_SIG_COUNT) > $@

$(OUTPUT_LOCATION)/aximux__$(SIG_COUNT)__$(ALT_SIG_COUNT).tcl: aximux.tcl
	sed 's/%SIG_COUNT%/$(SIG_COUNT)/' $< > $@
	sed -i 's/%ALT_COUNT%/$(ALT_SIG_COUNT)/' $@

$(OUTPUT_LOCATION)/axi4liteslave__$(SIG_COUNT)__$(ALT_SIG_COUNT).v: axislave.mm
	poetry run axi_slave_builder $< --param-replace mux_signals=$(SIG_COUNT) alt_signals=$(ALT_SIG_COUNT)\
                       --modname axi4liteslave__$(SIG_COUNT)__$(ALT_SIG_COUNT) > $@

clean:
	rm -rf $(OUTPUT_FILES)
