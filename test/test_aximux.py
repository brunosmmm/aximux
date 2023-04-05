"""Test axi mux."""
import cocotb
import os
import itertools
import logging
from cocotbext.axi import (
    AxiLiteBus,
    AxiLiteMaster,
)
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge


class AxiMuxTb:
    """Test bench."""

    def __init__(self, dut):
        """Initialize."""
        self.dut = dut

        self.log = logging.getLogger("cocotb.tb")
        self.log.setLevel(logging.DEBUG)

        s_clk = int(os.getenv("S_CLK", "10"))

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
async def test_mux(dut):
    """Test."""

    tb = AxiMuxTb(dut)

    await tb.reset()
