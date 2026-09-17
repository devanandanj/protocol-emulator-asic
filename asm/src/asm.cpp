#include "pemu/asm.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace pemu::assembler {

namespace {

std::string strip_comments_and_trim(std::string s) {
    for (char delim : {';', '#'}) {
        const auto pos = s.find(delim);
        if (pos != std::string::npos) s.resize(pos);
    }
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

AssembleResult assemble(std::istream& in) {
    AssembleResult r;
    std::string    line;
    std::size_t    line_no = 0;

    while (std::getline(in, line)) {
        ++line_no;
        line = strip_comments_and_trim(std::move(line));
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string        mnemonic;
        ss >> mnemonic;
        mnemonic = to_lower(std::move(mnemonic));

        // Phase 5 TODO: implement Set, Out, Shift, In, Wait, Delay,
        // Jmp, Jcnd, Push, Pull, Irq. For now the assembler only
        // knows NOP; that's enough for the scaffold to compile
        // and for the round-trip test to pass.
        if (mnemonic == "nop") {
            r.words.push_back(0x0000);
        } else {
            std::ostringstream err;
            err << "line " << line_no << ": unsupported mnemonic '"
                << mnemonic << "' (Phase 5 TODO)";
            r.errors.push_back(err.str());
        }
    }
    return r;
}

void write_hex(const std::vector<std::uint16_t>& words, std::ostream& out) {
    const auto flags = out.flags();
    out << std::hex << std::setfill('0');
    for (auto w : words) out << std::setw(4) << w << '\n';
    out.flags(flags);
}

} // namespace pemu::assembler
