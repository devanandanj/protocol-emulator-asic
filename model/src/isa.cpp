#include "pemu/isa.hpp"

#include <stdexcept>
#include <utility>

namespace pemu {

Core::Core(std::vector<std::uint16_t> program) noexcept
    : program_(std::move(program)) {
    reset();
}

void Core::reset() noexcept {
    pc_      = 0;
    regs_.fill(0);
    tx_fifo_.clear();
    rx_fifo_.clear();
    pin_in_  = 0;
    pin_out_ = 0;
    pin_oe_  = 0;
    cycles_  = 0;
}

void Core::step() {
    if (pc_ >= program_.size()) {
        throw std::runtime_error("PEmu: PC out of program bounds");
    }

    const Instruction ins = Instruction::decode(program_[pc_]);

    switch (ins.op) {
        case Op::Nop:
            ++pc_;
            break;

        // TODO(phase 1): implement the rest of the ISA.
        // Each case here should update pin_out_/pin_oe_/regs_/pc_/FIFOs
        // according to docs/isa.md, then fall through to cycles_++.
        case Op::Set:
        case Op::Out:
        case Op::Shift:
        case Op::In:
        case Op::Wait:
        case Op::Delay:
        case Op::Jmp:
        case Op::Jcnd:
        case Op::Push:
        case Op::Pull:
        case Op::Irq:
            throw std::runtime_error("PEmu: opcode not yet implemented (Phase 1 TODO)");
    }

    ++cycles_;
}

void Core::run(std::size_t max_cycles) {
    for (std::size_t i = 0; i < max_cycles; ++i) {
        step();
    }
}

void Core::push_tx(std::uint8_t byte) {
    tx_fifo_.push_back(byte);
}

std::optional<std::uint8_t> Core::pop_rx() {
    if (rx_fifo_.empty()) return std::nullopt;
    const std::uint8_t b = rx_fifo_.front();
    rx_fifo_.pop_front();
    return b;
}

TraceRecord Core::snapshot() const noexcept {
    return TraceRecord{ cycles_, pc_, regs_, pin_out_, pin_oe_ };
}

} // namespace pemu
