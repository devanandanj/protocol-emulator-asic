#include "pemu/asm.hpp"
#include "pemu/isa.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace pemu::assembler {

namespace {

using u16 = std::uint16_t;
using u32 = std::uint32_t;
using LabelTable = std::unordered_map<std::string, u16>;

// -----------------------------------------------------------------
//  Lexing helpers
// -----------------------------------------------------------------

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

bool is_ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool is_valid_label(const std::string& s) {
    if (s.empty() || !is_ident_start(s[0])) return false;
    return std::all_of(s.begin() + 1, s.end(), is_ident_char);
}

// Split "3, r0 , right" into {"3", "r0", "right"}. Whitespace-tolerant.
std::vector<std::string> split_operands(const std::string& s) {
    std::vector<std::string> out;
    std::string curr;
    auto push_curr = [&] {
        // Trim spaces from curr.
        const auto not_space = [](unsigned char c) { return !std::isspace(c); };
        curr.erase(curr.begin(), std::find_if(curr.begin(), curr.end(), not_space));
        curr.erase(std::find_if(curr.rbegin(), curr.rend(), not_space).base(), curr.end());
        if (!curr.empty()) out.push_back(curr);
        curr.clear();
    };
    for (char c : s) {
        if (c == ',') push_curr();
        else          curr.push_back(c);
    }
    push_curr();
    return out;
}

// -----------------------------------------------------------------
//  Line model
// -----------------------------------------------------------------

struct Line {
    std::size_t              line_no{};
    std::string              label;      // may be empty
    std::string              mnemonic;   // may be empty (label-only line)
    std::vector<std::string> operands;
};

// Parse one raw source line. Returns std::nullopt if the line is
// blank/comment-only. Pushes to `errors` on syntax problems.
std::optional<Line> parse_line(const std::string& raw, std::size_t line_no,
                               std::vector<std::string>& errors) {
    std::string s = strip_comments_and_trim(raw);
    if (s.empty()) return std::nullopt;

    Line line;
    line.line_no = line_no;

    // Optional "label:" prefix.
    const auto colon = s.find(':');
    if (colon != std::string::npos) {
        std::string label = s.substr(0, colon);
        // Trim label of any trailing whitespace picked up before ':'.
        const auto not_space = [](unsigned char c) { return !std::isspace(c); };
        label.erase(std::find_if(label.rbegin(), label.rend(), not_space).base(), label.end());
        if (!is_valid_label(label)) {
            std::ostringstream err;
            err << "line " << line_no << ": invalid label '" << label << "'";
            errors.push_back(err.str());
            return std::nullopt;
        }
        line.label = std::move(label);
        s = strip_comments_and_trim(s.substr(colon + 1));
    }

    if (s.empty()) return line;  // label-only line

    // Split into mnemonic + rest.
    std::size_t sp = 0;
    while (sp < s.size() && !std::isspace(static_cast<unsigned char>(s[sp]))) ++sp;
    line.mnemonic = to_lower(s.substr(0, sp));
    if (sp < s.size()) {
        line.operands = split_operands(strip_comments_and_trim(s.substr(sp)));
    }
    return line;
}

// -----------------------------------------------------------------
//  Operand parsing
// -----------------------------------------------------------------

u32 parse_number(const std::string& s) {
    if (s.empty()) throw std::runtime_error("expected number, got empty operand");
    int base = 10;
    std::size_t start = 0;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        start = 2;
    } else if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
        base = 2;
        start = 2;
    }
    std::size_t consumed = 0;
    u32 v = 0;
    try {
        v = static_cast<u32>(std::stoul(s.substr(start), &consumed, base));
    } catch (const std::exception&) {
        throw std::runtime_error("invalid number: '" + s + "'");
    }
    if (consumed != s.size() - start) {
        throw std::runtime_error("invalid number: '" + s + "'");
    }
    return v;
}

u32 parse_register(const std::string& s) {
    if (s.size() < 2 || (s[0] != 'r' && s[0] != 'R')) {
        throw std::runtime_error("expected register (r0..r7), got '" + s + "'");
    }
    // Only accept plain decimal digits after 'r' — no 0x, no 0b.
    const std::string tail = s.substr(1);
    if (tail.empty() ||
        !std::all_of(tail.begin(), tail.end(),
                     [](unsigned char c) { return std::isdigit(c); })) {
        throw std::runtime_error("expected register (r0..r7), got '" + s + "'");
    }
    const u32 v = parse_number(tail);
    if (v >= pemu::kNumRegs) {
        throw std::runtime_error("register out of range (0.." +
                                 std::to_string(pemu::kNumRegs - 1) + "): '" + s + "'");
    }
    return v;
}

u32 parse_direction(const std::string& s) {
    const std::string ls = to_lower(s);
    if (ls == "left"  || ls == "l" || ls == "0") return 0;
    if (ls == "right" || ls == "r" || ls == "1") return 1;
    throw std::runtime_error("expected 'left' or 'right', got '" + s + "'");
}

// Number or label reference. Labels win over numeric parsing when the
// text is a valid identifier.
u32 parse_addr(const std::string& s, const LabelTable& labels) {
    if (!s.empty() && is_ident_start(s[0])) {
        auto it = labels.find(s);
        if (it != labels.end()) return static_cast<u32>(it->second);
        throw std::runtime_error("undefined label '" + s + "'");
    }
    return parse_number(s);
}

// -----------------------------------------------------------------
//  Per-opcode encoders
// -----------------------------------------------------------------

void require_operand_count(const std::vector<std::string>& ops, std::size_t n,
                           const char* mnemonic, const char* signature) {
    if (ops.size() != n) {
        std::ostringstream err;
        err << mnemonic << " takes " << n << " operand"
            << (n == 1 ? "" : "s") << " (" << signature << "), got " << ops.size();
        throw std::runtime_error(err.str());
    }
}

void require_range(u32 v, u32 max, const char* field) {
    if (v > max) {
        std::ostringstream err;
        err << field << " out of range (0.." << max << "): " << v;
        throw std::runtime_error(err.str());
    }
}

u16 encode_nop(const std::vector<std::string>& ops) {
    require_operand_count(ops, 0, "NOP", "");
    return 0x0000;
}

u16 encode_set(const std::vector<std::string>& ops) {
    require_operand_count(ops, 2, "SET", "pin, val");
    const u32 pin = parse_number(ops[0]);
    const u32 val = parse_number(ops[1]);
    require_range(pin, 0xF,  "SET pin");
    require_range(val, 0xFF, "SET val");
    return static_cast<u16>((u16{0x1} << 12) | (pin << 8) | val);
}

u16 encode_out(const std::vector<std::string>& ops) {
    require_operand_count(ops, 2, "OUT", "pin, reg");
    const u32 pin = parse_number(ops[0]);
    const u32 reg = parse_register(ops[1]);
    require_range(pin, 0xF, "OUT pin");
    return static_cast<u16>((u16{0x2} << 12) | (pin << 8) | (reg << 4));
}

u16 encode_shift(const std::vector<std::string>& ops) {
    require_operand_count(ops, 3, "SHIFT", "reg, dir, count");
    const u32 reg   = parse_register(ops[0]);
    const u32 dir   = parse_direction(ops[1]);
    const u32 count = parse_number(ops[2]);
    require_range(count, 0xF, "SHIFT count");
    return static_cast<u16>((u16{0x3} << 12) | (reg << 8) | (dir << 7) | count);
}

u16 encode_in(const std::vector<std::string>& ops) {
    require_operand_count(ops, 2, "IN", "pin, reg");
    const u32 pin = parse_number(ops[0]);
    const u32 reg = parse_register(ops[1]);
    require_range(pin, 0xF, "IN pin");
    return static_cast<u16>((u16{0x4} << 12) | (pin << 8) | (reg << 4));
}

u16 encode_wait(const std::vector<std::string>& ops) {
    require_operand_count(ops, 3, "WAIT", "pin, val, timeout");
    const u32 pin = parse_number(ops[0]);
    const u32 val = parse_number(ops[1]);
    const u32 tmo = parse_number(ops[2]);
    require_range(pin, 0xF,  "WAIT pin");
    require_range(val, 1,    "WAIT val");
    require_range(tmo, 0x7F, "WAIT timeout");
    return static_cast<u16>((u16{0x5} << 12) | (pin << 8) | (val << 7) | tmo);
}

u16 encode_delay(const std::vector<std::string>& ops) {
    require_operand_count(ops, 1, "DELAY", "n");
    const u32 n = parse_number(ops[0]);
    require_range(n, 0xFFF, "DELAY n");
    return static_cast<u16>((u16{0x6} << 12) | n);
}

u16 encode_jmp(const std::vector<std::string>& ops, const LabelTable& labels) {
    require_operand_count(ops, 1, "JMP", "addr | label");
    const u32 addr = parse_addr(ops[0], labels);
    require_range(addr, 0xFFF, "JMP addr");
    return static_cast<u16>((u16{0x7} << 12) | addr);
}

u16 encode_jcnd(const std::vector<std::string>& ops, const LabelTable& labels) {
    require_operand_count(ops, 2, "JCND", "reg, addr | label");
    const u32 reg  = parse_register(ops[0]);
    const u32 addr = parse_addr(ops[1], labels);
    require_range(addr, 0xFF, "JCND addr");
    return static_cast<u16>((u16{0x8} << 12) | (reg << 8) | addr);
}

u16 encode_push(const std::vector<std::string>& ops) {
    require_operand_count(ops, 1, "PUSH", "reg");
    const u32 reg = parse_register(ops[0]);
    return static_cast<u16>((u16{0x9} << 12) | (reg << 8));
}

u16 encode_pull(const std::vector<std::string>& ops) {
    require_operand_count(ops, 1, "PULL", "reg");
    const u32 reg = parse_register(ops[0]);
    return static_cast<u16>((u16{0xA} << 12) | (reg << 8));
}

u16 encode_irq(const std::vector<std::string>& ops) {
    require_operand_count(ops, 1, "IRQ", "n");
    const u32 n = parse_number(ops[0]);
    require_range(n, 0xF, "IRQ n");
    return static_cast<u16>((u16{0xB} << 12) | (n << 8));
}

u16 encode_ldi(const std::vector<std::string>& ops) {
    require_operand_count(ops, 2, "LDI", "reg, imm");
    const u32 reg = parse_register(ops[0]);
    const u32 imm = parse_number(ops[1]);
    require_range(imm, 0xFF, "LDI imm");
    return static_cast<u16>((u16{0xC} << 12) | (reg << 8) | imm);
}

}  // namespace

// -----------------------------------------------------------------
//  Two-pass driver
// -----------------------------------------------------------------

AssembleResult assemble(std::istream& in) {
    AssembleResult r;

    // Parse all lines up-front.
    std::vector<Line> lines;
    std::string raw;
    std::size_t line_no = 0;
    while (std::getline(in, raw)) {
        ++line_no;
        if (auto l = parse_line(raw, line_no, r.errors); l.has_value()) {
            lines.push_back(std::move(*l));
        }
    }

    // Pass 1: label table. Word index only advances for lines with
    // an instruction — label-only lines share the address of the
    // next instruction.
    LabelTable labels;
    u16 pc = 0;
    for (const auto& line : lines) {
        if (!line.label.empty()) {
            if (labels.count(line.label) != 0u) {
                std::ostringstream err;
                err << "line " << line.line_no << ": duplicate label '" << line.label << "'";
                r.errors.push_back(err.str());
            } else {
                labels.emplace(line.label, pc);
            }
        }
        if (!line.mnemonic.empty()) ++pc;
    }

    if (!r.ok()) return r;

    // Pass 2: encode.
    for (const auto& line : lines) {
        if (line.mnemonic.empty()) continue;
        try {
            u16 word = 0;
            const std::string& m = line.mnemonic;
            if      (m == "nop")   word = encode_nop  (line.operands);
            else if (m == "set")   word = encode_set  (line.operands);
            else if (m == "out")   word = encode_out  (line.operands);
            else if (m == "shift") word = encode_shift(line.operands);
            else if (m == "in")    word = encode_in   (line.operands);
            else if (m == "wait")  word = encode_wait (line.operands);
            else if (m == "delay") word = encode_delay(line.operands);
            else if (m == "jmp")   word = encode_jmp  (line.operands, labels);
            else if (m == "jcnd")  word = encode_jcnd (line.operands, labels);
            else if (m == "push")  word = encode_push (line.operands);
            else if (m == "pull")  word = encode_pull (line.operands);
            else if (m == "irq")   word = encode_irq  (line.operands);
            else if (m == "ldi")   word = encode_ldi  (line.operands);
            else throw std::runtime_error("unknown mnemonic '" + m + "'");
            r.words.push_back(word);
        } catch (const std::exception& e) {
            std::ostringstream err;
            err << "line " << line.line_no << ": " << e.what();
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

}  // namespace pemu::assembler
