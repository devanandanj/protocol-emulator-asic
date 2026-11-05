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

- **16-opcode ISA** (see [docs/isa.md](docs/isa.md)) with a 12-bit PC,
  eight 8-bit general-purpose registers, and two 16-deep FIFOs on the
  host seam.
- **Verilog RTL** — `src/pemu_core.v` (the core), `src/pemu_host.v` (the
  SPI host port) and `src/project.v` (the Tiny Tapeout wrapper). 256-word
  SRAM program memory (IHP SRAM macro), loaded over the host port, which
  also carries FIFO data and run control.
- **Cycle-accurate C++ model** (`model/`) — the golden reference the
  RTL is verified against.
- **Assembler** (`asm/`) — turns `.pemu` mnemonics into 16-bit words.
- **Eight protocol programs** in `programs/`: `uart_tx.pemu`,
  `uart_rx.pemu`, `spi.pemu`, `i2c_write.pemu`, `ps2.pemu`, `jtag.pemu`,
  `swd.pemu`, `can_tx.pemu`. Every one of them
  cycle-matches the C++ model in `test/test.py`.

## Verification story

`test/test.py` drives the chip only through its pins, using the SPI
host port. Its two main tests are:

1. `test_step3` — a hand-written ISA walk that exercises every opcode
   at least once (34 clocks, direct assertions on `pc`, regs, and
   pins).
2. `test_protocols` — for each protocol program, run
   `pemu_sim` (the C++ model) with the same TX / pin-event stimulus,
   dump a per-cycle trace, then diff `(pc, regs, uio_out, uio_oe)`
   against the RTL cycle-for-cycle. Any drift fails the test.

```
uart_tx      500 cycles match
spi          400 cycles match
i2c_write   1000 cycles match
uart_rx     4400 cycles match
ps2        32510 cycles match
jtag_idcode 2780 cycles match
jtag_bypass 1940 cycles match
swd_rw      6000 cycles match
swd_wait    4500 cycles match
swd_parity  3500 cycles match
can_tx_ack  4200 cycles match
can_tx_nak  4600 cycles match
```

`test_can_frame` goes one step further: an independent CAN receiver
de-stuffs the chip's waveform and checks the CRC-15, so a stuffing or
framing bug the shared model would also make is still caught.

The full method, and what isn't covered yet, is in
[docs/verification.md](docs/verification.md).

## Layout

```
.
├── src/            Verilog RTL (Tiny Tapeout top module + core)
├── test/           cocotb testbench, protocol harness, golden traces
├── docs/           ISA spec, architecture, TT datasheet source
├── model/          C++ cycle-accurate model of the ISA (pemu_sim)
├── asm/            C++ assembler for .pemu programs
├── programs/       Verified protocol programs (uart_tx, uart_rx, spi, i2c_write, ps2, jtag, swd)
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
