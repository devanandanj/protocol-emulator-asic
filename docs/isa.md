# PEmu ISA — Working Draft (Phase 1 seed)

Status: **sketch, not frozen**. This document exists so Phase 1 has
somewhere to accumulate decisions instead of scattering them across
issues. Everything here is negotiable until the C++ model
successfully executes UART, SPI, and I²C programs.

## Design goals

1. Emulate UART, SPI, and I²C from firmware; leave headroom for
   PS/2, slow USB, or JTAG as stretch protocols.
2. Fit into 8×4 Tiny Tapeout tiles (~32K logic cells) *including*
   register file, program memory, pin mux, and host interface.
3. Cycle-precise timing so bit periods and setup/hold windows are
   deterministic from the outside.
4. Small enough that a hand-written program for a simple protocol
   fits in ≤32 instructions (matches RP2040 PIO per state machine).

## Non-goals

- Being a general-purpose CPU. If a protocol needs arbitrary
  compute it can offload to the host — PEmu just clocks the wire.
- Multi-master arbitration in silicon. Handle at the program
  level for the protocols we care about.

## Reference points

| Design         | Word width | Program size | Instructions | Notes                       |
|----------------|-----------:|-------------:|-------------:|-----------------------------|
| RP2040 PIO     | 16 bits    | 32 instrs    | 9            | 4 SMs share 32-instr memory |
| TI Sitara PRU  | 32 bits    | 8K instrs    | ~40 RISC ops | 200 MHz, huge for our budget|
| PEmu (target)  | 16 bits    | 64–128 instrs| 8–12         | one core, one program at a time |

## Instruction set (candidate v0.1)

Fixed 16-bit encoding. All instructions execute in 1 cycle unless
they explicitly wait.

| Mnemonic     | 4-bit op | Operand fields                       | Effect                                                 |
|--------------|:--------:|--------------------------------------|--------------------------------------------------------|
| `NOP`        | 0x0      | —                                    | Consume 1 cycle (for padding fixed-duration bit slots) |
| `SET pin,v`  | 0x1      | pin[11:8], val[7:0]                  | Drive `pin` to `val[0]`; assert OE on `pin`            |
| `OUT pin,r`  | 0x2      | pin[11:8], reg[7:4]                  | Drive `pin` from `regs[r] & 1`; assert OE on `pin`     |
| `SHIFT r,d`  | 0x3      | reg[3:0], dir[0], count[3:0]         | Shift register left/right by `count`                   |
| `IN pin,r`   | 0x4      | pin[3:0], reg[3:0]                   | Sample `pin` into `r` bit 0                            |
| `WAIT pin,v` | 0x5      | pin[3:0], val[0], timeout[6:0]       | Block until `pin==val` or `timeout` cycles elapse      |
| `DELAY n`    | 0x6      | n[11:0]                              | Block for `n` cycles                                   |
| `JMP a`      | 0x7      | addr[11:0]                           | PC ← addr                                              |
| `JCND c,a`   | 0x8      | cond[3:0], addr[7:0]                 | PC ← addr if `cond` true (zero, carry, pin==0, etc.)   |
| `PUSH r`     | 0x9      | reg[3:0]                             | Push register to host RX FIFO                          |
| `PULL r`     | 0xA      | reg[3:0]                             | Pop from host TX FIFO into register                    |
| `IRQ n`      | 0xB      | irq[3:0]                             | Raise IRQ line `n` to host                             |


### SET encoding detail

- `pin` is a 4-bit index (0–15). Physical mapping to the 24 chip pins
  is a Phase 2 concern; the model treats pin index `i` as bit `i` of an
  internal 24-bit `pin_out`/`pin_oe` register pair.
- `val[0]` is the value driven. `val[7:1]` are reserved (an extension
  could reuse them to set 8 adjacent pins at once, RP2040-PIO-style).
- SET also asserts the output enable on that pin. There is no matching
  "release" opcode yet — needed for open-drain I²C. Track as an open
  question below.

### OUT encoding detail

- `pin` is a 4-bit index (0–15), same as SET.
- `reg` is a 4-bit field but only values 0–7 name real registers;
  8–15 are invalid and the model throws. Every future opcode that
  reads or writes a register shares this convention.
- Only `regs[r]` bit 0 is used. To drive a higher bit of a register
  onto a pin, use `SHIFT` to bring that bit down to bit 0 first —
  this is exactly how UART TX iterates through a byte.

**Open questions to resolve during Phase 1:**

- Do we need one shift register or two? (SPI needs simultaneous TX
  and RX shifting.)
- Is 16 pins (4-bit pin field) enough? TT gives us 8 inputs + 8
  outputs + 8 bidir = 24 pins, so we may want 5-bit pin fields.
- Clock divider: fixed 8-bit prescaler shared by all instructions,
  or per-instruction override? RP2040 uses a fractional divider —
  probably overkill for our area budget.
- How does the host load programs? Options: (a) shift-in via a
  dedicated bidir pin at boot, (b) SPI-slave loader block, (c)
  memory-mapped via a wider host interface. (a) is cheapest.
- SET always asserts OE. I²C's open-drain SDA needs a way to *release*
  a pin (OE=0). Add a `RELEASE pin` opcode, or make SET encode OE as a
  distinct bit, or use a separate mode register?

## Programming model examples (sketch — will move to `model/programs/`)

### UART TX @ 115200 baud, 50 MHz core clock

```
; bit period = 50e6 / 115200 ≈ 434 core cycles
; program lives at address 0; program counter wraps to 0

start:  PULL   r0            ; wait for host to hand us a byte
        SET    pin_tx, 0     ; start bit
        DELAY  433
loop8:  OUT    pin_tx, r0    ; drive LSB of r0 onto TX
        DELAY  433
        SHIFT  r0, right, 1
        ; repeat 8 times (unrolled) ...
        SET    pin_tx, 1     ; stop bit
        DELAY  433
        JMP    start
```

### SPI master mode 0 (sketch)

```
        PULL   r0            ; byte to send
        ; for each of 8 bits: drive MOSI from r0[7], toggle SCK, sample MISO into r1[0], shift both
```

### I²C master start + address (sketch)

```
        ; SDA/SCL are bidir. Idle: both released (open-drain -> pull-up brings them high)
        SET   pin_sda, 0     ; SDA falls while SCL high = START
        DELAY tsu_sta
        SET   pin_scl, 0
        ; ... clock out 7 addr bits + R/W ...
        ; sample SDA on 9th clock for ACK
```

## Verification hooks baked into the ISA

- Deterministic `DELAY` gives us waveform-level golden vectors.
- `IRQ` makes it easy to write cocotb tests that block on a known
  program-defined event.
- `PUSH`/`PULL` give a clean seam to inject/observe data from Python
  without poking internal signals.
