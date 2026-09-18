# SPDX-License-Identifier: Apache-2.0
import os
import subprocess
from pathlib import Path

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, RisingEdge, ReadOnly, NextTimeStep


PROGRAM = [
    0xC001, 0x1001, 0x1100, 0x2200, 0x4310,
    0xC281, 0x3281, 0xC381, 0xD381, 0x6003,
    0x5385, 0x5305, 0xC403, 0x840D, 0xC5AA,
    0x9500, 0xB300, 0xC701, 0xE570, 0xC700,
    0xE570, 0xA600, 0x7000,
]

# ----------------------------------------------------------------------------
def ensure_clock(dut):
    """Start a 10ns clock for this test.

    cocotb 2.x cancels background tasks at test end, so the Clock started in
    one @cocotb.test() does not survive into the next — every test must start
    its own.
    """
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())


async def preload_program(dut, words):
    """Push program words into program_mem via the loader port. Assumes rst_n=0."""
    # Pulse prog_rst (ui_in[2]) to zero load_addr.
    dut.ui_in.value = 0b0100
    await ClockCycles(dut.clk, 1)
    dut.ui_in.value = 0
    for w in words:
        # Low byte:  prog_we=1, prog_hi=0
        dut.uio_in.value = w & 0xFF
        dut.ui_in.value = 0b0001
        await ClockCycles(dut.clk, 1)
        # High byte: prog_we=1, prog_hi=1 (commits and increments)
        dut.uio_in.value = (w >> 8) & 0xFF
        dut.ui_in.value = 0b0011
        await ClockCycles(dut.clk, 1)
    dut.ui_in.value = 0


@cocotb.test()
async def test_step3(dut):
    """SET drives pins, OUT drives from reg bit 0, IN samples pin_in into reg."""
    ensure_clock(dut)

    dut.ena.value = 1
    dut.ui_in.value = 0
    dut.uio_in.value = 0
    dut.rst_n.value = 0
    await ClockCycles(dut.clk, 5)

    # reset state
    assert int(dut.user_project.core.pc.value) == 0

    # Load program while still in reset, then set pin_in[3]=1 for IN/WAIT.
    await preload_program(dut, PROGRAM)
    dut.uio_in.value = 0x08

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


# ============================================================================
# Phase 3 — per-protocol RTL vs C++ golden-trace verification.
# ============================================================================

_TEST_DIR   = Path(__file__).resolve().parent
_REPO_ROOT  = _TEST_DIR.parent
_TRACE_DIR  = _TEST_DIR / "traces"


def _find_pemu_sim():
    """Locate the built pemu_sim binary. Container build first, then CLion.

    Filter .exe on POSIX: the Windows workspace is often bind-mounted into the
    Linux container, so a CLion-built `pemu_sim.exe` looks present but fails
    to exec (FileNotFoundError from the ELF loader hitting a PE file).
    """
    env = os.environ.get("PEMU_SIM")
    if env and Path(env).exists():
        return Path(env)
    candidates = [
        _REPO_ROOT / "build" / "pemu_sim",
        _REPO_ROOT / "build" / "model" / "pemu_sim",
        _REPO_ROOT / "cmake-build-debug" / "model" / "pemu_sim",
        _REPO_ROOT / "cmake-build-debug" / "model" / "pemu_sim.exe",
    ]
    if os.name != "nt":
        candidates = [c for c in candidates if not str(c).endswith(".exe")]
    for p in candidates:
        if p.exists():
            return p
    return None


def _load_hex(path):
    words = []
    for line in Path(path).read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            words.append(int(line, 16))
    return words


def _load_trace(path):
    """Parse pemu_sim's TSV trace into a list of dicts, one per cycle row."""
    rows = []
    for line in Path(path).read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        p = line.split("\t")
        rows.append({
            "cycle":   int(p[0]),
            "pc":      int(p[1]),
            "regs":    [int(p[2 + i]) for i in range(8)],
            "pin_out": int(p[10]),
            "pin_oe":  int(p[11]),
        })
    return rows


def _gen_golden(hex_path, trace_path, cycles, tx_bytes=None, pin_events=None):
    binary = _find_pemu_sim()
    if binary is None:
        raise RuntimeError(
            "pemu_sim not found. Build the C++ model first (cmake --build build "
            "or set $PEMU_SIM to its path)."
        )
    cmd = [str(binary), str(hex_path), str(trace_path), str(cycles)]
    if tx_bytes:
        cmd.append("--tx=" + ",".join(f"0x{b:02x}" for b in tx_bytes))
    if pin_events:
        cmd.append("--pin-events=" +
                   ",".join(f"{c}:0x{m:x}" for c, m in pin_events))
    subprocess.check_call(cmd)


async def _do_reset(dut, initial_pin_mask=0):
    # Previous test/protocol may have left us in the ReadOnly phase (signal
    # writes are forbidden there); step out before touching any inputs.
    await NextTimeStep()
    dut.ena.value = 1
    dut.ui_in.value = 0
    dut.uio_in.value = initial_pin_mask & 0xFF
    dut.rst_n.value = 0
    await ClockCycles(dut.clk, 5)


async def _run_protocol(dut, name, hex_path, cycles,
                        tx_bytes=None, pin_events=None):
    """Diff RTL against pemu_sim's golden trace cycle-for-cycle."""
    dut._log.info(f"=== protocol: {name} ({cycles} cycles) ===")

    words = _load_hex(hex_path)
    events = {c: m for c, m in (pin_events or [])}
    initial_mask = events.pop(0, 0)

    # Golden trace was pre-generated at import time. Reading only — no subprocess
    # here, because a blocking Python call inside a cocotb coroutine causes
    # icarus to exit ("Simulator shut down prematurely").
    trace_path = _TRACE_DIR / f"{name}.trace.tsv"
    if not trace_path.exists():
        raise RuntimeError(
            f"Missing golden trace {trace_path}. Check that pemu_sim is built "
            f"and _PROTOCOLS at the bottom of test.py names {name!r}."
        )
    golden = _load_trace(trace_path)

    await _do_reset(dut, initial_pin_mask=initial_mask)
    await preload_program(dut, words)

    # Restore the initial pin_in mask that preload_program clobbered.
    dut.uio_in.value = initial_mask & 0xFF
    dut.rst_n.value = 1

    core = dut.user_project.core
    if tx_bytes:
        # Backdoor TX preload — must happen in the same delta as rst_n=1 so the
        # first posedge sees rst_n=1 (reset branch off) with tx state loaded.
        for i, b in enumerate(tx_bytes):
            core.tx_fifo[i].value = b
        core.tx_head.value  = 0
        core.tx_tail.value  = len(tx_bytes) & 0xF
        core.tx_count.value = len(tx_bytes)

    for i in range(cycles):
        cyc = i + 1
        if cyc in events:
            # Prior iter's `await ReadOnly()` leaves us in the ReadOnly phase;
            # step out before writing signals.
            await NextTimeStep()
            dut.uio_in.value = events[cyc] & 0xFF
        await RisingEdge(dut.clk)
        await ReadOnly()

        g = golden[cyc]
        rtl_pc      = int(core.pc.value)
        rtl_out8    = int(dut.uio_out.value)
        rtl_oe8     = int(dut.uio_oe.value)
        rtl_regs    = [int(core.regs[j].value) for j in range(8)]
        exp_out8    = g["pin_out"] & 0xFF
        exp_oe8     = g["pin_oe"]  & 0xFF
        if (rtl_pc != g["pc"] or rtl_out8 != exp_out8
                or rtl_oe8 != exp_oe8 or rtl_regs != g["regs"]):
            raise AssertionError(
                f"{name} mismatch at cycle {cyc}:\n"
                f"  golden pc={g['pc']:3d} regs={g['regs']} "
                f"out=0x{exp_out8:02x} oe=0x{exp_oe8:02x}\n"
                f"  rtl    pc={rtl_pc:3d} regs={rtl_regs} "
                f"out=0x{rtl_out8:02x} oe=0x{rtl_oe8:02x}"
            )

    dut._log.info(f"{name}: {cycles} cycles match")


def _uart_rx_events(byte, start_cycle=10, bit_period=434, rx_pin=3):
    """Schedule pin_in transitions for one 8N1 UART frame on rx_pin.

    Idle high at cycle 0, start bit (low) at start_cycle, then 8 data bits
    LSB-first spaced bit_period apart, then a stop bit (high).
    """
    idle = 1 << rx_pin
    events = [(0, idle), (start_cycle, 0)]
    for i in range(8):
        bit = (byte >> i) & 1
        events.append((start_cycle + bit_period * (i + 1), idle if bit else 0))
    events.append((start_cycle + bit_period * 9, idle))
    return events

# Protocol configs: (name, hex-file, cycles, tx-bytes, pin-events)
_PROTOCOLS = [
    ("uart_tx",   _REPO_ROOT / "programs" / "uart_tx.hex",    500, [0x55], None),
    ("spi",       _REPO_ROOT / "programs" / "spi.hex",        400, [0xA5], None),
    ("i2c_write", _REPO_ROOT / "programs" / "i2c_write.hex", 1000, [0xA0], [(0, 0x18)]),
    ("uart_rx",   _REPO_ROOT / "programs" / "uart_rx.hex",   4400, None,   _uart_rx_events(0x55)),
]


def _pre_generate_all_traces():
    """Run pemu_sim once per protocol at module import time.

    Doing this inside a cocotb coroutine (via subprocess.check_call) makes
    icarus exit with "Simulator shut down prematurely". Running at import
    time keeps the subprocess strictly before cocotb takes over the sim.
    """
    binary = _find_pemu_sim()
    if binary is None:
        return                       # Test will surface a helpful error later.
    _TRACE_DIR.mkdir(exist_ok=True)
    for name, hex_path, cycles, tx_bytes, pin_events in _PROTOCOLS:
        trace_path = _TRACE_DIR / f"{name}.trace.tsv"
        cmd = [str(binary), str(hex_path), str(trace_path), str(cycles)]
        if tx_bytes:
            cmd.append("--tx=" + ",".join(f"0x{b:02x}" for b in tx_bytes))
        if pin_events:
            cmd.append("--pin-events=" +
                       ",".join(f"{c}:0x{m:x}" for c, m in pin_events))
        subprocess.check_call(cmd)


if os.environ.get("GATES") != "yes":
    _pre_generate_all_traces()


@cocotb.test(skip=os.environ.get("GATES") == "yes")
async def test_protocols(dut):
    """RTL matches C++ golden trace for each protocol program.

    Skipped in gate-level sim: GL is ~100x slower and the gl_test CI job
    doesn't build pemu_sim / assemble .hex, so there'd be nothing to
    compare against anyway. RTL cocotb runs this on every push.
    """
    # Prior test ends in the ReadOnly phase; leave it before touching signals.
    await NextTimeStep()
    ensure_clock(dut)
    for name, hex_path, cycles, tx_bytes, pin_events in _PROTOCOLS:
        await _run_protocol(dut, name, hex_path, cycles,
                            tx_bytes=tx_bytes, pin_events=pin_events)