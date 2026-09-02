"""Simple SRCSEL register test to validate register read/write."""

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import Timer
from cocotbext.axi import AxiLiteBus, AxiLiteMaster


@cocotb.test()
async def test_srcsel_basic(dut):
    """Test basic SRCSEL register read/write."""
    
    # Start clock
    clock = Clock(dut.s_axi_aclk, 10, unit="ns")
    cocotb.start_soon(clock.start())
    
    # Reset
    dut.s_axi_aresetn.value = 0
    await Timer(100, unit="ns")
    dut.s_axi_aresetn.value = 1
    await Timer(100, unit="ns")
    
    # Set up AXI interface
    axi_bus = AxiLiteBus.from_prefix(dut, "s_axi")
    axi_master = AxiLiteMaster(axi_bus, dut.s_axi_aclk, dut.s_axi_aresetn)
    
    dut._log.info("=== Testing SRCSEL Register Read/Write ===")
    
    # Test SRCSEL0 (address 0x00)
    srcsel0_addr = 0x00
    
    # Read initial value
    dut._log.info(f"Reading initial SRCSEL0 value...")
    resp = await axi_master.read(srcsel0_addr, 4)
    assert resp.resp == 0, f"Initial read failed: {resp.resp}"
    
    initial_value = int.from_bytes(resp.data, byteorder='little')
    dut._log.info(f"Initial SRCSEL0: 0x{initial_value:08x}")
    
    # Extract fields
    src = initial_value & 0x0F
    short = bool(initial_value & 0x20)
    diren = bool(initial_value & 0x40)
    dirctl = bool(initial_value & 0x80)
    
    dut._log.info(f"  src: {src}, short: {short}, diren: {diren}, dirctl: {dirctl}")
    
    # Test write - set specific pattern
    test_value = 0xE3  # All control bits + src=3
    dut._log.info(f"Writing test value 0x{test_value:02x} to SRCSEL0...")
    
    data = test_value.to_bytes(4, byteorder='little')
    resp = await axi_master.write(srcsel0_addr, data)
    assert resp.resp == 0, f"Write failed: {resp.resp}"
    
    # Read back and verify
    dut._log.info("Reading back SRCSEL0...")
    resp = await axi_master.read(srcsel0_addr, 4)
    assert resp.resp == 0, f"Readback failed: {resp.resp}"
    
    readback_value = int.from_bytes(resp.data, byteorder='little')
    dut._log.info(f"Readback SRCSEL0: 0x{readback_value:08x}")
    
    # Verify fields
    readback_src = readback_value & 0x0F
    readback_short = bool(readback_value & 0x20)
    readback_diren = bool(readback_value & 0x40)
    readback_dirctl = bool(readback_value & 0x80)
    
    dut._log.info(f"  src: {readback_src}, short: {readback_short}, diren: {readback_diren}, dirctl: {readback_dirctl}")
    
    # Check that values match (accounting for write mask)
    expected_src = 3
    expected_short = True
    expected_diren = True
    expected_dirctl = True
    
    assert readback_src == expected_src, f"src mismatch: {readback_src} != {expected_src}"
    assert readback_short == expected_short, f"short mismatch: {readback_short} != {expected_short}"
    assert readback_diren == expected_diren, f"diren mismatch: {readback_diren} != {expected_diren}"
    assert readback_dirctl == expected_dirctl, f"dirctl mismatch: {readback_dirctl} != {expected_dirctl}"
    
    dut._log.info("✅ SRCSEL0 register test PASSED!")


@cocotb.test()
async def test_all_srcsel_registers(dut):
    """Test all SRCSEL registers."""
    
    # Start clock
    clock = Clock(dut.s_axi_aclk, 10, unit="ns")
    cocotb.start_soon(clock.start())
    
    # Reset
    dut.s_axi_aresetn.value = 0
    await Timer(100, unit="ns")
    dut.s_axi_aresetn.value = 1
    await Timer(100, unit="ns")
    
    # Set up AXI interface
    axi_bus = AxiLiteBus.from_prefix(dut, "s_axi")
    axi_master = AxiLiteMaster(axi_bus, dut.s_axi_aclk, dut.s_axi_aresetn)
    
    dut._log.info("=== Testing All SRCSEL Registers ===")
    
    # Determine signal count from MUXINFO
    resp = await axi_master.read(0x80, 4)
    muxinfo = int.from_bytes(resp.data, byteorder='little')
    sig_count = muxinfo & 0xFF
    alt_sig_count = (muxinfo >> 8) & 0xFF
    
    dut._log.info(f"Testing {sig_count} SRCSEL registers...")
    
    # Test each SRCSEL register
    for i in range(sig_count):
        srcsel_addr = i * 4  # SRCSEL registers at 0x00, 0x04, 0x08, etc.
        
        dut._log.info(f"Testing SRCSEL{i} at address 0x{srcsel_addr:02x}")
        
        # Test write/read with unique pattern for each register
        test_src = i % min(alt_sig_count, 15)  # Cycle through valid sources
        test_value = 0x60 | test_src  # short=0, diren=1, dirctl=1, src=test_src
        
        # Write
        data = test_value.to_bytes(4, byteorder='little')
        resp = await axi_master.write(srcsel_addr, data)
        assert resp.resp == 0, f"SRCSEL{i} write failed: {resp.resp}"
        
        # Read back
        resp = await axi_master.read(srcsel_addr, 4)
        assert resp.resp == 0, f"SRCSEL{i} read failed: {resp.resp}"
        
        readback = int.from_bytes(resp.data, byteorder='little')
        readback_src = readback & 0x0F
        
        assert readback_src == test_src, f"SRCSEL{i} src mismatch: {readback_src} != {test_src}"
        
        dut._log.info(f"✅ SRCSEL{i} test passed (src={readback_src})")
    
    dut._log.info(f"✅ All {sig_count} SRCSEL registers test PASSED!")


@cocotb.test()
async def test_control_bits(dut):
    """Test individual control bits."""
    
    # Start clock
    clock = Clock(dut.s_axi_aclk, 10, unit="ns")
    cocotb.start_soon(clock.start())
    
    # Reset
    dut.s_axi_aresetn.value = 0
    await Timer(100, unit="ns")
    dut.s_axi_aresetn.value = 1
    await Timer(100, unit="ns")
    
    # Set up AXI interface
    axi_bus = AxiLiteBus.from_prefix(dut, "s_axi")
    axi_master = AxiLiteMaster(axi_bus, dut.s_axi_aclk, dut.s_axi_aresetn)
    
    dut._log.info("=== Testing Control Bits ===")
    
    srcsel0_addr = 0x00
    
    # Test each control bit independently
    control_tests = [
        (0x00, "All bits clear"),
        (0x20, "SHORT bit set"),
        (0x40, "DIREN bit set"), 
        (0x80, "DIRCTL bit set"),
        (0xE0, "All control bits set")
    ]
    
    for test_value, description in control_tests:
        dut._log.info(f"Testing: {description} (0x{test_value:02x})")
        
        # Write test pattern
        data = test_value.to_bytes(4, byteorder='little')
        resp = await axi_master.write(srcsel0_addr, data)
        assert resp.resp == 0, f"Write failed for {description}"
        
        # Read back
        resp = await axi_master.read(srcsel0_addr, 4)
        assert resp.resp == 0, f"Read failed for {description}"
        
        readback = int.from_bytes(resp.data, byteorder='little')
        
        # Check bits (mask unused bits)
        readback_masked = readback & 0xEF  # Mask bit 4 (reserved)
        test_masked = test_value & 0xEF
        
        assert readback_masked == test_masked, f"Control bit test failed: expected 0x{test_masked:02x}, got 0x{readback_masked:02x}"
        
        dut._log.info(f"✅ {description} test passed")
    
    dut._log.info("✅ All control bit tests PASSED!")