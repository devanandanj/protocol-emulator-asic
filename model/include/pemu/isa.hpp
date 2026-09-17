// PEmu ISA - cycle-accurate behavioural model.
//
// See docs/isa.md for the opcode table. This header intentionally stays
// small: the interesting logic goes in isa.cpp. The rules on 'why C++
// and not Python' live in START_HERE.md.
//
// Design intent:
//   - Zero external dependencies (C++17 stdlib only).
//   - Deterministic step()/run() so cocotb can compare RTL cycle-for-cycle
//     against a trace this model produced ahead of time.
//   - Cheap to construct in unit tests: pass a std::vector<uint16_t>
//     of instruction words, done.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace pemu {
// Opcode field lives in bits [15:12] of each 16-bit instruction word.
// See docs/isa.md for the full table.
enum class Op : std::uint8_t {
    Nop   = 0x0,
    Set   = 0x1,
    Out   = 0x2,
    Shift = 0x3,
    In    = 0x4,
    Wait  = 0x5,
    Delay = 0x6,
    Jmp   = 0x7,
    Jcnd  = 0x8,
    Push  = 0x9,
    Pull  = 0xA,
    Irq   = 0xB,
    Ldi   = 0xC,   // load immediate into register
    Rot   = 0xD,   // Rotate Shift Register
};

inline constexpr std::size_t kNumRegs      = 8;
inline constexpr std::size_t kProgramWords = 128;
inline constexpr std::size_t kNumPins      = 24;   // 8 ui + 8 uo + 8 uio
inline constexpr std::size_t kFifoSize     = 16;   // TX and RX bounded FIFO depth
inline constexpr std::size_t kNumIrqLines  = 16;   // irq_lines_ is a 16-bit bitmask

// Decoded instruction. The 12-bit operand field is preserved raw here;
// each opcode's execute path slices it up its own way.
struct Instruction {
    Op            op;
    std::uint16_t operand;   // bits [11:0] of the encoded word

    static constexpr Instruction decode(std::uint16_t word) noexcept {
        return { static_cast<Op>((word >> 12) & 0xF),
                 static_cast<std::uint16_t>(word & 0x0FFF) };
    }
};

// One row of the cycle-accurate trace. What cocotb will compare
// against the RTL's own per-cycle observations.
struct TraceRecord {
    std::uint64_t                        cycle;
    std::uint16_t                        pc;
    std::array<std::uint8_t, kNumRegs>   regs;
    std::uint32_t                        pin_out;   // 24 bits used
    std::uint32_t                        pin_oe;    // 24 bits used
};

class Core {
public:
    explicit Core(std::vector<std::uint16_t> program) noexcept;

    void reset() noexcept;
    void step();                          // execute one instruction
    void run(std::size_t max_cycles);     // step() until max_cycles or halt

    // Host-side FIFO seam. The RTL exposes the same seam so cocotb can
    // drive TX / observe RX identically on model and design.
    void                        push_tx(std::uint8_t byte);
    std::optional<std::uint8_t> pop_rx();

    // For directly injecting input-pin state from tests.
    void set_pin_in(std::uint32_t bits) noexcept { pin_in_ = bits & 0x00FFFFFFu; }
    void set_reg(std::size_t idx, std::uint8_t val);

    [[nodiscard]] TraceRecord snapshot() const noexcept;

    // Host-visible interrupt lines. Set by the IRQ opcode; the host
    // (or a cocotb testbench) reads them and clears when serviced.
    [[nodiscard]] std::uint16_t irq_lines() const noexcept { return irq_lines_; }
    void                        clear_irq(std::uint8_t n) noexcept;

    // Small accessors for unit tests.
    std::uint16_t                                    pc()     const noexcept { return pc_; }
    const std::array<std::uint8_t, kNumRegs>&        regs()   const noexcept { return regs_; }
    std::uint64_t                                    cycles() const noexcept { return cycles_; }

private:
    std::vector<std::uint16_t>            program_;
    std::uint16_t                         pc_{0};
    std::array<std::uint8_t, kNumRegs>    regs_{};
    std::deque<std::uint8_t>              tx_fifo_;   // host -> core
    std::deque<std::uint8_t>              rx_fifo_;   // core -> host
    std::uint32_t                         pin_in_{};
    std::uint32_t                         pin_out_{};
    std::uint32_t                         pin_oe_{};
    std::uint64_t                         cycles_{};
    std::uint16_t                         stall_remaining_{};
    bool                                  waiting_for_pin_{false};
    bool                                  wait_forever_{false};
    std::uint8_t                          wait_pin_{};
    std::uint8_t                          wait_val_{};
    bool                                  waiting_for_pull_{false};
    std::uint8_t                          pull_reg_{};
    std::uint16_t                         irq_lines_{};
};

}
