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
| `SHIFT r,d,c`| 0x3      | reg[11:8], dir[7], count[3:0]        | `regs[r] = dir==0 ? regs[r]<<c : regs[r]>>c` (logical, 8-bit) |
| `IN pin,r`   | 0x4      | pin[11:8], reg[7:4]                  | Release `pin`'s OE, sample `pin_in[pin]` into `regs[r] & 1` |
| `WAIT pin,v,t`| 0x5     | pin[11:8], val[7], timeout[6:0]      | Release `pin`'s OE, stall until `pin_in[pin]==val` or `t` cycles elapse; `t==0` means indefinite |
| `DELAY n`    | 0x6      | n[11:0]                              | Consume exactly `max(n,1)` cycles; PC advances at issue, remaining cycles stall |
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

### SHIFT encoding detail

- `reg` at [11:8]; only 0–7 valid, others throw.
- `dir` is a single bit at [7]: **0 = shift left, 1 = shift right**.
- `count` is 4 bits [3:0], range 0..15. Values ≥ 8 are clamped to 8
  so a shift-out is always fully-zero (and never UB in C++).
- `count = 0` is a legal no-op that still consumes one cycle —
  handy for holding a program's cycle budget exact.
- Shifts are logical (zero-filled). Arithmetic shift would need
  its own opcode; skip for now.
- Bits [6:4] reserved. An assembler should zero them.

### IN encoding detail

- `pin` at [11:8]; `reg` at [7:4]. Bits [3:0] reserved.
- Sampling **releases** `pin_oe[pin]` (drives OE=0). This makes IN
  the natural read path for open-drain I²C SDA — release, wait
  for the line to settle, sample. For a dedicated input pin like
  UART RX, OE was never asserted anyway so this is a no-op.
- Only bit 0 of the target register is written; upper bits are
  preserved. To accumulate multiple sampled bits into a register
  (as UART RX does across 8 bit periods), interleave `IN` with
  `SHIFT r, 1, 1` between each sample. Costs 2 instructions per
  received bit — acceptable at Phase 1; may motivate a fused
  "sample-and-shift" opcode later if instruction budget bites.

### WAIT encoding detail

- `pin` at [11:8], `val` at [7], `timeout` at [6:0] (7 bits, 0–127).
- WAIT consumes **at most `timeout` cycles including the issue cycle**.
  If the pin already matches at issue, WAIT completes in 1 cycle.
  If it never matches, WAIT completes after exactly `timeout` cycles
  (unless `timeout == 0`).
- `timeout == 0` means **wait indefinitely** — no cycle cap. Useful
  for I²C clock stretching. Program has no built-in escape;
  paired with a host-driven reset it's the natural pattern.
- WAIT releases `pin_oe[pin]` at issue, symmetric with IN. This
  gives the open-drain read pattern for I²C:
    ```
    SET  sda, 1              ; release SDA (open-drain pullup brings high)
    WAIT sda, 1, timeout     ; block until master sees line high
    IN   sda, r0             ; sample
    ```
- Model doesn't currently track "matched vs. timed out." Programs
  distinguish by following WAIT with an IN + JCND on bit 0.
  **Open question:** worth adding a dedicated "last WAIT timed
  out" flag readable by JCND? Would cost 1 flip-flop in RTL and
  save 2 instructions per timeout check.

### DELAY encoding detail

- `n` is 12 bits [11:0], range 0..4095.
- Semantics: `DELAY n` consumes exactly `max(n, 1)` cycles. `n=0`
  is clamped to 1 so every instruction takes at least one cycle —
  this keeps `step()` calls exactly one-to-one with clock edges.
- PC advances on the *first* cycle of a DELAY. Subsequent cycles
  are pure stalls: no instruction fetch, no PC change.
- DELAY is the first multi-cycle opcode. The model gains a
  `stall_remaining_` counter so `step()` still corresponds to
  exactly one clock edge, which is essential for cycle-for-cycle
  comparison with the RTL. `WAIT` will reuse this mechanism.

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
