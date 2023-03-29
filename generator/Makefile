SIG_COUNT ?= 8
ALT_SIG_COUNT ?= 4

OUTPUT_LOCATION ?= .
GENERATED_FILES := aximux__$(SIG_COUNT)__$(ALT_SIG_COUNT).v axi4liteslave__$(SIG_COUNT)__$(ALT_SIG_COUNT).v
OUTPUT_FILES := $(addprefix $(OUTPUT_LOCATION)/,$(GENERATED_FILES))

all: $(OUTPUT_FILES)

$(OUTPUT_LOCATION)/aximux__$(SIG_COUNT)__$(ALT_SIG_COUNT).v: mux_gen.py
	python mux_gen.py $(SIG_COUNT) $(ALT_SIG_COUNT) > $@

$(OUTPUT_LOCATION)/axi4liteslave__$(SIG_COUNT)__$(ALT_SIG_COUNT).v: axislave.mm
	axi_slave_builder $< --replace-param mux_signals=$(SIG_COUNT) alt_signals=$(ALT_SIG_COUNT)\
                       --modname axi4liteslave__$(SIG_COUNT)__$(ALT_SIG_COUNT) > $@

clean:
	rm -rf $(OUTPUT_FILES)
