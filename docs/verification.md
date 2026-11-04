# PEmu Verification

How PEmu is checked, what that proves, and where the holes are. Numbers here
come from `ctest` and `cd test && make`.

## Method

Three independent references check the design, because each one misses
things the others catch.

| Check                | Compares                             | Catches                         |
|----------------------|--------------------------------------|---------------------------------|
| Golden trace         | RTL vs C++ model, every cycle        | RTL differing from the ISA      |
| Far-end device model | Chip's pins vs a Python device       | Programs that break the protocol|
| Pins-only host path  | Data read back over the SPI port     | Features unreachable on silicon |

**Golden trace.** The C++ model (`model/`) is written first and is the
reference for the ISA. `pemu_sim` dumps a per-cycle trace, and cocotb compares
`(pc, regs, uio_out, uio_oe)` against the RTL on every clock.

A trace match alone can't prove a program is right, since the RTL and model
run the same program and can share its mistake. So the protocols with a
far-end model also assert on what the host receives.

**Far-end device models** (`test/test.py`) are written from the protocol specs
and don't share code with the programs:

- PS/2: a keyboard sending good, bad-parity and bad-stop frames.
- JTAG: a 16-state TAP with IDCODE and BYPASS. It starts outside reset, so the
  host's TMS stream has to bring it there.
- SWD: a target with DP registers, line reset, WAIT and bad parity. It checks
  request framing, write parity and bus contention.

JTAG and SWD devices react to the chip's real output. TCK doesn't depend on
TDO, so JTAG needs one model run to get the waveform. SWD replies change the
chip's path, so the test repeats model run and target replay until the pin
events stop changing.

**Pins-only host path.** Tests send all data through the SPI host port. They
still read internal signals to check state, but never write them. This rule
exists because of a real bug: tests once wrote the FIFOs directly, which hid
that no pin could reach them. Synthesis had deleted the FIFOs, and only a
flip-flop count in the netlist showed it.

## Checking the tests

A passing test proves little until it has been seen to fail. Each new feature
was tested by breaking it on purpose and confirming a test failed. These are
manual runs, not an automated suite.

| Area        | Deliberate breaks                                                 |
|-------------|-------------------------------------------------------------------|
| Host port   | Write while running, pop at load, no TX overflow guard            |
| ALU/skips   | `SKP` delay, `SKEQ` never skips, `ADD` as `OR`                    |
| Synchronizer| Raw pin in `IN`, `WAIT` at issue, `WAIT` while stalled            |
| PS/2        | No parity check, no stop check, reversed bits                     |
| JTAG        | TDI from wrong register, bytes swapped, 7-bit chunks              |
| SWD         | Write parity, no turnaround, early drive, read parity, short reset|

Three of these first passed, which exposed test gaps: `ext_test` never
changed a pin as `WAIT` issued, the SWD test word had even parity, and the SWD
target ignored bus contention. All three are fixed.

## Coverage

| Test                 | Covers                                                       |
|----------------------|--------------------------------------------------------------|
| `test_step3`         | Opcode walk, FIFO drain, IRQ read and clear                  |
| `test_sram_prog_mem` | Code above address 64, writes refused while running, restart |
| `test_host_port`     | Status byte, TX full and overflow, unknown commands, cut frames, FIFO order |
| `test_protocols`     | Every shipped program against its golden trace               |
| `test_gl_smoke`      | Gate-level netlist: load and run through the pins            |
| `ctest`              | Model and assembler unit tests                               |

## Known holes

- **No constrained-random tests.** Every program is hand-written.
- **No formal proofs.** SymbiYosys properties are planned.
- **No coverage report.** Which opcodes and branches ran isn't measured.
- **Gate-level sim is a smoke test.** Protocols run on RTL only.
- **uart_tx, spi and i2c_write have no far-end model.** Only their golden
  traces check them.
- **Far-end models come from the same author as the programs.** A shared
  misreading of a spec would pass. Nothing has run against real hardware.
- **Timing is not simulated.** The SRAM runs a zero-delay model, and the
  SCK limit of `clk/8` is checked at that limit only, in simulation.
- **Sampling is not metastability-proof.** Two-flop synchronizers make
  failure very unlikely, not impossible.
- **Long host streams are untested.** Tests preload at most the 16-byte TX
  FIFO before starting.
