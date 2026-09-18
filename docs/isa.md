# PEmu ISA

Status: **frozen for v1**. The 15-opcode set below is what the RTL,
the C++ model, and all four shipped protocol programs (`uart_tx`,
`uart_rx`, `spi`, `i2c_write`) run against. The verification harness
diffs RTL and model cycle-for-cycle on every one of them.

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
| `JMP a`      | 0x7      | addr[11:0]                           | `pc = addr`. Unconditional, 1 cycle.                   |
| `JCND r,a`   | 0x8      | reg[11:8], addr[7:0]                 | If `regs[r] != 0`: `--regs[r]`, `pc = addr`. Else `pc++`. |
| `PUSH r`     | 0x9      | reg[11:8]                            | Append `regs[r]` to RX FIFO (host reads); throw if full. |
| `PULL r`     | 0xA      | reg[11:8]                            | Pop TX FIFO into `regs[r]`; stall until non-empty.     |
| `IRQ n`      | 0xB      | irq[11:8]                            | Set bit `n` of host-visible `irq_lines` (level, host clears). |
| `LDI r,imm`  | 0xC      | reg[11:8], imm[7:0]                  | `regs[r] = imm`. Load 8-bit immediate into register.   |
| `ROT r,d,c`  | 0xD      | reg[11:8], dir[7], count[3:0]        | Rotate `regs[r]` by `c mod 8` positions. dir=0 left, dir=1 right. |
| `OUT_OD p,r` | 0xE      | pin[11:8], reg[7:4]                  | Open-drain drive: bit0=0 drives `pin` low (OE=1), bit0=1 releases (OE=0). |


### SET encoding detail

- `pin` is a 4-bit field at [11:8]. On the shipped chip only pins 0–7
  are wired (RTL slices `operand[10:8]`), so bit [11] is
  reserved-must-be-zero. Programs encoded for pins ≥ 8 will silently
  alias.
- `val[0]` is the value driven. `val[7:1]` are reserved (an extension
  could reuse them to set 8 adjacent pins at once, RP2040-PIO-style).
- SET also asserts the output enable on that pin. Open-drain
  operation lives in the `OUT_OD` opcode; SET is push-pull.

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

### JMP encoding detail

- `addr` is 12 bits [11:0], range 0..4095. Covers any position in
  the 128-word (or up to 4096-word) program memory.
- 1 cycle. `pc = addr` unconditionally. The next `step()` fetches
  program[addr]; PC-bounds checking runs there, not in JMP.

### JCND encoding detail

- `reg` at [11:8], `addr` at [7:0] (8 bits, 0..255).
- Semantics: **decrement-and-branch-if-nonzero**. If `regs[r] != 0`,
  `regs[r] -= 1` and `pc = addr`. If `regs[r] == 0`, PC falls through
  (r unchanged).
- 1 cycle either way.
- Loop idiom: load counter into a register, do the body, `JCND r, top`.
  Executes body exactly N+1 times where N is the initial counter
  (N branches back + 1 fall-through). To loop *exactly* N times,
  preload with N-1.
- For pin-based branches: `IN pin, r; JCND r, target` — sampled bit
  becomes r=0 or r=1; the JCND then branches iff the pin was 1
  (also decrementing r back to 0). Two-instruction pattern; if this
  is too costly in real programs we'll revisit with a dedicated
  pin-conditional branch.
- `addr` is only 8 bits, so JCND targets must live in the first
  256 program words. Beyond that, use JMP.

### PUSH encoding detail

- `reg` at [11:8]. Bits [7:0] reserved.
- Appends `regs[r]` (8-bit) to the RX FIFO — the *core-to-host*
  direction. Named from the host's perspective: the host reads
  from RX.
- RX FIFO is bounded at `kFifoSize` (16 entries in v0.1). Push
  onto a full FIFO throws in the model. In the RTL this maps to
  a "FIFO full" IRQ or a program-visible flag; deferred to Phase 2.
- 1 cycle.

### PULL encoding detail

- `reg` at [11:8]. Bits [7:0] reserved.
- Pops the front byte of the TX FIFO (the *host-to-core* direction)
  and writes it into `regs[r]`.
- If the TX FIFO is empty at issue, PULL **stalls** until the host
  pushes a byte, then completes on the cycle after data arrives.
  PC advances at issue; subsequent stall cycles don't touch PC.
- 1 cycle when data is ready; unbounded stall when it isn't. This
  is the standard "consumer waits on producer" seam that lets
  cocotb drive protocol tests by pacing `push_tx()`.

### IRQ encoding detail

- `n` at [11:8], range 0..15. Bits [7:0] reserved.
- Sets bit `n` of the host-visible `irq_lines` register. Latching:
  the bit stays set until the host calls `clear_irq(n)`.
- Multiple IRQ opcodes accumulate independent bits (bitmask OR).
  Clearing a bit that isn't set — or an out-of-range index — is a
  silent no-op.
- 1 cycle.
- Programs typically raise IRQ to signal "byte ready in RX FIFO"
  or "protocol error"; cocotb tests block until a specific bit
  goes high, then service and clear.

### LDI encoding detail

- `reg` at [11:8], `imm` at [7:0]. Full 8-bit immediate.
- `regs[r] = imm`, 1 cycle. Overwrites any previous value.
- Added late in the ISA-design pass — the original v0.1 sketch
  assumed every register value would arrive via PULL (from host)
  or IN (from a pin). That works for real protocol operation,
  but makes it painful to write standalone smoke tests where the
  program should be self-sufficient. LDI plugs that gap without
  changing anything about how PULL/IN behave.
- Assembler: `ldi r0, 0x5A` — accepts decimal, `0x`-hex, or `0b`-
  binary immediates.

### ROT encoding detail

- Same field layout as SHIFT (reg [11:8], dir [7], count [3:0]).
- **Rotation**, not shift — bits leaving one end reappear at the other.
- `count mod 8` is used, so rotating by 8, 16, ..., is a no-op.
- Added specifically so UART RX (and other LSB-first serial protocols)
  can assemble bytes in natural memory order via `IN + ROT r, right, 1`.
  Without it, sample-and-shift produces bit-reversed bytes.

**Design questions closed during v1 hardening:**

- **Shift register count** — one general-purpose register file (8 × 8) is
  enough. SPI's simultaneous TX/RX shift uses two separate registers
  (`r0` shifts TX left, `r1` accumulates RX MSB-first); the cost is one
  extra instruction per bit and it fits in the 50-cycle SCK slot.
- **Pin field width** — kept at 4 bits in the encoding, but the chip only
  wires pins 0–7 (RTL slices `operand[10:8]`, bit [11] reserved). Room
  for a v2 that grows to 16 pins by widening `pin_out`/`pin_oe`.
- **Clock divider** — no dedicated prescaler. `DELAY` covers every
  timing budget we care about, saves a shared piece of state, and keeps
  each program self-contained about its own cycle math.
- **Loader path** — bootstrap on `ui[0..2]` + `uio_in[7:0]` while
  `rst_n=0` (see `architecture.md`). Two-byte writes per instruction,
  auto-incrementing address.
- **Open-drain drive** — `OUT_OD` opcode (0xE). Bit 0 == 0 drives low
  (OE=1); bit 0 == 1 releases (OE=0). Matches I²C SDA/SCL semantics
  without a separate mode register.

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
