# PEmu — Project Plan

This document is meant to be **read start to finish**. It explains
what we're building, why we've chosen this shape, and how the work
splits across the ~16 months until the submission deadline. The
task tracker holds the "what next" checklist; this file holds the
mental model.

- **Deadline:** 2027-01-18
- **Target shuttle:** 2027-03, IHP 130nm CMOS5L via Tiny Tapeout
- **Working assumption:** solo, ~10+ hrs/week, comfortable with
  Verilog and FPGA sim, first-ever silicon tapeout.

---

## 1. What we're building, in one paragraph

A tiny programmable core that lives on ~1 mm² of silicon and does
one job: **read and write chip pins on a cycle-precise schedule**.
The exact protocol it "is" — UART today, SPI in an hour, I²C
tomorrow — is decided at boot by loading a short program into its
instruction memory. Think of it as an RP2040 PIO block or a TI
Sitara PRU shrunk down to fit one Tiny Tapeout submission.

## 2. Why this shape, and not something else

The competition brief specifically points at the RP2040 PIO and the
TI PRU as inspirations. That's a strong hint that the judges are
looking for the *programmable-state-machine* idiom rather than
three hand-written peripheral blocks glued together. Three reasons
this is the right frame for us:

1. **The novelty budget is the ISA and the verification, not a
   whole new architectural paradigm.** Trying to invent an
   entirely new class of programmable I/O engine on a first
   tapeout is a good way to miss the deadline. Copying the proven
   pattern and being interesting *inside* it is what the winning
   RP2040 PIO designers did too.
2. **Programmability is the whole point.** A fixed-function block
   that only does UART/SPI/I²C would technically satisfy the brief
   but would demo poorly — "look, my UART works" is not a story.
   "Here is my ISA; here are ten programs written for it; here is
   the assembler; here is silicon running any of them from
   firmware" is a story.
3. **Verification tractability.** A small ISA with deterministic
   `DELAY` and observable `PUSH`/`PULL` FIFOs gives us clean seams
   for cocotb, constrained-random, and formal. The judges named
   verification methodology as a scoring axis — we get to lean on
   that.

## 3. The strategy in three moves

**Move A — de-risk the toolchain before designing anything.** The
RTL→GDS flow (Verilog through OpenLane, DRC/LVS in KLayout,
sign-off against the CMOS5L PDK) has more surprises than any
single design decision. Get a blinky through it *first*.

**Move B — freeze the ISA in C++ before writing Verilog.**
Every hour spent debugging the ISA in Verilog is 10x an hour spent
debugging it in C++. So we build a cycle-accurate C++ model of the
ISA (`model/`), hand-write UART/SPI/I²C programs against it, and
only touch RTL once real protocols have shaken out the instruction
set. The model dumps per-cycle trace files that the cocotb Python
testbench reads back and compares against the RTL — no FFI, just a
plain TSV between the two worlds.

**Move C — treat verification as a deliverable, not cleanup.** The
brief calls out "formal methods, random constrained tests,
AI-assisted verification" explicitly. Structure the cocotb tests
around per-protocol golden models, layer in constrained-random and
coverage, and add SymbiYosys formal properties where scope allows.
Write it all up so the judges can see the methodology, not just
its output.

## 4. What the finished submission looks like

If everything goes to plan, on 2027-01-17 we are handing Jane
Street a repo that contains:

- **RTL** — a ~2–3K-line Verilog core plus TT wrapper, fitting in
  8×4 (or 8×2, TBD) tiles, closing timing at the target clock.
- **A GDS** — signed off through the CMOS5L OpenLane flow and
  passing DRC/LVS.
- **An ISA doc** — one short PDF/markdown, opcodes and encoding
  fully specified, cycle costs explicit.
- **A behavioural model in C++** — the same ISA, executable,
  matches the RTL cycle-for-cycle when cocotb compares against
  the model's trace file.
- **A tiny assembler in C++** — turns human-readable programs into
  machine code, ships with example programs for each supported
  protocol.
- **A verification write-up** — what's covered by directed tests,
  what by constrained-random, what by formal, and where the
  holes are.
- **A demo** — waveforms and (post-March-2027) a dev-board photo
  showing the fabricated chip actually talking UART/SPI/I²C.

## 5. The six phases, as a narrative

### Phase 0 — de-risk the toolchain (weeks 1–2)

We push a throwaway design (currently the blinky counter in
`src/project.v`) through the *entire* pipeline: local `iverilog +
cocotb` and the four Tiny Tapeout GitHub Actions workflows
(`test.yaml`, `fpga.yaml`, `gds.yaml`, `docs.yaml`). We fill out
the Jane Street sign-up form so we're on their mailing list for
shuttle logistics. We pin down the tile-shape question (the blog
says 8×4, the template comment lists up to 8×2 — someone knows
the answer, we just have to ask).

The goal is to find every environment gotcha *now*, when the cost
of surprise is zero.

**Done when:** all four TT workflows green on a trivial commit.

### Phase 1 — ISA and architecture (weeks 3–6)

We write down the ISA in `docs/isa.md` — opcodes, encoding, word
width, program memory depth, register file size, clock divider,
pin mux, host interface. Then we fill in the C++ model (`model/`)
into a real cycle-accurate simulator and hand-write PEmu programs
for UART TX, UART RX, SPI master, and I²C master against it. If
any of those four protocols can't be expressed cleanly in the
ISA, we revise the ISA — cheaply, in C++ — and iterate.

Alongside, we do a back-of-envelope area sketch: a 4-bit-op /
16-bit-word core, an 8-entry register file, 128-word program
memory, pin mux and OE control, host FIFOs. Rough number: does it
fit into ~30–40% of the tile budget, leaving slack for the
inevitable overrun? If not, we shrink.

**Done when:** the four target-protocol programs run correctly on
the C++ model and the area sketch closes.

### Phase 2 — RTL (weeks 7–14)

We write the Verilog: PC, program memory, register file,
ALU/shifter, clock divider, pin I/O with output enables, and the
host-side interface (how programs get loaded and how FIFOs get
drained). We bring protocols up in the order most likely to expose
ISA problems: **UART TX** first (easiest — one-directional, no
timing feedback), then **UART RX** (start-bit sampling adds
mid-bit alignment), then **SPI master** (adds simultaneous
in/out shifting), then **I²C master** (open-drain, clock
stretching — the real stress test).

Expect the ISA to change once or twice during this phase. That
happened to the RP2040 PIO too, and it's cheap when the C++ model
is the source of truth. When we tweak an opcode, the model
changes first, both the model's unit tests and the Verilog cocotb
tests should still pass, and then the RTL changes.

**Done when:** the four protocols pass their cocotb tests on the
RTL, matching the C++ model cycle-for-cycle.

### Phase 3 — verification (parallel to phase 2, deepens after)

This runs alongside RTL work, not after it — every new opcode gets
tests written the day it lands. But once RTL is functionally
complete, we go deeper:

- **Directed tests** — one cocotb test per protocol per corner:
  baud rate extremes, back-to-back bytes, timeouts.
- **Constrained-random** — random programs + random FIFO
  stimulus, checked against the C++ model as the golden.
- **Formal** — SymbiYosys with properties like "PC is always in
  range," "no output pin is both driven and OE-off," "no double
  push on FIFO." We won't formally prove correctness of the whole
  design; we'll prove safety invariants that are cheap and high
  value.
- **Coverage** — cocotb coverage on which opcodes fire, which
  pins are driven, which conditional branches taken.

And we write it all up in `docs/verification.md`. The write-up
itself is a submission deliverable — the judges want to see the
methodology.

**Done when:** no known open bugs, coverage report checked in.

### Phase 4 — physical implementation (weeks 15–20)

We run the full OpenLane flow for CMOS5L targeting the chosen
tile shape. Close timing. Run gate-level sim (`make GATES=yes`)
against the same testbenches to prove the netlist behaves
identically to the RTL. Expect one or two rounds of RTL rework
driven by area overflow or timing violations — a critical path
turns out to be a shift+add in the ALU, we pipeline it, everything
resimulates.

**Done when:** clean GDS through DRC/LVS at the target clock,
gate-level sim green.

### Phase 5 — polish and stretch (weeks 21–24)

Everything to make the submission look like a finished product:

- **Assembler** — `asm/` builds a `pemuasm` binary that turns
  human-readable syntax into machine code. The v0 skeleton already
  accepts `nop`; Phase 5 finishes the opcode set. Even a minimal
  assembler massively strengthens the "here's how you use this
  chip" story.
- **Stretch protocol** — a fifth program to demo the
  general-purpose claim. PS/2 is easy; low-speed USB or
  10Mbit Ethernet are the "wow" options the brief calls out
  explicitly, if we have the cycles.
- **Documentation** — `docs/architecture.md` with a real block
  diagram, `docs/info.md` (the TT datasheet) filled in, README
  with waveforms.

### Phase 6 — submission buffer (Dec 2026 – Jan 18, 2027)

**No new development.** The last month is slack for the things
that always go wrong at the end: a KLayout DRC rule we missed,
a broken CI job, a needed clarification from Jane Street. We
should be feature-complete on 2026-12-15 and spend the last
month locking things down.

## 6. How the pieces of the repo fit together

Once Phase 1 is under way, the interlocking parts look like this:

```
   docs/isa.md            <----- the source of truth for the ISA
        |
        |   translated into executable form
        v
   model/{isa.hpp,cpp}    <----- C++ interpreter of the ISA
        |
        |   emits a per-cycle trace.tsv
        v
   test/*.py              <----- cocotb tests read trace.tsv,
        ^                        compare against RTL cycle-for-cycle
        |
   src/*.v                <----- the Verilog implementation
```

Programs (UART, SPI, …) sit as human-readable `.pemu` text and get
assembled by the `pemuasm` binary (built from `asm/`) into the u16
hex format that both `pemu_sim` and the Verilog RTL load. If a
program produces the same trace under both, the RTL is correct
on that program.

## 7. Risks and unknowns we're actively tracking

- **Tile shape.** Blog says 8×4, template comment lists 1x1..8x2.
  Answer this on the TT / Jane Street channel *before* Phase 2
  ends — the floorplan choice cascades into what fits.
- **Timing on 130nm.** Older nodes are slower. We may find our
  achievable clock is lower than we assumed, which stretches
  UART bit periods and eats into DELAY range. Mitigation: make
  the clock divider wider than we think we need.
- **I²C clock stretching.** This is the one protocol feature most
  likely to expose an ISA gap. If the `WAIT pin` primitive can't
  express it cleanly, we'll add a dedicated opcode or a
  sample-and-branch idiom.
- **Solo bus factor.** No teammate to catch a stale assumption.
  Compensating levers: aggressive testing, an early friendly
  reader of the ISA doc, and finishing a month early on purpose.

## 8. What good looks like as we go

Each phase should end with a **tagged commit** in git and a short
note in `docs/decisions.md` (to be created in Phase 1) explaining
what changed and why. If a phase drags past its window, the
question to ask is not "how do I catch up" but "which scope do I
cut to protect Phase 6's buffer." The buffer is sacred.
