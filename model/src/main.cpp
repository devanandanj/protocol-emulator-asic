// pemu_sim: run a PEmu program to completion and dump a per-cycle trace.
//
// usage:  pemu_sim <program.hex> <trace.tsv> [max_cycles]
//
// program.hex   one instruction word per line, in hex ("nop" == "0000").
//               Lines starting with '#' and blank lines are ignored.
// trace.tsv     tab-separated columns:
//                 cycle  pc  r0..r7  pin_out  pin_oe
//               Cocotb testbenches read this and compare against RTL.

#include "pemu/isa.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::uint16_t> load_program(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open program: " + path);
    }
    std::vector<std::uint16_t> words;
    std::string line;
    while (std::getline(in, line)) {
        // Strip comments after '#' and trim.
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        std::istringstream ss(line);
        std::string tok;
        if (!(ss >> tok)) continue;
        words.push_back(static_cast<std::uint16_t>(std::stoul(tok, nullptr, 16)));
    }
    return words;
}

void write_header(std::ostream& out) {
    out << "# cycle\tpc\tr0\tr1\tr2\tr3\tr4\tr5\tr6\tr7\tpin_out\tpin_oe\n";
}

void write_row(std::ostream& out, const pemu::TraceRecord& rec) {
    out << rec.cycle << '\t' << rec.pc;
    for (auto r : rec.regs) out << '\t' << static_cast<int>(r);
    out << '\t' << rec.pin_out << '\t' << rec.pin_oe << '\n';
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <program.hex> <trace.tsv> [max_cycles]\n";
        return 2;
    }
    const std::size_t max_cycles = (argc >= 4) ? std::stoul(argv[3]) : 1024u;

    try {
        auto program = load_program(argv[1]);
        if (program.empty()) {
            std::cerr << "error: program is empty\n";
            return 1;
        }

        pemu::Core core(std::move(program));
        std::ofstream out(argv[2]);
        if (!out) {
            throw std::runtime_error(std::string("cannot open trace: ") + argv[2]);
        }
        write_header(out);
        write_row(out, core.snapshot());
        for (std::size_t i = 0; i < max_cycles; ++i) {
            core.step();
            write_row(out, core.snapshot());
        }
        std::cerr << "ran " << max_cycles << " cycles OK\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
