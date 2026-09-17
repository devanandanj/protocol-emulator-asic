// pemuasm: assemble a .pemu source file into a hex program file.
//
// usage:  pemuasm <in.pemu> <out.hex>

#include "pemu/asm.hpp"

#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <in.pemu> <out.hex>\n";
        return 2;
    }

    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "cannot open input: " << argv[1] << '\n';
        return 1;
    }

    const auto result = pemu::assembler::assemble(in);
    for (const auto& e : result.errors) std::cerr << e << '\n';
    if (!result.ok()) return 1;

    std::ofstream out(argv[2]);
    if (!out) {
        std::cerr << "cannot open output: " << argv[2] << '\n';
        return 1;
    }
    pemu::assembler::write_hex(result.words, out);
    std::cerr << "wrote " << result.words.size() << " words to " << argv[2] << '\n';
    return 0;
}
