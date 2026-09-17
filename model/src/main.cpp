// pemu_sim: run a PEmu program to completion and dump a per-cycle trace.
//
// usage:
//   pemu_sim <program.hex> <trace.tsv> [max_cycles]
//            [--tx=b0,b1,...]
//            [--pin-events=cycle:mask,cycle:mask,...]
//
// program.hex   one instruction word per line, in hex ("nop" == "0000").
//               Lines starting with '#' and blank lines are ignored.
// trace.tsv     tab-separated columns:
//                 cycle  pc  r0..r7  pin_out  pin_oe
//               Cocotb testbenches read this and compare against RTL.
// max_cycles    positional integer (default 1024).
// --tx=...      comma-separated bytes to push into the TX FIFO
//               before the program starts, so PULL instructions
//               have data to consume.
// --pin-events  comma-separated (cycle, pin_in mask) pairs. Before
//               each step at cycle C, if an event with time <= C has
//               not yet been applied, its mask becomes the new
//               pin_in. Mask is a 24-bit value; cycle and mask accept
//               decimal, 0x hex, or 0b binary.
// Literals for --tx and --pin-events: decimal, 0xFF hex, 0b1101 binary.

#include "pemu/isa.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
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

std::uint32_t parse_uint_literal(const std::string& tok) {
    int base = 10;
    std::size_t start = 0;
    if (tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
        base = 16; start = 2;
    } else if (tok.size() > 2 && tok[0] == '0' && (tok[1] == 'b' || tok[1] == 'B')) {
        base = 2;  start = 2;
    }
    return static_cast<std::uint32_t>(std::stoul(tok.substr(start), nullptr, base));
}

std::uint32_t parse_byte_literal(const std::string& tok) {
    const auto v = parse_uint_literal(tok);
    if (v > 0xFFu) {
        throw std::runtime_error("byte > 255: '" + tok + "'");
    }
    return v;
}

struct PinEvent {
    std::uint64_t cycle;
    std::uint32_t mask;
};

std::vector<std::uint8_t> parse_tx_list(std::string_view csv) {
    std::vector<std::uint8_t> out;
    std::string curr;
    auto flush = [&] {
        if (curr.empty()) return;
        out.push_back(static_cast<std::uint8_t>(parse_byte_literal(curr)));
        curr.clear();
    };
    for (char c : csv) {
        if (c == ',') flush();
        else if (!std::isspace(static_cast<unsigned char>(c))) curr.push_back(c);
    }
    flush();
    return out;
}

// Parse "cycle:mask,cycle:mask,..." into a vector of PinEvent.
// Whitespace tolerated. Events are sorted by cycle before returning.
std::vector<PinEvent> parse_pin_events(std::string_view csv) {
    std::vector<PinEvent> out;
    std::string curr;
    auto flush = [&] {
        if (curr.empty()) return;
        const auto colon = curr.find(':');
        if (colon == std::string::npos) {
            throw std::runtime_error("pin-event missing ':': '" + curr + "'");
        }
        const std::uint64_t c = parse_uint_literal(curr.substr(0, colon));
        const std::uint32_t m = parse_uint_literal(curr.substr(colon + 1)) & 0x00FFFFFFu;
        out.push_back({c, m});
        curr.clear();
    };
    for (char c : csv) {
        if (c == ',') flush();
        else if (!std::isspace(static_cast<unsigned char>(c))) curr.push_back(c);
    }
    flush();
    std::sort(out.begin(), out.end(),
              [](const PinEvent& a, const PinEvent& b) { return a.cycle < b.cycle; });
    return out;
}

void print_usage(const char* argv0) {
    std::cerr << "usage: " << argv0
              << " <program.hex> <trace.tsv> [max_cycles]"
              << " [--tx=b0,b1,...]"
              << " [--pin-events=cycle:mask,cycle:mask,...]\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 2;
    }

    const std::string progfile  = argv[1];
    const std::string tracefile = argv[2];
    std::size_t max_cycles = 1024;
    std::vector<std::uint8_t> tx_bytes;
    std::vector<PinEvent>     pin_events;

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        constexpr std::string_view kTxPrefix     = "--tx=";
        constexpr std::string_view kEventsPrefix = "--pin-events=";
        try {
            if (arg.rfind(kTxPrefix, 0) == 0) {
                tx_bytes = parse_tx_list(std::string_view(arg).substr(kTxPrefix.size()));
            } else if (arg.rfind(kEventsPrefix, 0) == 0) {
                pin_events = parse_pin_events(std::string_view(arg).substr(kEventsPrefix.size()));
            } else if (!arg.empty() && std::isdigit(static_cast<unsigned char>(arg[0]))) {
                max_cycles = std::stoul(arg);
            } else {
                std::cerr << "error: unknown option '" << arg << "'\n";
                print_usage(argv[0]);
                return 2;
            }
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << '\n';
            return 2;
        }
    }

    try {
        auto program = load_program(progfile);
        if (program.empty()) {
            std::cerr << "error: program is empty\n";
            return 1;
        }

        pemu::Core core(std::move(program));
        for (auto b : tx_bytes) core.push_tx(b);

        std::ofstream out(tracefile);
        if (!out) {
            throw std::runtime_error("cannot open trace: " + tracefile);
        }
        write_header(out);
        // Apply any events scheduled at cycle 0 before the first snapshot.
        std::size_t next_event = 0;
        while (next_event < pin_events.size() && pin_events[next_event].cycle == 0) {
            core.set_pin_in(pin_events[next_event].mask);
            ++next_event;
        }
        write_row(out, core.snapshot());
        for (std::size_t i = 0; i < max_cycles; ++i) {
            const std::uint64_t now = static_cast<std::uint64_t>(i) + 1u;
            // Apply any events whose cycle number == now, BEFORE stepping.
            // The just-changed pin_in is what this step observes.
            while (next_event < pin_events.size() && pin_events[next_event].cycle == now) {
                core.set_pin_in(pin_events[next_event].mask);
                ++next_event;
            }
            core.step();
            write_row(out, core.snapshot());
        }
        std::cerr << "ran " << max_cycles << " cycles OK";
        if (!tx_bytes.empty())   std::cerr << " (preloaded " << tx_bytes.size() << " TX byte(s))";
        if (!pin_events.empty()) std::cerr << " (" << pin_events.size() << " pin event(s))";
        std::cerr << '\n';
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
