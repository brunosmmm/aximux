"""Legacy thin entrypoint — prefer `make -C test` (AXIMUX-0001 matrix)."""

import cocotb
import os
import logging
from cocotbext.axi import (
    AxiLiteBus,
    AxiLiteMaster,
)
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge
from cocotb.handle import Immediate

REG_MUXINFO = 0x80


class AxiMuxTb:
    """Test bench."""

    def __init__(self, dut):
        """Initialize."""
        self.dut = dut

        self.log = logging.getLogger("cocotb.tb")
        self.log.setLevel(logging.DEBUG)

        s_clk = int(os.getenv("S_CLK", "10"))
        self.SIG_COUNT = int(os.getenv("SIG_COUNT", 8))
        self.ALT_SIG_COUNT = int(os.getenv("ALT_SIG_COUNT", 4))

        cocotb.start_soon(Clock(dut.s_axi_aclk, s_clk, unit="ns").start())

        self.from_host = AxiLiteMaster(
            AxiLiteBus.from_prefix(dut, "s_axi"),
            dut.s_axi_aclk,
            dut.s_axi_aresetn,
            reset_active_level=False,
        )

    async def reset(self):
        """Reset."""
        self.dut.s_axi_aresetn.set(Immediate(1))
        for _ in range(10):
            await RisingEdge(self.dut.s_axi_aclk)
        self.dut.s_axi_aresetn.value = 0
        for _ in range(10):
            await RisingEdge(self.dut.s_axi_aclk)
        self.dut.s_axi_aresetn.value = 1
        for _ in range(10):
            await RisingEdge(self.dut.s_axi_aclk)


@cocotb.test()
async def test_parameters(dut):
    """MUXINFO readback (kept for MODULE=test_aximux smoke)."""
    tb = AxiMuxTb(dut)
    await tb.reset()
    for _ in range(10):
        await RisingEdge(tb.dut.s_axi_aclk)

    ret = await tb.from_host.read(REG_MUXINFO, 4)
    assert ret.data[0] == tb.SIG_COUNT
    assert ret.data[1] == tb.ALT_SIG_COUNT
