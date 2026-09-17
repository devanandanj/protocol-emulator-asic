// Deliberately framework-free: plain <cassert>, so the model has zero
// external test-side dependencies. Add Catch2/GoogleTest later only if
// the volume of tests actually demands it.

// Force assert() to be live even in Release builds — this is a test binary
// and silently-passing tests would be worse than useless.
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "pemu/isa.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void reset_zeros_state() {
    pemu::Core c({0x0000});
    assert(c.pc() == 0);
    assert(c.cycles() == 0);
    for (auto r : c.regs()) assert(r == 0);
}

void nop_advances_pc() {
    pemu::Core c({0x0000, 0x0000, 0x0000});
    c.step();
    assert(c.pc() == 1);
    assert(c.cycles() == 1);
    c.step();
    assert(c.pc() == 2);
    assert(c.cycles() == 2);
}

void pc_out_of_range_throws() {
    pemu::Core c({0x0000});
    c.step();  // pc = 1, past the end of a 1-word program
    bool threw = false;
    try {
        c.step();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void reset_after_use_zeros_again() {
    pemu::Core c({0x0000, 0x0000});
    c.step();
    c.step();
    c.reset();
    assert(c.pc() == 0);
    assert(c.cycles() == 0);
}

void snapshot_matches_accessors() {
    pemu::Core c({0x0000});
    const auto s = c.snapshot();
    assert(s.pc == c.pc());
    assert(s.cycle == c.cycles());
    for (std::size_t i = 0; i < pemu::kNumRegs; ++i) assert(s.regs[i] == c.regs()[i]);
}

} // namespace

int main() {
    reset_zeros_state();
    nop_advances_pc();
    pc_out_of_range_throws();
    reset_after_use_zeros_again();
    snapshot_matches_accessors();
    std::cout << "pemu_model_tests: all tests passed\n";
    return 0;
}
