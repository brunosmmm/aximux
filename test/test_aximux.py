"""Test axi mux."""

import cocotb
import os
import logging
from cocotbext.axi import (
    AxiLiteBus,
    AxiLiteMaster,
)
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge

REG_MUXINFO = 0x80


class AxiMuxTb:
    """Test bench."""

    def __init__(self, dut):
        """Initialize."""
        self.dut = dut

        self.log = logging.getLogger("cocotb.tb")
        self.log.setLevel(logging.DEBUG)

        s_clk = int(os.getenv("S_CLK", "10"))
        # environment should have parameters, if not we can read from register
        self.SIG_COUNT = int(os.getenv("SIG_COUNT", 8))
        self.ALT_SIG_COUNT = int(os.getenv("ALT_SIG_COUNT", 4))

        cocotb.start_soon(Clock(dut.s_axi_aclk, s_clk, units="ns").start())

        self.from_host = AxiLiteMaster(
            AxiLiteBus.from_prefix(dut, "s_axi"),
            dut.s_axi_aclk,
            dut.s_axi_aresetn,
            reset_active_level=False,
        )

    async def reset(self):
        """Reset."""
        self.dut.s_axi_aresetn.setimmediatevalue(1)
        for k in range(10):
            await RisingEdge(self.dut.s_axi_aclk)
        self.dut.s_axi_aresetn.value = 0
        for k in range(10):
            await RisingEdge(self.dut.s_axi_aclk)
        self.dut.s_axi_aresetn.value = 1
        for k in range(10):
            await RisingEdge(self.dut.s_axi_aclk)


@cocotb.test()
async def test_parameters(dut):
    """Test."""

    tb = AxiMuxTb(dut)

    await tb.reset()
    for _ in range(10):
        await RisingEdge(tb.dut.s_axi_aclk)

    ret = await tb.from_host.read(REG_MUXINFO, 4)

    # Wait a few more cycles to see if signals update
    for i in range(5):
        await RisingEdge(tb.dut.s_axi_aclk)

    sig_count = ret.data[0]
    alt_sig_count = ret.data[1]

    assert sig_count == tb.SIG_COUNT
    assert alt_sig_count == tb.ALT_SIG_COUNT


@cocotb.test()
async def test_mux(dut):
    """Test."""

    tb = AxiMuxTb(dut)

    await tb.reset()
