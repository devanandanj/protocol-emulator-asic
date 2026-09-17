# SPDX-License-Identifier: Apache-2.0
import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, RisingEdge, ReadOnly, NextTimeStep


@cocotb.test()
async def test_step3(dut):
    """SET drives pins, OUT drives from reg bit 0, IN samples pin_in into reg."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())

    dut.ena.value = 1
    dut.ui_in.value = 0
    dut.uio_in.value = 0x08         # drive external pin 3 high (for IN test)
    dut.rst_n.value = 0
    await ClockCycles(dut.clk, 5)

    # reset state
    assert int(dut.user_project.core.pc.value) == 0

    dut.rst_n.value = 1

    pc      = dut.user_project.core.pc
    regs    = dut.user_project.core.regs
    uio_out = dut.uio_out
    uio_oe  = dut.uio_oe

    # cycle 1: LDI r0, 0x01
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 1
    assert int(regs[0].value) == 0x01

    # cycle 2: SET pin 0, val 1  → uio_out[0]=1, uio_oe[0]=1
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 2
    assert  int(uio_out.value) & 0x01
    assert  int(uio_oe.value)  & 0x01

    # cycle 3: SET pin 1, val 0  → uio_out[1]=0, uio_oe[1]=1
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 3
    assert (int(uio_out.value) >> 1) & 0x01 == 0
    assert (int(uio_oe.value)  >> 1) & 0x01 == 1

    # cycle 4: OUT pin 2, r0  → uio_out[2]=1 (r0 bit 0), uio_oe[2]=1
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 4
    assert (int(uio_out.value) >> 2) & 0x01 == 1
    assert (int(uio_oe.value)  >> 2) & 0x01 == 1

    # cycle 5: IN pin 3, r1  → r1 bit 0 = pin_in[3] = uio_in[3] = 1
    #                          uio_oe[3] released (=0)
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 5
    assert int(regs[1].value) & 0x01 == 1
    assert (int(uio_oe.value) >> 3) & 0x01 == 0

    # cycle 6: LDI r2, 0x81
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 6
    assert int(regs[2].value) == 0x81

    # cycle 7: SHIFT r2, right, 1  → 0x81 >> 1 = 0x40 (bit 0 falls off)
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 7
    assert int(regs[2].value) == 0x40

    # cycle 8: LDI r3, 0x81
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 8
    assert int(regs[3].value) == 0x81

    # cycle 9: ROT r3, right, 1   → 0x81 rot-right 1 = 0xC0 (bit 0 wraps to bit 7)
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 9
    assert int(regs[3].value) == 0xC0

    # cycle 10: DELAY 3 issues. pc advances to 10, then 2 stall cycles follow.
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 10

    # cycles 11, 12: DELAY stalls — pc frozen at 10.
    for _ in range(2):
        await RisingEdge(dut.clk); await ReadOnly()
        assert int(pc.value) == 10

    # cycle 13: WAIT (val=1) — pin_in[3]=1 matches immediately, no stall.
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 11

    # cycle 14: WAIT (val=0) — no match; timeout=5 starts 4 more stall cycles.
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 12

    # cycles 15..18: WAIT stalls — pc frozen at 12.
    for _ in range(4):
        await RisingEdge(dut.clk); await ReadOnly()
        assert int(pc.value) == 12

    # cycle 19: LDI r4, 3
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 13
    assert int(regs[4].value) == 3

    # cycles 20, 21, 22: JCND r4, 13 — decrement-and-branch back to self.
    #   r4 walks 3→2→1→0. Each iteration pc stays at 13.
    for expected_r4 in [2, 1, 0]:
        await RisingEdge(dut.clk); await ReadOnly()
        assert int(pc.value) == 13
        assert int(regs[4].value) == expected_r4

    # cycle 23: JCND r4=0 falls through, pc → 14.
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 14

    core = dut.user_project.core

    # cycle 24: LDI r5, 0xAA
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 15
    assert int(regs[5].value) == 0xAA

    # cycle 25: PUSH r5 → rx_fifo[0]=0xAA, rx_count=1
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 16
    assert int(core.rx_count.value) == 1
    assert int(core.rx_fifo[0].value) == 0xAA

    # cycle 26: IRQ 3 → irq_lines bit 3 set
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 17
    assert int(core.irq_lines.value) & (1 << 3)

    # cycle 27: LDI r7, 1
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 18
    assert int(regs[7].value) == 1

    # cycle 28: OUT_OD pin 5, r7  (bit=1 → release pin 5)
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 19
    assert (int(uio_oe.value) >> 5) & 1 == 0

    # cycle 29: LDI r7, 0
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 20
    assert int(regs[7].value) == 0

    # cycle 30: OUT_OD pin 5, r7  (bit=0 → drive pin 5 low)
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 21
    assert (int(uio_out.value) >> 5) & 1 == 0
    assert (int(uio_oe.value)  >> 5) & 1 == 1

    # cycle 31: PULL r6 — TX FIFO empty, enters stall
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 22
    assert int(core.waiting_for_pull.value) == 1

    # cycle 32: still stalling
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 22
    assert int(core.waiting_for_pull.value) == 1

    # Leave ReadOnly phase so we can write to signals (backdoor host push).
    await NextTimeStep()
    core.tx_fifo[0].value = 0xBE
    core.tx_tail.value = 1
    core.tx_count.value = 1

    # cycle 33: PULL stall service loads r6 = 0xBE, clears wait
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 22
    assert int(regs[6].value) == 0xBE
    assert int(core.waiting_for_pull.value) == 0

    # cycle 34: JMP 0 executes — wrap back
    await RisingEdge(dut.clk); await ReadOnly()
    assert int(pc.value) == 0