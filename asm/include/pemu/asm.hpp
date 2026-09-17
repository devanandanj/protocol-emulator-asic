// PEmu assembler - text -> u16 machine code.
//
// Phase 5 fills this in properly. For now the API and skeleton exist
// so the build wires up and we have somewhere to hang tests.

#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace pemu::assembler {

struct AssembleResult {
    std::vector<std::uint16_t> words;
    std::vector<std::string>   errors;   // human-readable, "line N: ..." style

    bool ok() const noexcept { return errors.empty(); }
};

// Read a .pemu source stream and produce the encoded program.
// Blank lines and everything after ';' or '#' on a line is ignored.
AssembleResult assemble(std::istream& in);

// One u16 per line, lowercase four-digit hex. Matches pemu_sim's loader.
void write_hex(const std::vector<std::uint16_t>& words, std::ostream& out);

} // namespace pemu::assembler
