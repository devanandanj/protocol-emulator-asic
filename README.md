# Protocol Emulator ASIC (PEmu)

A small, programmable pin-bit-banging core targeting the [Jane Street
Protocol Emulator ASIC Competition](https://blog.janestreet.com/protocol-emulator-asic-competition/).
Deadline: **January 18, 2027**. Target shuttle: **March 2027 CMOS5L**.

## Idea

Rather than hardwire a UART, an SPI master, and an I²C master as three
independent blocks, PEmu is a tiny instruction-set core purpose-built
for reading and writing pins on a cycle-precise schedule (in the
tradition of the RP2040 PIO state machines and the TI Sitara PRU).
The same silicon emulates UART, SPI, or I²C — and, we hope, PS/2,
JTAG, or slow-USB — just by loading a different program at boot.

## What's shipped

- **15-opcode ISA** (see [docs/isa.md](docs/isa.md)) with a 12-bit PC,
  eight 8-bit general-purpose registers, and two 16-deep FIFOs on the
  host seam.
- **Verilog RTL** — `src/pemu_core.v` (the core) + `src/project.v` (the
  Tiny Tapeout wrapper). 64-word program memory loaded at boot via a
  three-wire loader on `ui[0..2]` + `uio_in`.
- **Cycle-accurate C++ model** (`model/`) — the golden reference the
  RTL is verified against.
- **Assembler** (`asm/`) — turns `.pemu` mnemonics into 16-bit words.
- **Four protocol programs** in `programs/`: `uart_tx.pemu`,
  `uart_rx.pemu`, `spi.pemu`, `i2c_write.pemu`. Every one of them
  cycle-matches the C++ model in `test/test.py`.

## Verification story

`test/test.py` runs two tests inside cocotb:

1. `test_step3` — a hand-written ISA walk that exercises every opcode
   at least once (34 clocks, direct assertions on `pc`, regs, and
   pins).
2. `test_protocols` — for each of the four protocols, run
   `pemu_sim` (the C++ model) with the same TX / pin-event stimulus,
   dump a per-cycle trace, then diff `(pc, regs, uio_out, uio_oe)`
   against the RTL cycle-for-cycle. Any drift fails the test.

```
uart_tx      500 cycles match
spi          400 cycles match
i2c_write   1000 cycles match
uart_rx     4400 cycles match
```

## Layout

```
.
├── src/            Verilog RTL (Tiny Tapeout top module + core)
├── test/           cocotb testbench, protocol harness, golden traces
├── docs/           ISA spec, architecture, TT datasheet source
├── model/          C++ cycle-accurate model of the ISA (pemu_sim)
├── asm/            C++ assembler for .pemu programs
├── programs/       Four verified protocol programs (uart_tx, uart_rx, spi, i2c_write)
├── CMakeLists.txt  Top-level CMake for model/ + asm/
├── info.yaml       Tiny Tapeout project metadata / pinout
├── PLAN.md         Multi-phase roadmap
└── LICENSE         Apache-2.0
```

## Getting started

Build the C++ model and assembler:

```bash
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

Or, without CMake:

```bash
mkdir -p build
g++ -std=c++17 -O2 -Imodel/include model/src/isa.cpp model/src/main.cpp -o build/pemu_sim
```

Run the local RTL simulation (requires `iverilog` and `cocotb`; the
supplied devcontainer has both):

```bash
cd test
make
```

`make` will invoke `pemu_sim` to regenerate the four golden traces at
import time, then run cocotb's `test_step3` and `test_protocols`.

Push to GitHub to run the Tiny Tapeout hardening flow
(`.github/workflows/gds.yaml`): OpenLane synth + PnR + DRC/LVS on the
IHP CMOS5L PDK.

## License

Apache-2.0. Open source is a requirement of the competition.
