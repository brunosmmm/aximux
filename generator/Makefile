SIG_COUNT ?= 8
ALT_SIG_COUNT ?= 4

all: axi_mux.v axi4liteslave.v

axi_mux.v: mux_gen.py
	python mux_gen.py $(SIG_COUNT) $(ALT_SIG_COUNT) > $@

axi4liteslave.v: axislave.mm
	axi_slave_builder $< --replace-param mux_signals=$(SIG_COUNT) alt_signals=$(ALT_SIG_COUNT)\
                       --modname axi4liteslave > $@

clean:
	rm -rf axi_mux.v axi4liteslave.v
