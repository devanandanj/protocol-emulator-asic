// Force assert() to be live even in Release builds — see note in test_isa.cpp.
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "pemu/asm.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

namespace {

using pemu::assembler::assemble;
using pemu::assembler::write_hex;

pemu::assembler::AssembleResult asm_str(const std::string& src) {
    std::istringstream in(src);
    return assemble(in);
}

// -----------------------------------------------------------------
// Existing tests (kept)
// -----------------------------------------------------------------

void empty_source_produces_no_words() {
    auto r = asm_str("");
    assert(r.ok());
    assert(r.words.empty());
}

void nop_encodes_to_zero() {
    auto r = asm_str("nop\nNOP\n  nop  ; with a comment\n");
    assert(r.ok());
    assert(r.words.size() == 3);
    for (auto w : r.words) assert(w == 0x0000);
}

void unknown_mnemonic_reports_line() {
    auto r = asm_str("nop\nxyzzy foo\n");
    assert(!r.ok());
    assert(r.errors.size() == 1);
    assert(r.errors[0].find("line 2") != std::string::npos);
}

void write_hex_pads_to_four_digits() {
    std::ostringstream out;
    write_hex({0x0000, 0x00A5, 0xBEEF}, out);
    assert(out.str() == "0000\n00a5\nbeef\n");
}

// -----------------------------------------------------------------
// Per-opcode encoding
// -----------------------------------------------------------------

void set_encodes_as_expected() {
    auto r = asm_str("set 3, 1");
    assert(r.ok() && r.words.size() == 1);
    assert(r.words[0] == 0x1301);
}

void out_accepts_register_mnemonic() {
    auto r = asm_str("out 5, r2");
    assert(r.ok() && r.words.size() == 1);
    // op=2, pin=5, reg=2 -> 0x2 5 2 0
    assert(r.words[0] == 0x2520);
}

void shift_direction_keywords() {
    auto r = asm_str("shift r2, right, 4\nshift r2, left, 4\n");
    assert(r.ok() && r.words.size() == 2);
    // op=3, reg=2, dir=1, count=4 -> 0x32 84 (0011 0010 1000 0100)
    assert(r.words[0] == 0x3284);
    // op=3, reg=2, dir=0, count=4 -> 0x3204
    assert(r.words[1] == 0x3204);
}

void delay_accepts_hex_and_binary_numbers() {
    auto r = asm_str("delay 100\ndelay 0x64\ndelay 0b1100100\n");
    assert(r.ok() && r.words.size() == 3);
    for (auto w : r.words) assert(w == 0x6064);   // 100 = 0x64
}

void wait_encodes_all_three_fields() {
    auto r = asm_str("wait 5, 1, 20");
    assert(r.ok() && r.words.size() == 1);
    // op=5, pin=5, val=1, timeout=20 -> 0x5 5 0x94  (5<<12 | 5<<8 | 1<<7 | 20)
    const std::uint16_t expected = (0x5u << 12) | (5u << 8) | (1u << 7) | 20u;
    assert(r.words[0] == expected);
}

void jmp_resolves_backward_label() {
    auto r = asm_str("start:\n  nop\n  jmp start\n");
    assert(r.ok() && r.words.size() == 2);
    assert(r.words[0] == 0x0000);         // nop
    assert(r.words[1] == 0x7000);         // jmp start (start = 0)
}

void jmp_resolves_forward_label() {
    auto r = asm_str("  jmp end\n  nop\nend:  nop\n");
    assert(r.ok() && r.words.size() == 3);
    assert(r.words[0] == (0x7u << 12 | 2));   // jmp end (end = 2)
    assert(r.words[1] == 0x0000);
    assert(r.words[2] == 0x0000);
}

void jcnd_addr_out_of_range_reports_error() {
    // jcnd's addr field is 8 bits; 256 is out of range.
    auto r = asm_str("jcnd r0, 256");
    assert(!r.ok());
    assert(r.errors.size() == 1);
}

void push_pull_encoding() {
    auto r = asm_str("push r3\npull r5\n");
    assert(r.ok() && r.words.size() == 2);
    assert(r.words[0] == 0x9300);
    assert(r.words[1] == 0xA500);
}

void irq_encoding() {
    auto r = asm_str("irq 7");
    assert(r.ok() && r.words.size() == 1);
    assert(r.words[0] == 0xB700);
}

void ldi_encoding() {
    auto r = asm_str("ldi r3, 0x5A");
    assert(r.ok() && r.words.size() == 1);
    assert(r.words[0] == 0xC35A);
}

void ldi_imm_out_of_range_reports_error() {
    auto r = asm_str("ldi r0, 256");
    assert(!r.ok());
    assert(r.errors[0].find("LDI imm") != std::string::npos);
}

void out_od_encoding() {
    auto r = asm_str("out_od 3, r2");
    assert(r.ok() && r.words.size() == 1);
    // op=E, pin=3, reg=2 -> 0xE320
    assert(r.words[0] == 0xE320);
}

void rot_encoding() {
    auto r = asm_str("rot r0, right, 1");
    assert(r.ok() && r.words.size() == 1);
    // op=D, reg=0, dir=1, count=1 -> 0xD081
    assert(r.words[0] == 0xD081);
}

// -----------------------------------------------------------------
// Error cases
// -----------------------------------------------------------------

void register_out_of_range_reports_error() {
    auto r = asm_str("out 0, r8");
    assert(!r.ok());
    assert(r.errors[0].find("register") != std::string::npos);
}

void wrong_operand_count_reports_error() {
    auto r = asm_str("set 3");            // needs 2, given 1
    assert(!r.ok());
    assert(r.errors[0].find("SET") != std::string::npos);
}

void duplicate_label_reports_error() {
    auto r = asm_str("start:\n  nop\nstart:\n  nop\n");
    assert(!r.ok());
    assert(r.errors[0].find("duplicate label") != std::string::npos);
}

void undefined_label_reports_error() {
    auto r = asm_str("jmp nowhere");
    assert(!r.ok());
    assert(r.errors[0].find("undefined label") != std::string::npos);
}

// -----------------------------------------------------------------
// Integration: a UART-TX-shaped program assembles cleanly
// -----------------------------------------------------------------

void uart_tx_shaped_program_assembles() {
    const std::string prog =
        "; a rough UART TX skeleton, not tuned for real baud\n"
        "start:  pull  r0             ; wait for host byte\n"
        "        set   0, 0           ; start bit low\n"
        "        delay 10\n"
        "        out   0, r0          ; drive bit 0\n"
        "        delay 10\n"
        "        shift r0, right, 1\n"
        "        set   0, 1           ; stop bit high\n"
        "        delay 10\n"
        "        jmp   start\n";
    auto r = asm_str(prog);
    for (const auto& e : r.errors) std::cerr << e << '\n';
    assert(r.ok());
    assert(r.words.size() == 9);
    assert(r.words[0] == 0xA000);              // pull r0
    assert(r.words[8] == 0x7000);              // jmp start (start = 0)
}

}  // namespace

int main() {
    // existing
    empty_source_produces_no_words();
    nop_encodes_to_zero();
    unknown_mnemonic_reports_line();
    write_hex_pads_to_four_digits();
    // encoding
    set_encodes_as_expected();
    out_accepts_register_mnemonic();
    shift_direction_keywords();
    delay_accepts_hex_and_binary_numbers();
    wait_encodes_all_three_fields();
    jmp_resolves_backward_label();
    jmp_resolves_forward_label();
    jcnd_addr_out_of_range_reports_error();
    push_pull_encoding();
    irq_encoding();
    ldi_encoding();
    ldi_imm_out_of_range_reports_error();
    out_od_encoding();
    // error cases
    register_out_of_range_reports_error();
    wrong_operand_count_reports_error();
    duplicate_label_reports_error();
    undefined_label_reports_error();
    // integration
    uart_tx_shaped_program_assembles();

    std::cout << "pemu_asm_tests: all tests passed\n";
    return 0;
}
