"""SW direction control: DIREN/DIRCTL drive sig_dir when software control is enabled."""

import os

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import Timer
from cocotbext.axi import AxiLiteBus, AxiLiteMaster

REG_MUXINFO = 0x80
DIREN = 0x40
DIRCTL = 0x80


async def _setup(dut):
    clock = Clock(dut.s_axi_aclk, 10, unit="ns")
    cocotb.start_soon(clock.start())
    dut.s_axi_aresetn.value = 0
    await Timer(100, unit="ns")
    dut.s_axi_aresetn.value = 1
    await Timer(100, unit="ns")
    return AxiLiteMaster(
        AxiLiteBus.from_prefix(dut, "s_axi"),
        dut.s_axi_aclk,
        dut.s_axi_aresetn,
        reset_active_level=False,
    )


async def _sw_drive_all_inputs(dut, axi, sig_count, dirctl_bit=0):
    """Force every signal into SW direction so sig_dir has no Z/X bits."""
    for i in range(sig_count):
        for alt in range(5):
            try:
                getattr(dut, f"sig{i}ctl{alt}").value = 0
            except AttributeError:
                pass
        val = DIREN | (DIRCTL if dirctl_bit else 0)
        await axi.write(i * 4, val.to_bytes(4, byteorder="little"))
    await Timer(50, unit="ns")


@cocotb.test()
async def test_sw_direction_sig0(dut):
    """With DIREN=1, sig_dir[0] follows DIRCTL regardless of HW ctl pins."""
    axi = await _setup(dut)
    resp = await axi.read(REG_MUXINFO, 4)
    sig_count = resp.data[0]

    await _sw_drive_all_inputs(dut, axi, sig_count, dirctl_bit=0)

    # Drive HW ctl pins high — must be ignored while DIREN=1.
    for alt in range(5):
        try:
            getattr(dut, f"sig0ctl{alt}").value = 1
        except AttributeError:
            pass

    await axi.write(0x00, (DIREN | DIRCTL).to_bytes(4, byteorder="little"))
    await Timer(50, unit="ns")
    sig_dir = int(dut.sig_dir.value)
    assert (sig_dir >> 0) & 1 == 1, f"expected sig_dir[0]=1, got 0x{sig_dir:02x}"

    await axi.write(0x00, DIREN.to_bytes(4, byteorder="little"))
    await Timer(50, unit="ns")
    sig_dir = int(dut.sig_dir.value)
    assert (sig_dir >> 0) & 1 == 0, f"expected sig_dir[0]=0, got 0x{sig_dir:02x}"

    dut._log.info("SW direction for signal 0 PASSED")


@cocotb.test()
async def test_sw_direction_all_signals(dut):
    """Each signal's sig_dir bit follows its own SRCSEL DIRCTL when DIREN=1."""
    axi = await _setup(dut)

    resp = await axi.read(REG_MUXINFO, 4)
    sig_count = resp.data[0]
    expected = int(os.getenv("SIG_COUNT", "8"))
    assert sig_count == expected, f"MUXINFO signals {sig_count} != {expected}"

    for i in range(sig_count):
        for alt in range(5):
            try:
                getattr(dut, f"sig{i}ctl{alt}").value = 0
            except AttributeError:
                pass
        want = 1 if (i % 2) else 0
        val = DIREN | (DIRCTL if want else 0)
        await axi.write(i * 4, val.to_bytes(4, byteorder="little"))

    await Timer(50, unit="ns")
    sig_dir = int(dut.sig_dir.value)
    for i in range(sig_count):
        want = 1 if (i % 2) else 0
        got = (sig_dir >> i) & 1
        assert got == want, f"sig_dir[{i}]={got} want {want} (bus=0x{sig_dir:02x})"

    dut._log.info(f"SW direction for {sig_count} signals PASSED")
