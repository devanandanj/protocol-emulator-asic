# PEmu Architecture — Sketch

This will grow into the real block-diagram doc during Phase 1. For
now it captures the intended shape so ISA discussions have
a physical picture to attach to.

```
                   +----------------------------------+
                   |            PEmu core             |
                   |                                  |
   host <--PULL----|  TX FIFO ----+                   |
   host ---PUSH--->|  RX FIFO ----+                   |
   host ---PROG--->|  program mem (64–128 x 16)       |
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
                       ui_in [7:0]   uo_out [7:0], uio [7:0]
```

## Open architectural decisions

- Program-memory loading path (bootstrap SPI-slave vs bit-serial vs
  memory-mapped). Cheapest silicon: bit-serial shift-in on a
  dedicated bidir pin at reset.
- Whether to expose a second, tiny state machine that can run in
  parallel with the main one (à la RP2040 PIO's 4 SMs sharing
  program memory). Almost certainly out of area budget for v1.
- Clock strategy: single clock from `clk`, with an internal
  prescaler; no CDC needed if all inputs are registered on entry.
