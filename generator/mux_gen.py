"""Generate multiport AXI MUX."""

from argparse import ArgumentParser

from hdltools.abshdl.module import HDLModule, input_port, output_port
from hdltools.hdllib.patterns import ParallelBlock, get_multiplexer
from hdltools.abshdl.signal import HDLSignal
from hdltools.abshdl.highlvl import HDLBlock
from hdltools.verilog.codegen import VerilogCodeGenerator
from hdltools.hdllib.axi.interfaces import AXI4LiteSlaveIf
from hdltools.hdllib.axi.modules import AXI4LiteSlave
from hdltools.util import clog2

AXI_DATA_WIDTH = 32
MUX_SIGNALS = 8
AXI_ADDR_WIDTH = 7
MUX_ALT_SIGNALS = 1
MAX_ALT_SIGNALS = 15
MAX_SIGNALS = 32

if __name__ == "__main__":

    parser = ArgumentParser()
    parser.add_argument(
        "signal_count", help="number of muxed signals", type=int
    )
    parser.add_argument(
        "alt_signal_count", help="number of alternate signals", type=int
    )

    args = parser.parse_args()

    # sanity checks
    if args.alt_signal_count > MAX_ALT_SIGNALS:
        print("ERROR: alternate signals exceed maximum")
        exit(1)

    if args.signal_count > MAX_SIGNALS:
        print("ERROR: signals exceed maximum")
        exit(1)

    if args.signal_count < 1:
        print("ERROR: invalid signal count")
        exit(1)

    if args.alt_signal_count < 1:
        print("ERROR: alternate signal count must be >= 1")
        exit(1)

    def aximux_generator(mux_signals, alternate_signals):
        """Parameterized generator."""

        if alternate_signals < 1:
            raise RuntimeError("mux must have at least one alternate signal")

        # direction control ports
        dir_ports = [
            input_port(f"sig{sig}ctl{alt}")
            for sig in range(0, mux_signals)
            for alt in range(0, alternate_signals + 1)
        ]
        # incoming signal ports
        incoming_ports = [
            output_port(f"sig{sig}incoming{alt}")
            for sig in range(0, mux_signals)
            for alt in range(0, alternate_signals + 1)
        ]
        # outgoing signal ports
        outgoing_ports = [
            input_port(f"sig{sig}outgoing{alt}")
            for sig in range(0, mux_signals)
            for alt in range(0, alternate_signals + 1)
        ]

        @HDLModule(
            f"aximux__{mux_signals}__{alternate_signals}",
            ports=(
                AXI4LiteSlaveIf.instantiate(
                    "s_axi",
                    data_width=AXI_DATA_WIDTH,
                    addr_width=AXI_ADDR_WIDTH,
                ),
                input_port("sig_in", mux_signals),
                output_port("sig_out", mux_signals),
                output_port("sig_dir", mux_signals),
                dir_ports,
                incoming_ports,
                outgoing_ports,
            ),
        )
        def axi_mux(mod):
            """AXI MUX."""
            # src select and short select signals
            mod.add(
                [
                    HDLSignal(
                        "comb", f"srcsel{mux}", size=clog2(alternate_signals)
                    )
                    for mux in range(0, mux_signals)
                ]
            )
            mod.add(
                [
                    HDLSignal("comb", f"shortsel{mux}", size=1)
                    for mux in range(0, mux_signals)
                ]
            )
            # hw/sw direction control signals
            mod.add(
                [
                    HDLSignal("comb", f"diren{mux}", size=1)
                    for mux in range(0, mux_signals)
                ]
            )
            mod.add(
                [
                    HDLSignal("comb", f"dirctl{mux}", size=1)
                    for mux in range(0, mux_signals)
                ]
            )
            mod.add(
                [
                    HDLSignal("comb", f"hw_dir_sig{mux}", size=1)
                    for mux in range(0, mux_signals)
                ]
            )

            @HDLBlock(mod)
            @ParallelBlock()
            def incoming_gen(incoming, src_sel, short_sel, sig_in, index):
                """Generate incoming signal."""
                incoming = (
                    sig_in
                    if src_sel == index
                    else (sig_in if short_sel else 0)
                )

            # generate signals
            for mux in range(0, mux_signals):
                dir_sig = mod.get_signal_or_port("sig_dir")
                out_sig = mod.get_signal_or_port("sig_out")
                sel_sig = mod.get_signal_or_port(f"srcsel{mux}")
                short_sig = mod.get_signal_or_port(f"shortsel{mux}")
                diren_sig = mod.get_signal_or_port(f"diren{mux}")
                dirctl_sig = mod.get_signal_or_port(f"dirctl{mux}")
                hw_dir_sig = mod.get_signal_or_port(f"hw_dir_sig{mux}")
                _dir_ports = [
                    mod.get_signal_or_port(f"sig{mux}ctl{alt}")
                    for alt in range(0, alternate_signals + 1)
                ]
                _out_ports = [
                    mod.get_signal_or_port(f"sig{mux}outgoing{alt}")
                    for alt in range(0, alternate_signals + 1)
                ]
                _in_port = mod.get_signal_or_port("sig_in")
                incoming_signals = [
                    mod.get_signal_or_port(f"sig{mux}incoming{alt}")
                    for alt in range(0, alternate_signals + 1)
                ]

                # direction control multiplexer
                hw_dir_mux = get_multiplexer(hw_dir_sig, sel_sig, *_dir_ports)
                dir_mux = get_multiplexer(
                    dir_sig[mux], diren_sig, hw_dir_sig, dirctl_sig
                )
                mod.add([hw_dir_mux, dir_mux])

                # output signal routing
                mod.add([get_multiplexer(out_sig[mux], sel_sig, *_out_ports)])

                # input signal routing
                for idx, incoming_signal in enumerate(incoming_signals):
                    mod.extend(
                        *incoming_gen(
                            incoming=incoming_signal,
                            src_sel=sel_sig,
                            short_sel=short_sig,
                            sig_in=_in_port[mux],
                            index=idx,
                        )
                    )

            # TODO: add axi_slave instance
            extra_slave_ports = (
                [
                    output_port(
                        f"srcsel{mux}",
                        size=clog2(alternate_signals),
                    )
                    for mux in range(0, mux_signals)
                ]
                + [
                    output_port(f"shortsel{mux}")
                    for mux in range(0, mux_signals)
                ]
                + [output_port(f"diren{mux}") for mux in range(0, mux_signals)]
                + [
                    output_port(f"dirctl{mux}")
                    for mux in range(0, mux_signals)
                ]
            )
            axi_slave = AXI4LiteSlave(
                "axislave", extra_ports=extra_slave_ports
            )
            axi_slave.attach_parameter_value("C_S_AXI_ADDR_WIDTH", 6)
            axi_slave.attach_parameter_value("C_S_AXI_DATA_WIDTH", 32)
            axi_slave.connect_port("s_axi", "s_axi")
            # capitalize ports
            port_names = list(axi_slave.ports.keys())
            for port in port_names:
                axi_slave.rename_port(port, port.upper())
            for mux in range(0, mux_signals):
                axi_slave.connect_port(f"srcsel{mux}", f"srcsel{mux}")
                axi_slave.connect_port(f"shortsel{mux}", f"shortsel{mux}")
                axi_slave.connect_port(f"diren{mux}", f"diren{mux}")
                axi_slave.connect_port(f"dirctl{mux}", f"dirctl{mux}")
            mod.add_instances(axi_slave)

        return axi_mux()

    aximux = aximux_generator(args.signal_count, args.alt_signal_count)
    gen = VerilogCodeGenerator(indent=True)
    print(gen.dump_element(aximux))
