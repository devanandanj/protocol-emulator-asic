#include "pemu/isa.hpp"

#include <stdexcept>
#include <utility>

using u32 = std::uint32_t;
using u16 = std::uint16_t;
using u8 = std::uint8_t;

namespace {
    void check_reg_in_range(u32 r) {
        if (r >= pemu::kNumRegs) {
            throw std::runtime_error("PEmu: Register index out of range");
        }
    }
}

namespace pemu {

Core::Core(std::vector<u16> program) noexcept
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
    stall_remaining_ = 0;
    waiting_for_pin_ = false;
    wait_forever_    = false;
    wait_pin_        = 0;
    wait_val_        = 0;
}

void Core::step() {
    if (waiting_for_pin_) {
        const u32 curr = (pin_in_ >> wait_pin_) & 0x1u;
        if (curr == static_cast<u32>(wait_val_)) {
            waiting_for_pin_ = false;
            wait_forever_    = false;
            stall_remaining_ = 0;
        } else if (wait_forever_) {
            //
        } else {
            if (stall_remaining_ == 1u) {
                waiting_for_pin_ = false;
            }
            --stall_remaining_;
        }
        ++cycles_;
        return;
    }

    if (stall_remaining_ > 0u) {
        --stall_remaining_;
        ++cycles_;
        return;
    }

    if (pc_ >= program_.size()) {
        throw std::runtime_error("PEmu: PC out of program bounds");
    }

    const Instruction ins = Instruction::decode(program_[pc_]);

    switch (ins.op) {
        case Op::Nop:
            ++pc_;
            break;

        // Each case here should update pin_out_/pin_oe_/regs_/pc_/FIFOs
        // according to docs/isa.md, then fall through to cycles_++.
        case Op::Set: {
            const u32 pin = (ins.operand >> 8) & 0xFu;
            const u32 val =  ins.operand       & 0x1u;
            const u32 mask = u32{1} << pin;
            if (val) pin_out_ |= mask;
            else     pin_out_ &= ~mask;
            pin_oe_ |= mask;
            ++pc_;
            break;
        }
        case Op::Out: {
            const u32 pin = (ins.operand >> 8) & 0xFu;
            const u32 reg = (ins.operand >> 4) & 0xFu;
            check_reg_in_range(reg);
            const u32 bit = regs_[reg] & 0x1u;
            const u32 mask = u32{1} << pin;
            if (bit) pin_out_ |= mask;
            else     pin_out_ &= ~mask;
            pin_oe_ |= mask;
            ++pc_;
            break;
        }

        case Op::Shift: {
            const u32 reg = (ins.operand >> 8) & 0xFu;
            const u32 dir = (ins.operand >> 7) & 0x1u;
            const u32 count = ins.operand      & 0xFu;
            check_reg_in_range(reg);
            const u32 n = count > 8u ? 8u : count;
            const u32 r = regs_[reg];
            regs_[reg] = (dir == 0) ? static_cast<u8>((r << n) & 0xFFu)
            : static_cast<u8>(r >> n);
            ++pc_;
            break;
        }

        case Op::In: {
            const u32 pin = (ins.operand >> 8) & 0xFu;
            const u32 reg = (ins.operand >> 4) & 0xFu;
            check_reg_in_range(reg);
            const u32 mask = u32{1} << pin;
            pin_oe_ &= ~mask;
            const u32 bit = (pin_in_ >> pin) & 0x1u;
            regs_[reg] = static_cast<u8>((regs_[reg] & 0xFEu) | bit);
            ++pc_;
            break;
        }

        case Op::Wait: {
            const u32 pin = (ins.operand >> 8) & 0xFu;
            const u32 val = (ins.operand >> 7) & 0x1u;
            const u32 timeout = ins.operand & 0x7Fu;
            const u32 mask = u32{1} << pin;
            pin_oe_ &= ~mask;
            const u32 curr = (pin_in_ >> pin) & 0x1u;
            if (curr == val) {
                //
            }
            else if (timeout == 0u) {
                waiting_for_pin_ = true;
                wait_forever_    = true;
                wait_pin_        = static_cast<u8>(pin);
                wait_val_        = static_cast<u8>(val);
            } else if (timeout == 1u) {
                //
            } else {
                waiting_for_pin_ = true;
                wait_forever_    = false;
                wait_pin_        = static_cast<u8>(pin);
                wait_val_        = static_cast<u8>(val);
                stall_remaining_ = static_cast<u16>(timeout - 1u);
            }
            ++pc_;
            break;
        }

        case Op::Delay: {
            const u32 n = ins.operand & 0xFFFu;
            const u32 effective = n == 0u ? 1u : n;
            stall_remaining_ = static_cast<u16>(effective - 1u);
            ++pc_;
            break;
        }

        // Todo:
        case Op::Jmp:
        case Op::Jcnd:
        case Op::Push:
        case Op::Pull:
        case Op::Irq:
            throw std::runtime_error("PEmu: opcode not yet implemented");
    }

    ++cycles_;
}

void Core::run(std::size_t max_cycles) {
    for (std::size_t i = 0; i < max_cycles; ++i) {
        step();
    }
}

void Core::set_reg(std::size_t idx, u8 val) {
    if (idx >= kNumRegs) {
        throw std::runtime_error("PEmu::set_reg: register index out of range");
    }
    regs_[idx] = val;
}

void Core::push_tx(u8 byte) {
    tx_fifo_.push_back(byte);
}

std::optional<u8> Core::pop_rx() {
    if (rx_fifo_.empty()) return std::nullopt;
    const u8 b = rx_fifo_.front();
    rx_fifo_.pop_front();
    return b;
}

TraceRecord Core::snapshot() const noexcept {
    return TraceRecord{ cycles_, pc_, regs_, pin_out_, pin_oe_ };
}

}
