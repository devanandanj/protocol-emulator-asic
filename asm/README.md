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
; Comments start with ';' or '#'.
; One instruction per line. Case-insensitive mnemonics.

nop            ; the only instruction implemented today.
```

## Status

**Phase 5 deliverable.** Right now the assembler only accepts `nop`
— just enough to compile, wire into the build, and let round-trip
tests through the model pass. The real opcode set gets filled in
once Phase 4 is done and we know the ISA has settled.
