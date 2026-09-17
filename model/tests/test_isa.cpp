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

// Encode Set
constexpr std::uint16_t encode_set(std::uint16_t pin, std::uint16_t val) {
    return static_cast<std::uint16_t>((std::uint16_t{0x1} << 12) | ((pin & 0xF) << 8) | (val & 0xFF));
}
void set_high_drives_pin_and_asserts_oe() {
    pemu::Core c({ encode_set(3, 1) });
    c.step();
    const auto s = c.snapshot();
    assert(s.pin_out & (1u << 3));
    assert(s.pin_oe  & (1u << 3));
    assert(s.pc == 1);
    assert(s.cycle == 1);
}

void set_low_clears_pin_but_leaves_oe_asserted() {
    pemu::Core c({ encode_set(3, 1), encode_set(3, 0) });
    c.run(2);
    const auto s = c.snapshot();
    assert((s.pin_out & (1u << 3)) == 0);   // pin driven low
    assert( s.pin_oe  & (1u << 3));         // but still an output
}

void set_uses_only_val_bit0() {
    // val[7:1] are reserved; only bit 0 should influence the pin.
    pemu::Core c({ encode_set(0, 0xFE) });   // 0xFE = ...1111 1110, bit 0 = 0
    c.step();
    assert((c.snapshot().pin_out & 0x1u) == 0);
}

void set_multiple_pins_accumulate_in_pin_out() {
    pemu::Core c({ encode_set(0, 1), encode_set(7, 1), encode_set(15, 1) });
    c.run(3);
    const auto s = c.snapshot();
    assert(s.pin_out == ((1u << 0) | (1u << 7) | (1u << 15)));
    assert(s.pin_oe  == ((1u << 0) | (1u << 7) | (1u << 15)));
}

// Encode Out
constexpr std::uint16_t encode_out(std::uint16_t pin, std::uint16_t reg) {
    return static_cast<std::uint16_t>((std::uint16_t{0x2} << 12) | ((pin & 0xF) << 8) | ((reg & 0xF) << 4));
}
void out_drives_pin_from_reg_bit0() {
    pemu::Core c({ encode_out(5, 2) });
    c.set_reg(2, 0x01);                // reg[2] bit 0 = 1
    c.step();
    const auto s = c.snapshot();
    assert(s.pin_out & (1u << 5));
    assert(s.pin_oe  & (1u << 5));
    assert(s.pc == 1);
}

void out_ignores_reg_bits_above_zero() {
    pemu::Core c({ encode_out(5, 2) });
    c.set_reg(2, 0xFE);                // bit 0 = 0, bits 1..7 = 1
    c.step();
    assert((c.snapshot().pin_out & (1u << 5)) == 0);
}

void out_low_clears_pin_but_keeps_oe() {
    // SET the pin high first, then OUT with a register whose bit 0 is 0.
    pemu::Core c({ encode_set(5, 1), encode_out(5, 3) });
    c.set_reg(3, 0x00);
    c.run(2);
    const auto s = c.snapshot();
    assert((s.pin_out & (1u << 5)) == 0);
    assert( s.pin_oe  & (1u << 5));
}

void out_invalid_register_throws() {
    pemu::Core c({ encode_out(0, 8) });   // reg 8 doesn't exist
    bool threw = false;
    try { c.step(); } catch (const std::runtime_error&) { threw = true; }
    assert(threw);
}


} // namespace

int main() {
    reset_zeros_state();
    nop_advances_pc();
    pc_out_of_range_throws();
    reset_after_use_zeros_again();
    snapshot_matches_accessors();
    //set
    set_high_drives_pin_and_asserts_oe();
    set_low_clears_pin_but_leaves_oe_asserted();
    set_uses_only_val_bit0();
    set_multiple_pins_accumulate_in_pin_out();
    // OUT
    out_drives_pin_from_reg_bit0();
    out_ignores_reg_bits_above_zero();
    out_low_clears_pin_but_keeps_oe();
    out_invalid_register_throws();

    std::cout << "pemu_model_tests: all tests passed\n";
    return 0;
}
