# SPDX-License-Identifier: Apache-2.0
"""
Phase 2 Step 1 sanity test.

The RTL currently implements NOP and JMP. The hardcoded program is
five NOPs followed by JMP 0 at address 5. After reset, PC should:
  cycle 1: 1  (NOP at 0)
  cycle 2: 2  (NOP at 1)
  ...
  cycle 5: 5  (NOP at 4)
  cycle 6: 0  (JMP at 5 taking us back)
  cycle 7: 1  (NOP at 0)
  ...
Once more opcodes land, this test grows or gets replaced.
"""

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, RisingEdge


async def _reset(dut):
    dut.ena.value = 1
    dut.ui_in.value = 0
    dut.uio_in.value = 0
    dut.rst_n.value = 0
    await ClockCycles(dut.clk, 5)
    dut.rst_n.value = 1
    await RisingEdge(dut.clk)


@cocotb.test()
async def test_pc_advances_and_wraps(dut):
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start())
    await _reset(dut)

    pc = dut.user_project.core.pc
    # After reset, expect PC to walk 1..5 then wrap to 0 via JMP.
    expected = [1, 2, 3, 4, 5, 0, 1, 2]
    for want in expected:
        await RisingEdge(dut.clk)
        got = int(pc.value)
        assert got == want, f"expected pc={want}, got {got}"