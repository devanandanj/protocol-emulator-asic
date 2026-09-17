# PEmu Assembler (C++)

A small standalone assembler for PEmu programs. Text goes in, one
u16 per line of hex comes out.

## Layout

```
asm/
├── CMakeLists.txt
├── include/pemu/asm.hpp    API: assemble(), write_hex()
├── src/
│   ├── asm.cpp             implementation
│   └── main.cpp            pemuasm CLI
└── tests/
    └── test_asm.cpp        framework-free asserts
```

## Build & run

From the repo root:

```bash
cmake -B build -S .
cmake --build build
./build/asm/pemuasm examples/blink.pemu blink.hex
```

The output file is consumed directly by `pemu_sim` and by the RTL's
program-loader (once that exists).

## Source format

```
; Comments start with ';' or '#'. Blank lines ignored.
; Case-insensitive mnemonics. Numbers: decimal, 0x hex, 0b binary.
; Registers: r0..r7. SHIFT direction: 'left' or 'right'.
; Labels: 'name:' — any position, forward or backward references OK.

start:  pull  r0              ; wait for host byte
        set   0, 0            ; TX pin low = start bit
        delay 434             ; one bit period @ 115200 baud, 50 MHz core
        out   0, r0           ; drive bit 0
        delay 434
        shift r0, right, 1
        ; ... unroll or loop for 8 bits ...
        set   0, 1            ; stop bit
        delay 434
        jmp   start
```

## Supported opcodes

Every opcode in `docs/isa.md` is accepted:

| Mnemonic          | Example                 |
|-------------------|-------------------------|
| `nop`             | `nop`                   |
| `set pin, val`    | `set 0, 1`              |
| `out pin, reg`    | `out 0, r0`             |
| `shift reg, dir, count` | `shift r0, right, 1` |
| `in pin, reg`     | `in 3, r1`              |
| `wait pin, val, timeout` | `wait 3, 1, 100` |
| `delay n`         | `delay 434`             |
| `jmp addr | label` | `jmp start`            |
| `jcnd reg, addr | label` | `jcnd r0, loop`  |
| `push reg`        | `push r1`               |
| `pull reg`        | `pull r0`               |
| `irq n`           | `irq 3`                 |

## Status

Complete v0.1 assembler. Two-pass, resolves forward and backward
label references, reports errors with source line numbers, no
external dependencies.

Not yet supported (deferred): macros, `.equ`-style constants,
data literals, multi-file assembly. Add if programs get repetitive
enough to want them.
