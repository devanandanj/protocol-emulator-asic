# PEmu Architecture

Block diagram of the shipped v1 core. The ISA table lives in
`isa.md`; this file is the physical picture.

```
                   +----------------------------------+
                   |            PEmu core             |
                   |                                  |
   host <--PULL----|  TX FIFO (16 x 8)  --+           |
   host ---PUSH--->|  RX FIFO (16 x 8)  --+           |
   host ---PROG--->|  program mem (64 x 16)           |
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

