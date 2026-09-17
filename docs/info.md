<!---
This file is the source for the Tiny Tapeout datasheet page for this
project. TT's docs.yaml workflow validates that each required section
has real content (placeholder text is rejected).
-->

## How it works

PEmu (Protocol Emulator) is a small programmable pin-bit-banging core
being developed for the Jane Street 2027 ASIC competition. The finished
core will run short instruction programs — loaded at boot — that toggle
chip pins on a cycle-accurate schedule, so the same silicon can behave
as a UART, SPI master, or I²C master depending on which firmware image
is loaded. It follows the same idiom as the RP2040's PIO state machines
and the TI Sitara PRU cores: a purpose-built ISA for pin-level I/O
rather than general-purpose compute.

The current commit contains a **Phase 0 placeholder design**: a simple
counter whose top eight bits drive `uo[7:0]`. `ui[4:0]` sets the shift
amount used to select which counter bit lights `uo[0]`, so the blink
rate is programmable across a wide range. This lets us validate the
full RTL-to-GDS pipeline (synthesis, place-and-route, DRC, LVS) on a
trivial design before any of the real ISA logic lands. The programmable
counter+shifter also exercises the input pins so the pinout is real
rather than a stub.

The full PEmu core — 12-opcode ISA, 128-word program memory, host-side
TX/RX FIFOs, cycle-accurate C++ behavioural model — is being developed
in the same repository and will replace the placeholder in a later
commit.

## How to test

After the design is fabricated and mounted on a Tiny Tapeout dev board:

1. Power the board and select this project via the TT commander.
2. Hold `rst_n` low briefly to reset, then release.
3. `uo[0]` will toggle at a rate determined by `ui[4:0]`. With
   `ui[4:0] = 0`, `uo[0]` toggles once per clock; with `ui[4:0] = 20`,
   it toggles roughly once per second at a 50 MHz clock (period ≈
   2^21 cycles).
4. `uo[7:1]` expose the upper counter bits so a logic analyser can
   confirm the counter is advancing monotonically and no pins are
   stuck.

For pre-silicon testing, the repository ships a cocotb testbench
(`test/test.py`) driven by Icarus Verilog. From the project root:

```
cd test
make
```

produces `tb.fst` which can be viewed in GTKWave or Surfer to see
the reset behaviour and counter output.

## External hardware

None. The design is self-contained and drives its outputs directly.
Any 3.3 V logic analyser or LED can be attached to `uo[0..7]` to
observe the counter; any switch bank can drive `ui[0..4]` to change
the blink rate.
