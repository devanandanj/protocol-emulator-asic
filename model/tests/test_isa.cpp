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

// Shift
constexpr std::uint16_t encode_shift(std::uint16_t reg,
                                     std::uint16_t dir,
                                     std::uint16_t count) {
    return static_cast<std::uint16_t>(
        (std::uint16_t{0x3} << 12)
        | ((reg   & 0xF) << 8)
        | ((dir   & 0x1) << 7)
        |  (count & 0xF));
}

void shift_left_by_one_doubles_low_value() {
    pemu::Core c({ encode_shift(2, /*dir=*/0, /*count=*/1) });
    c.set_reg(2, 0x03);              // 0000 0011  ->  0000 0110
    c.step();
    assert(c.regs()[2] == 0x06);
    assert(c.snapshot().pc == 1);
}

void shift_right_by_two_halves_twice() {
    pemu::Core c({ encode_shift(2, /*dir=*/1, /*count=*/2) });
    c.set_reg(2, 0x80);              // 1000 0000  ->  0010 0000
    c.step();
    assert(c.regs()[2] == 0x20);
}

void shift_left_drops_high_bits() {
    // 0xF0 << 4 = 0xF00; register is only 8 bits, high nibble falls off.
    pemu::Core c({ encode_shift(2, 0, 4) });
    c.set_reg(2, 0xF0);
    c.step();
    assert(c.regs()[2] == 0x00);
}

void shift_by_zero_is_noop_but_advances_state() {
    pemu::Core c({ encode_shift(2, 0, 0) });
    c.set_reg(2, 0x5A);
    c.step();
    const auto s = c.snapshot();
    assert(c.regs()[2] == 0x5A);
    assert(s.pc    == 1);
    assert(s.cycle == 1);            // still consumes one cycle
}

void shift_by_more_than_eight_clears_register() {
    // count=15 clamps to 8; 8-bit register shifted by 8 loses everything.
    pemu::Core c({ encode_shift(2, 0, 15) });
    c.set_reg(2, 0xFF);
    c.step();
    assert(c.regs()[2] == 0x00);
}

// In
constexpr std::uint16_t encode_in(std::uint16_t pin, std::uint16_t reg) {
    return static_cast<std::uint16_t>(
        (std::uint16_t{0x4} << 12) | ((pin & 0xF) << 8) | ((reg & 0xF) << 4));
}

void in_samples_high_pin_into_reg_bit0() {
    pemu::Core c({ encode_in(3, 2) });
    c.set_pin_in(1u << 3);                    // pin 3 high
    c.step();
    assert((c.regs()[2] & 0x1u) == 1);
    assert(c.snapshot().pc == 1);
}

void in_sampling_zero_clears_only_bit0() {
    pemu::Core c({ encode_in(3, 2) });
    c.set_reg(2, 0xFF);                       // preload all-ones
    c.set_pin_in(0);                          // pin 3 low
    c.step();
    assert(c.regs()[2] == 0xFE);              // bit 0 -> 0, others preserved
}

void in_releases_output_enable_on_sampled_pin() {
    // SET drives the pin (OE=1), then IN releases it.
    pemu::Core c({ encode_set(3, 1), encode_in(3, 2) });
    c.set_pin_in(1u << 3);
    c.run(2);
    const auto s = c.snapshot();
    assert((s.pin_oe & (1u << 3)) == 0);      // now floating
    assert((c.regs()[2] & 0x1u) == 1);        // sampled the '1'
}

void in_invalid_register_throws() {
    pemu::Core c({ encode_in(0, 8) });
    bool threw = false;
    try { c.step(); } catch (const std::runtime_error&) { threw = true; }
    assert(threw);
}

// Wait
constexpr std::uint16_t encode_wait(std::uint16_t pin,
                                    std::uint16_t val,
                                    std::uint16_t timeout) {
    return static_cast<std::uint16_t>(
        (std::uint16_t{0x5} << 12)
        | ((pin     & 0xF)  << 8)
        | ((val     & 0x1)  << 7)
        |  (timeout & 0x7F));
}
void wait_returns_in_one_cycle_when_condition_already_met() {
    // WAIT pin=3, val=1, timeout=5, then NOP.
    pemu::Core c({ encode_wait(3, 1, 5), 0x0000 });
    c.set_pin_in(1u << 3);                       // pin 3 already high
    c.step();
    const auto s = c.snapshot();
    assert(s.cycle == 1);
    assert(s.pc == 1);
    c.step();                                     // NOP fetches
    assert(c.snapshot().pc == 2);
    assert(c.snapshot().cycle == 2);
}

void wait_times_out_after_exactly_timeout_cycles() {
    // Condition never met.
    pemu::Core c({ encode_wait(3, 1, 5), 0x0000 });
    c.set_pin_in(0);
    for (int i = 0; i < 5; ++i) c.step();
    const auto s = c.snapshot();
    assert(s.cycle == 5);
    assert(s.pc == 1);                            // WAIT itself just finished
    c.step();
    assert(c.snapshot().pc == 2);                 // NOP now fetches
    assert(c.snapshot().cycle == 6);
}

void wait_exits_early_when_pin_matches_mid_stall() {
    pemu::Core c({ encode_wait(3, 1, 100), 0x0000 });
    c.set_pin_in(0);
    c.step();                                     // cycle 1: WAIT issues, fail
    c.step();                                     // cycle 2: stall, fail
    c.set_pin_in(1u << 3);                        // external event
    c.step();                                     // cycle 3: check succeeds
    assert(c.snapshot().cycle == 3);
    assert(c.snapshot().pc == 1);
    c.step();
    assert(c.snapshot().pc == 2);                 // NOP fetched
}

void wait_forever_with_timeout_zero_never_times_out() {
    pemu::Core c({ encode_wait(3, 1, 0) });
    c.set_pin_in(0);
    for (int i = 0; i < 100; ++i) c.step();
    assert(c.snapshot().cycle == 100);
    assert(c.snapshot().pc == 1);                 // PC advanced past WAIT at issue
    // Flip pin; next step exits.
    c.set_pin_in(1u << 3);
    c.step();
    assert(c.snapshot().cycle == 101);
}

void wait_releases_oe_on_pin_at_issue() {
    // SET drives pin 3 (OE=1), then WAIT releases it (OE=0).
    pemu::Core c({ encode_set(3, 1), encode_wait(3, 0, 5) });
    c.set_pin_in(0);                              // pin low externally
    c.step();                                     // SET
    c.step();                                     // WAIT — condition (pin==0) already met
    const auto s = c.snapshot();
    assert((s.pin_oe & (1u << 3)) == 0);
}

// Delay
constexpr std::uint16_t encode_delay(std::uint16_t n) {
    return static_cast<std::uint16_t>(
        (std::uint16_t{0x6} << 12) | (n & 0x0FFFu));
}

void delay_of_one_behaves_like_single_cycle_instruction() {
    pemu::Core c({ encode_delay(1) });
    c.step();
    const auto s = c.snapshot();
    assert(s.pc == 1);
    assert(s.cycle == 1);
}

void delay_of_zero_clamps_to_one_cycle() {
    // n=0 must still consume one cycle — otherwise step() calls
    // stop being 1:1 with clock edges.
    pemu::Core c({ encode_delay(0) });
    c.step();
    assert(c.snapshot().cycle == 1);
    assert(c.snapshot().pc == 1);
}

void delay_consumes_exactly_n_cycles() {
    // DELAY 5 then NOP: 5 + 1 = 6 total cycles across 6 step() calls.
    pemu::Core c({ encode_delay(5), 0x0000 });
    // First step: DELAY issues, PC advances, 4 more stalls remain.
    c.step();
    assert(c.snapshot().cycle == 1);
    assert(c.snapshot().pc == 1);
    // Steps 2..5: pure stalls.
    for (int i = 0; i < 4; ++i) c.step();
    assert(c.snapshot().cycle == 5);
    assert(c.snapshot().pc == 1);      // PC did NOT advance during stall
    // Step 6: NOP fetches.
    c.step();
    assert(c.snapshot().cycle == 6);
    assert(c.snapshot().pc == 2);
}

void delay_via_run_helper_matches_step_by_step() {
    // Same program driven by run() — result must be identical.
    pemu::Core c({ encode_delay(3), 0x0000 });
    c.run(4);
    assert(c.snapshot().cycle == 4);
    assert(c.snapshot().pc == 2);
}

void delay_max_encodable_value() {
    // 0xFFF = 4095 — the largest single-instruction stall.
    pemu::Core c({ encode_delay(0xFFF) });
    c.run(4095);
    assert(c.snapshot().cycle == 4095);
    assert(c.snapshot().pc == 1);
    // A 4096th step would fetch off the end and throw — leave that
    // to the pc_out_of_range test rather than duplicating here.
}

// JMP
constexpr std::uint16_t encode_jmp(std::uint16_t addr) {
    return static_cast<std::uint16_t>(
        (std::uint16_t{0x7} << 12) | (addr & 0x0FFFu));
}

void jmp_sets_pc_directly() {
    // NOP at 0, JMP 0 at 1 -> after two steps PC is back at 0.
    pemu::Core c({ 0x0000, encode_jmp(0) });
    c.step();                          // NOP -> pc=1
    c.step();                          // JMP 0
    assert(c.snapshot().pc == 0);
    assert(c.snapshot().cycle == 2);
}

// JCND
constexpr std::uint16_t encode_jcnd(std::uint16_t reg, std::uint16_t addr) {
    return static_cast<std::uint16_t>(
        (std::uint16_t{0x8} << 12) | ((reg & 0xF) << 8) | (addr & 0xFFu));
}

void jcnd_branches_when_reg_nonzero_and_decrements() {
    pemu::Core c({ encode_jcnd(2, 0) });
    c.set_reg(2, 3);
    c.step();
    assert(c.snapshot().pc == 0);      // branched back
    assert(c.regs()[2] == 2);          // decremented
}

void jcnd_falls_through_when_reg_zero() {
    pemu::Core c({ encode_jcnd(2, 5) });
    c.set_reg(2, 0);
    c.step();
    assert(c.snapshot().pc == 1);      // fell through
    assert(c.regs()[2] == 0);          // NOT decremented below 0
}

void jcnd_loops_correct_number_of_times() {
    // Decrement-and-branch classic: r0 starts at 5, loop back to 0
    // until r0 hits 0. Expect 5 branches + 1 fall-through = 6 steps.
    pemu::Core c({ encode_jcnd(0, 0), 0x0000 });
    c.set_reg(0, 5);
    for (int i = 0; i < 6; ++i) c.step();
    assert(c.snapshot().pc == 1);
    assert(c.regs()[0] == 0);
    assert(c.snapshot().cycle == 6);
}

// PUSH
constexpr std::uint16_t encode_push(std::uint16_t reg) {
    return static_cast<std::uint16_t>(
        (std::uint16_t{0x9} << 12) | ((reg & 0xF) << 8));
}

void push_appends_reg_to_rx_fifo() {
    pemu::Core c({ encode_push(3) });
    c.set_reg(3, 0x5A);
    c.step();
    const auto b = c.pop_rx();
    assert(b.has_value() && *b == 0x5A);
    assert(!c.pop_rx().has_value());   // FIFO now empty
    assert(c.snapshot().pc == 1);
}

void push_overflow_throws() {
    // 16 pushes fill the FIFO; the 17th must throw.
    std::vector<std::uint16_t> prog(17, encode_push(0));
    pemu::Core c(prog);
    c.set_reg(0, 0xAA);
    for (int i = 0; i < 16; ++i) c.step();
    bool threw = false;
    try { c.step(); } catch (const std::runtime_error&) { threw = true; }
    assert(threw);
}

// PULL
constexpr std::uint16_t encode_pull(std::uint16_t reg) {
    return static_cast<std::uint16_t>(
        (std::uint16_t{0xA} << 12) | ((reg & 0xF) << 8));
}

void pull_pops_tx_fifo_into_reg() {
    pemu::Core c({ encode_pull(4) });
    c.push_tx(0x42);
    c.step();
    assert(c.regs()[4] == 0x42);
    assert(c.snapshot().pc == 1);
}

void pull_stalls_when_tx_fifo_empty() {
    // PULL then NOP. FIFO starts empty; host pushes after 6 stall cycles.
    pemu::Core c({ encode_pull(4), 0x0000 });
    c.step();                                  // cycle 1: PULL issues, stalls
    for (int i = 0; i < 5; ++i) c.step();      // cycles 2..6: still stalled
    assert(c.snapshot().cycle == 6);
    assert(c.snapshot().pc == 1);              // PC advanced at issue, no further
    c.push_tx(0x99);                           // host provides data
    c.step();                                  // cycle 7: pull completes
    assert(c.regs()[4] == 0x99);
    c.step();                                  // cycle 8: NOP fetches
    assert(c.snapshot().pc == 2);
    assert(c.snapshot().cycle == 8);
}

// IRQ
constexpr std::uint16_t encode_irq(std::uint16_t n) {
    return static_cast<std::uint16_t>(
        (std::uint16_t{0xB} << 12) | ((n & 0xF) << 8));
}

void irq_sets_bit_in_irq_lines() {
    pemu::Core c({ encode_irq(3) });
    c.step();
    assert(c.irq_lines() == (1u << 3));
    assert(c.snapshot().pc == 1);
}

void irq_lines_accumulate_and_can_be_cleared() {
    pemu::Core c({ encode_irq(1), encode_irq(4) });
    c.run(2);
    assert(c.irq_lines() == ((1u << 1) | (1u << 4)));
    c.clear_irq(1);
    assert(c.irq_lines() == (1u << 4));
    c.clear_irq(4);
    assert(c.irq_lines() == 0);
    // Clearing an out-of-range line is a silent no-op.
    c.clear_irq(200);
    assert(c.irq_lines() == 0);
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
    // SHIFT
    shift_left_by_one_doubles_low_value();
    shift_right_by_two_halves_twice();
    shift_left_drops_high_bits();
    shift_by_zero_is_noop_but_advances_state();
    shift_by_more_than_eight_clears_register();
    // IN
    in_samples_high_pin_into_reg_bit0();
    in_sampling_zero_clears_only_bit0();
    in_releases_output_enable_on_sampled_pin();
    in_invalid_register_throws();
    // DELAY
    delay_of_one_behaves_like_single_cycle_instruction();
    delay_of_zero_clamps_to_one_cycle();
    delay_consumes_exactly_n_cycles();
    delay_via_run_helper_matches_step_by_step();
    delay_max_encodable_value();
    // WAIT
    wait_returns_in_one_cycle_when_condition_already_met();
    wait_times_out_after_exactly_timeout_cycles();
    wait_exits_early_when_pin_matches_mid_stall();
    wait_forever_with_timeout_zero_never_times_out();
    wait_releases_oe_on_pin_at_issue();
    // JMP
    jmp_sets_pc_directly();
    // JCND
    jcnd_branches_when_reg_nonzero_and_decrements();
    jcnd_falls_through_when_reg_zero();
    jcnd_loops_correct_number_of_times();
    // PUSH
    push_appends_reg_to_rx_fifo();
    push_overflow_throws();
    // PULL
    pull_pops_tx_fifo_into_reg();
    pull_stalls_when_tx_fifo_empty();
    // IRQ
    irq_sets_bit_in_irq_lines();
    irq_lines_accumulate_and_can_be_cleared();

    std::cout << "pemu_model_tests: all tests passed\n";
    return 0;
}
