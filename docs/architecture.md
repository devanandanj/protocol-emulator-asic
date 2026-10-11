# PEmu Architecture

Block diagram of the shipped v1 core. The ISA table lives in
`isa.md`; this file is the physical picture.

```
                   +----------------------------------+
                   |            PEmu core             |
                   |                                  |
   host <--PULL----|  TX FIFO (16 x 8)  --+           |
   host ---PUSH--->|  RX FIFO (16 x 8)  --+           |
   host ---PROG--->|  program mem SRAM (256 x 16)     |
                   |     |                            |
                   |     v                            |
                   |    IFETCH --> DECODE --> EXEC    |
                   |                          |       |
                   |                  +-------+-----+ |
                   |                  |  reg file   | |
                   |                  |  (8 x 8)    | |
                   |                  +-------+-----+ |
                   |                          |       |
                   |                    pin mux + OE  |
                   +----------|-----------|-----------+
                              v           v
                                    uio [7:0]
```

## Loader path

The program-memory bootstrap uses the three dedicated inputs `ui[0..2]`
while `rst_n=0` and multiplexes `uio_in[7:0]` as the byte-wide data bus.
`ui[0]` (`prog_we`) is a per-byte write strobe, `ui[1]` (`prog_hi`) picks
which half of the 16-bit word to write (low first, then high commits the
word and increments the load address), and `ui[2]` (`prog_rst`) forces
the load address back to 0. During run (`rst_n=1`) all loader signals
are ignored and `uio_in` reverts to `pin_in`.

## Program memory

Program memory is IHP's `RM_IHPSG13_1P_256x16_c2_bm_bist` single-port
SRAM macro (instance `core.u_sram`). The previous 64-word flip-flop
array was about 70% of the synthesized logic. The macro holds 4x
the words in about 28,000 µm².

The SRAM has a registered read port, so the core addresses it with
`pc_next` (the PC value being written this edge), not `pc`. The
instruction for `pc` is then on `A_DOUT` for the whole cycle that
executes it. Issue stays single-cycle and matches the C++ model
cycle-for-cycle. Stalls (DELAY, WAIT, PULL) keep `pc_next == pc`, so
the SRAM simply re-reads the same word.

A loader commit is a write, so `A_DOUT` is stale afterwards. The
last reset cycle should be idle (no commit) so the SRAM reads
address 0. A one-bit `fetch_valid` flag covers hosts that skip this.
The core then spends one extra cycle re-fetching before it executes.

Chosen over an SPI-slave loader block (saves the extra state machine)
and over a memory-mapped host interface (TT gives us no wide bus). The
core is held in reset the whole time the host is streaming, so no
run-time contention on `uio_in`.

## Concurrency

Only one program runs on the core at a time. RP2040-PIO-style parallel
state machines would double the register file, PC, and stall state —
outside the tile budget for the first tapeout. If a stretch drop of
tiles becomes available, a second SM sharing program memory is the
natural v2.

## Clock

Single 50 MHz clock from the pad. No internal prescaler — timing is
firmware-controlled via `DELAY`, which is cheap and lets the same
opcode encoding work at any target clock. All inputs are registered by
their consumers before observation, so no CDC is required inside the
core.

