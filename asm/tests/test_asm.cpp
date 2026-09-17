// Force assert() to be live even in Release builds — see note in test_isa.cpp.
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "pemu/asm.hpp"

#include <cassert>
#include <iostream>
#include <sstream>

namespace {

void empty_source_produces_no_words() {
    std::istringstream in("");
    auto r = pemu::assembler::assemble(in);
    assert(r.ok());
    assert(r.words.empty());
}

void nop_encodes_to_zero() {
    std::istringstream in("nop\nNOP\n  nop  ; with a comment\n");
    auto r = pemu::assembler::assemble(in);
    assert(r.ok());
    assert(r.words.size() == 3);
    for (auto w : r.words) assert(w == 0x0000);
}

void unknown_mnemonic_reports_line() {
    std::istringstream in("nop\nxyzzy foo\n");
    auto r = pemu::assembler::assemble(in);
    assert(!r.ok());
    assert(r.errors.size() == 1);
    assert(r.errors[0].find("line 2") != std::string::npos);
}

void write_hex_pads_to_four_digits() {
    std::ostringstream out;
    pemu::assembler::write_hex({0x0000, 0x00A5, 0xBEEF}, out);
    assert(out.str() == "0000\n00a5\nbeef\n");
}

} // namespace

int main() {
    empty_source_produces_no_words();
    nop_encodes_to_zero();
    unknown_mnemonic_reports_line();
    write_hex_pads_to_four_digits();
    std::cout << "pemu_asm_tests: all tests passed\n";
    return 0;
}
