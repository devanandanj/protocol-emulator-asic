# SPDX-License-Identifier: Apache-2.0
"""
Phase 0 sanity tests for the blinky counter.

These tests just verify that:
  - reset holds the counter at 0
  - the counter increments each cycle
  - ui_in selects which counter bit is exposed on uo_out[0]

Once we swap the blinky for the real PEmu core, this file will be
replaced with per-protocol testbenches (uart_tx, uart_rx, spi, i2c).
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
async def test_reset_zeros_output(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    dut.ena.value = 1
    dut.ui_in.value = 0
    dut.uio_in.value = 0
    dut.rst_n.value = 0
    await ClockCycles(dut.clk, 5)
    # During reset the counter is 0, so uo_out[0] (bit 0 of counter) is 0.
    assert int(dut.uo_out.value) & 0x01 == 0


@cocotb.test()
async def test_bit0_toggles_every_cycle(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await _reset(dut)
    dut.ui_in.value = 0  # tap = bit 0
    prev = int(dut.uo_out.value) & 0x01
    flips = 0
    for _ in range(16):
        await RisingEdge(dut.clk)
        cur = int(dut.uo_out.value) & 0x01
        if cur != prev:
            flips += 1
        prev = cur
    # Bit 0 of a free-running counter flips every cycle.
    assert flips >= 14, f"bit 0 barely toggled ({flips} flips in 16 cycles)"


@cocotb.test()
async def test_higher_tap_toggles_slower(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await _reset(dut)
    dut.ui_in.value = 3  # tap = bit 3 -> toggles every 8 cycles
    await RisingEdge(dut.clk)
    prev = int(dut.uo_out.value) & 0x01
    flips = 0
    for _ in range(64):
        await RisingEdge(dut.clk)
        cur = int(dut.uo_out.value) & 0x01
        if cur != prev:
            flips += 1
        prev = cur
    # Expect ~64/8 = 8 flips, allow slack for reset alignment.
    assert 4 <= flips <= 12, f"bit 3 flipped {flips} times (expected ~8)"
