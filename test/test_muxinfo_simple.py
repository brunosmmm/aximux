"""Simple MUXINFO register test to validate AXI address decoding fix."""

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import Timer
from cocotbext.axi import AxiLiteBus, AxiLiteMaster


@cocotb.test()
async def test_muxinfo_read(dut):
    """Test reading MUXINFO register at 0x80."""
    
    # Start clock
    clock = Clock(dut.s_axi_aclk, 10, unit="ns")  # 100MHz
    cocotb.start_soon(clock.start())
    
    # Reset
    dut.s_axi_aresetn.value = 0
    await Timer(100, unit="ns")
    dut.s_axi_aresetn.value = 1
    await Timer(100, unit="ns")
    
    # Set up AXI-Lite interface
    axi_bus = AxiLiteBus.from_prefix(dut, "s_axi")
    axi_master = AxiLiteMaster(axi_bus, dut.s_axi_aclk, dut.s_axi_aresetn)
    
    dut._log.info("=== Testing MUXINFO Register Read ===")
    
    try:
        # Read MUXINFO register at 0x80
        dut._log.info("Reading MUXINFO at address 0x80...")
        resp = await axi_master.read(0x80, 4)
        
        # Check response
        if resp.resp != 0:
            dut._log.error(f"AXI read failed with response: {resp.resp}")
            assert False, f"AXI read failed with response: {resp.resp}"
        
        # Convert response to integer
        muxinfo_value = int.from_bytes(resp.data, byteorder='little')
        dut._log.info(f"MUXINFO value: 0x{muxinfo_value:08x}")
        
        # Extract fields
        sig_count = muxinfo_value & 0xFF
        alt_sig_count = (muxinfo_value >> 8) & 0xFF
        reserved = (muxinfo_value >> 16) & 0xFFFF
        
        dut._log.info(f"Signal count: {sig_count}")
        dut._log.info(f"Alt signal count: {alt_sig_count}")
        dut._log.info(f"Reserved: 0x{reserved:04x}")
        
        # Basic validation - should have non-zero signal count
        assert sig_count > 0, f"Invalid signal count: {sig_count}"
        assert sig_count <= 32, f"Signal count too high: {sig_count}"
        assert alt_sig_count > 0, f"Invalid alt signal count: {alt_sig_count}"
        assert alt_sig_count <= 15, f"Alt signal count too high: {alt_sig_count}"
        
        dut._log.info("✅ MUXINFO register test PASSED!")
        
    except Exception as e:
        dut._log.error(f"Test failed with exception: {e}")
        raise


@cocotb.test()
async def test_muxinfo_address_calculation(dut):
    """Test that the address calculation is working correctly."""
    
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
    
    dut._log.info("=== Testing Address Calculation ===")
    
    # The critical test: 0x80 should be accessible with current OPT_MEM_ADDR_BITS
    # 0x80 >> 2 = 0x20 = 32 decimal
    # With 6 bits, we can address 0-63, so 32 should be accessible
    
    addr_under_test = 0x80
    expected_case_addr = addr_under_test >> 2  # 0x20 = 32
    
    dut._log.info(f"Testing address 0x{addr_under_test:02x}")
    dut._log.info(f"Expected case address: 0x{expected_case_addr:02x} ({expected_case_addr})")
    dut._log.info(f"With 6-bit addressing, max address is {(1<<6)-1}")
    
    if expected_case_addr >= (1 << 6):
        dut._log.error(f"Address {expected_case_addr} not reachable with 6 bits!")
        assert False, "Address calculation shows MUXINFO should not be reachable"
    else:
        dut._log.info(f"Address {expected_case_addr} is reachable with 6 bits ✓")
    
    # Now try the actual read
    try:
        resp = await axi_master.read(addr_under_test, 4)
        
        if resp.resp == 0:
            value = int.from_bytes(resp.data, byteorder='little')
            dut._log.info(f"✅ Successfully read 0x{addr_under_test:02x}: 0x{value:08x}")
        else:
            dut._log.error(f"❌ Read failed with AXI response: {resp.resp}")
            assert False, f"Read failed with response {resp.resp}"
            
    except Exception as e:
        dut._log.error(f"Read attempt failed: {e}")
        raise