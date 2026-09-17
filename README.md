# Protocol Emulator ASIC (PEmu)

A small, programmable pin-bit-banging core targeting the [Jane Street
Protocol Emulator ASIC Competition](https://blog.janestreet.com/protocol-emulator-asic-competition/).
Deadline: **January 18, 2027**. Target shuttle: **March 2027 CMOS5L**.

## Idea

Rather than hardwire a UART, an SPI master, and an I²C master as three
independent blocks, PEmu is a tiny instruction-set core purpose-built
for reading and writing pins on a cycle-precise schedule (in the
tradition of the RP2040 PIO state machines and the TI Sitara PRU).
The same silicon can then emulate UART, SPI, I²C — and, we hope,
PS/2, JTAG, or slow-USB — just by changing the program in its
instruction memory.

## Layout

```
.
├── src/            Verilog RTL (Tiny Tapeout top module lives here)
├── test/           cocotb testbenches (Icarus by default)
├── docs/           ISA spec, architecture notes, block diagrams
├── model/          C++ behavioural model of the ISA (built with CMake)
├── asm/            C++ assembler for PEmu programs
├── CMakeLists.txt  Top-level CMake for model/ + asm/
├── info.yaml       Tiny Tapeout project metadata / pinout
├── LICENSE         Apache-2.0
├── PLAN.md         Full multi-phase roadmap
└── tt-template/    Untouched upstream template kept for reference (gitignored)
```

## Status

**Phase 0 — toolchain de-risk.** The RTL in `src/project.v` is a
throwaway blinky counter; its only job is to prove the RTL→GDS flow
locally (Icarus + cocotb) and in Tiny Tapeout's GitHub Actions
(OpenLane synth, PnR, DRC/LVS). It will be replaced wholesale by the
PEmu core once the flow is green end to end.

See [PLAN.md](PLAN.md) for phases and dependencies, and
[docs/isa.md](docs/isa.md) for the (evolving) ISA sketch.

## Getting started

Build the C++ model and assembler:

```bash
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the local RTL simulation (requires `iverilog` and `cocotb`):

```bash
cd test
pip install -r requirements.txt
make
```

Push to GitHub to run the Tiny Tapeout CI (`.github/workflows/gds.yaml`).

## License

Apache-2.0. Open source is a requirement of the competition.
