# PEmu Architecture

Block diagram of the shipped v1 core. The ISA table lives in
`isa.md`; this file is the physical picture.

```
                   +----------------------------------+
                   |            PEmu core             |
                   |                                  |
   SPI  <-- host --|  RX FIFO (16 x 8)  --+           |
   host     port ->|  TX FIFO (16 x 8)  --+           |
   (ui/uo)      -->|  program mem SRAM (256 x 16)     |
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

## Host port

`src/pemu_host.v` is the chip's only path to program memory, the FIFOs,
the IRQ lines and run control. It is an SPI mode 0 slave, MSB first,
with SCK at most `clk/8` (6.25 MHz at 50 MHz):

| Pin     | Signal                                  |
|---------|-----------------------------------------|
| `ui[0]` | SCK                                     |
| `ui[1]` | MOSI                                    |
| `ui[2]` | CS_N (keep high when idle)              |
| `uo[0]` | MISO                                    |
| `uo[1]` | IRQ: high while any IRQ line is set     |

A frame is CS_N low, one command byte, then data bytes. Unknown
commands act as `NOP`.

| Byte   | Command | Data                                                     |
|--------|---------|----------------------------------------------------------|
| `0x00` | NOP     | none; use it to read status                              |
| `0x01` | CTRL    | one byte: bit 0 sets `run`                               |
| `0x02` | PROG    | start address, then low/high byte pairs (auto-increment) |
| `0x03` | TX      | bytes appended to the TX FIFO                            |
| `0x04` | RX      | send dummy bytes; each returns one RX FIFO byte          |
| `0x05` | IRQ     | send two dummy bytes; returns `irq[7:0]`, `irq[15:8]`    |

Every byte not returning data returns the status byte, including the
command byte itself:

```
{run, irq_pending, tx_full, rx_count[4:0]}
```

- `rst_n` halts the core and clears the FIFOs and IRQ lines. The core
  stays halted until the host sets `run`.
- Clearing `run` resets the core (PC, registers, pins) but keeps the
  FIFOs and IRQ lines, so a host can preload TX before starting and
  drain RX after stopping.
- PROG writes are dropped while running, since they would corrupt
  instruction fetch.
- TX bytes pushed while `tx_full` is set are dropped. RX bytes pushed by
  the core when RX is full are dropped too. Read only `rx_count` bytes:
  an RX read of an empty FIFO returns 0.
- IRQ clears exactly the bits it returned, so a bit raised during the
  read stays set for the next one.

Chosen over a parallel bus because `ui` has too few pins for a data byte
plus strobes, and over a separate loader because one port covers
loading and run-time data. `uio` stays free for the protocol pins.

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

While halted, `pc_next` is 0, so `A_DOUT` already holds address 0 when
`run` rises and the first instruction issues on the next clock.

## Concurrency

Only one program runs on the core at a time. RP2040-PIO-style parallel
state machines would double the register file, PC, and stall state —
outside the tile budget for the first tapeout. If a stretch drop of
tiles becomes available, a second SM sharing program memory is the
natural v2.

## Clock

Single 50 MHz clock from the pad. No internal prescaler — timing is
firmware-controlled via `DELAY`, which is cheap and lets the same
opcode encoding work at any target clock. Host port pins and protocol pins each
pass through a two-flop synchronizer.

