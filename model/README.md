# PEmu Behavioural Model (C++)

A cycle-accurate C++ simulator of the PEmu ISA. This is the Phase 1
workhorse: we write UART/SPI/I²C programs against it and prove the
ISA can express them **before** touching Verilog.

## Layout

```
model/
├── CMakeLists.txt
├── include/pemu/isa.hpp   API: Op enum, Instruction, Core
├── src/
│   ├── isa.cpp            simulator implementation
│   └── main.cpp           pemu_sim CLI
└── tests/
    └── test_isa.cpp       framework-free asserts
```

Two artifacts are built:

| Target             | What it is                                                             |
|--------------------|------------------------------------------------------------------------|
| `pemu_model`       | static library, linked by the CLI and by tests                         |
| `pemu_sim`         | CLI. Runs a program, writes a per-cycle TSV trace                      |
| `pemu_model_tests` | Standalone test binary, exercised by `ctest`                           |

## Build & run

From the repo root:

```bash
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

Then run a program (once we have any):

```bash
./build/model/pemu_sim examples/nop.hex trace.tsv
```

## How it plugs into cocotb

The Python testbench never links C++ code. Instead:

1. Cocotb starts the RTL simulation.
2. It invokes `pemu_sim` as a subprocess with the same program the
   RTL is running.
3. `pemu_sim` writes `trace.tsv`.
4. Cocotb reads the TSV line-by-line and asserts the RTL's observed
   state matches the model's cycle-for-cycle.

Trivial file format, no FFI, C++ and Python stay strangers.

## Status

The `step()` function currently implements only `NOP` and throws on
every other opcode. Filling it in is the first Phase 1 coding task —
see `docs/isa.md` for the opcode table.
