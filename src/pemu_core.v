/*
 * Copyright (c) 2024 Devanandan J
 * SPDX-License-Identifier: Apache-2.0
 */

`default_nettype none

module pemu_core (
    input   wire          clk,
    input   wire          rst_n,
    // Pin buses are 8 bits — TinyTapeout gives us 8 bidir uio_ pins. The
    // 4-bit pin field in the ISA still allows encoding pins 8..15, but bit
    // [11] is treated as reserved-must-be-zero (see docs/isa.md). All pin
    // index sites in this file slice operand[10:8] to enforce that.
    input   wire  [7:0]   pin_in,
    output  reg   [7:0]   pin_out,
    output  reg   [7:0]   pin_oe,
    // Program loader — only active while rst_n=0.
    // Sequence per instruction: (prog_hi=0, prog_we=1) latches low byte,
    // then (prog_hi=1, prog_we=1) commits {hi,lo} to program_mem[load_addr]
    // and increments load_addr. prog_rst pulses load_addr back to 0.
    input   wire          prog_rst,
    input   wire          prog_we,
    input   wire          prog_hi,
    input   wire  [7:0]   prog_data
);

  localparam [3:0] OP_NOP    = 4'h0;
  localparam [3:0] OP_SET    = 4'h1;
  localparam [3:0] OP_OUT    = 4'h2;
  localparam [3:0] OP_SHIFT  = 4'h3;
  localparam [3:0] OP_IN     = 4'h4;
  localparam [3:0] OP_WAIT   = 4'h5;
  localparam [3:0] OP_DELAY  = 4'h6;
  localparam [3:0] OP_JMP    = 4'h7;
  localparam [3:0] OP_JCND   = 4'h8;
  localparam [3:0] OP_PUSH   = 4'h9;
  localparam [3:0] OP_PULL   = 4'hA;
  localparam [3:0] OP_IRQ    = 4'hB;
  localparam [3:0] OP_LDI    = 4'hC;
  localparam [3:0] OP_ROT    = 4'hD;
  localparam [3:0] OP_OUT_OD = 4'hE;

  // Program memory — 64 words × 16 bits. Loaded by the host via prog_* port
  // while the core is held in reset. No initial contents on real silicon.
  reg[15:0] program_mem[63:0];

  reg [5:0] load_addr;
  reg [7:0] load_lo;
  always @(posedge clk) begin
      if (!rst_n) begin
          if (prog_rst) begin
              load_addr <= 6'd0;
          end else if (prog_we) begin
              if (prog_hi) begin
                  program_mem[load_addr] <= {prog_data, load_lo};
                  load_addr <= load_addr + 6'd1;
              end else begin
                  load_lo <= prog_data;
              end
          end
      end
  end



reg[11:0] pc;

reg[7:0] regs[0:7];
integer j;

// Multi-cycle stall state (mirror of C++ model)
reg [11:0] stall_remaining;
reg        waiting_for_pin;
reg        wait_forever;
reg [2:0]  wait_pin;
reg        wait_val;

// Host-interface FIFOs, IRQ lines, and PULL stall state.
reg [7:0]  tx_fifo [0:15];    // host -> core, read by PULL
reg [3:0]  tx_head;           // read index (advances on PULL)
reg [3:0]  tx_tail;           // write index (advances on host push)
reg [4:0]  tx_count;          // 0..16

reg [7:0]  rx_fifo [0:15];    // core -> host, written by PUSH
reg [3:0]  rx_head;
reg [3:0]  rx_tail;
reg [4:0]  rx_count;

reg [15:0] irq_lines;         // set by IRQ opcode, cleared by host
reg        waiting_for_pull;  // PULL stall flag (TX FIFO empty at issue)
reg [2:0]  pull_reg;          // which register PULL should load into

integer k;

wire[15:0] instr    = program_mem[pc[5:0]];
wire[3:0]  op       = instr[15:12];
wire[11:0] operand  = instr[11:0];

always @(posedge clk) begin
    if (!rst_n) begin
        pc               <= 12'd0;
        pin_out          <= 8'd0;
        pin_oe           <= 8'd0;
        stall_remaining  <= 12'd0;
        waiting_for_pin  <= 1'b0;
        wait_forever     <= 1'b0;
        wait_pin         <= 3'd0;
        wait_val         <= 1'b0;
        tx_head          <= 4'd0;
        tx_tail          <= 4'd0;
        tx_count         <= 5'd0;
        rx_head          <= 4'd0;
        rx_tail          <= 4'd0;
        rx_count         <= 5'd0;
        irq_lines        <= 16'd0;
        waiting_for_pull <= 1'b0;
        pull_reg         <= 3'd0;
        for (j = 0; j < 8; j = j + 1) regs[j] <= 8'd0;
        for (k = 0; k < 16; k = k + 1) begin
            tx_fifo[k] <= 8'd0;
            rx_fifo[k] <= 8'd0;
        end
    end else if (waiting_for_pin) begin
        // WAIT stall service — check condition each cycle
        if (pin_in[wait_pin] == wait_val) begin
            waiting_for_pin <= 1'b0;
            wait_forever    <= 1'b0;
            stall_remaining <= 12'd0;
        end else if (!wait_forever) begin
            // bounded wait: on last chance, exit; else keep counting down
            if (stall_remaining == 12'd1) begin
                waiting_for_pin <= 1'b0;
            end
            stall_remaining <= stall_remaining - 12'd1;
        end
    end else if (stall_remaining > 12'd0) begin
        // DELAY stall service — just tick down
        stall_remaining <= stall_remaining - 12'd1;
    end else if (waiting_for_pull) begin
        // PULL stall service — resume the moment TX FIFO has data
        if (tx_count > 5'd0) begin
            regs[pull_reg]   <= tx_fifo[tx_head];
            tx_head          <= tx_head + 4'd1;
            tx_count         <= tx_count - 5'd1;
            waiting_for_pull <= 1'b0;
        end
    end else begin
        case (op)
            OP_NOP: begin
                pc <= pc + 12'd1;
            end
            OP_JMP: begin
                pc <= operand;
            end
            OP_JCND: begin
                // operand: reg[10:8], addr[7:0]
                // Decrement-and-branch: if regs[r] != 0, decrement and jump; else pc++.
                if (regs[operand[10:8]] != 8'd0) begin
                    regs[operand[10:8]] <= regs[operand[10:8]] - 8'd1;
                    pc <= {4'd0, operand[7:0]};
                end else begin
                    pc <= pc + 12'd1;
                end
            end
            OP_LDI: begin
                // operand layout: reg[10:8] (3 bits, top bit reserved), imm[7:0]
                regs[operand[10:8]] <= operand[7:0];
                pc <= pc + 12'd1;
            end
            OP_SET: begin
                // operand layout: pin[11:8], val[7:0] - val[0] only matters
                pin_out[operand[10:8]] <= operand[0];
                pin_oe[operand[10:8]]  <= 1'b1;
                pc <= pc + 12'd1;
            end
            OP_OUT: begin
                //operand layout: pin[11:8], reg[6:4] - 3bits, top bit reserved
                pin_out[operand[10:8]] <= regs[operand[6:4]][0];
                pin_oe[operand[10:8]]  <= 1'b1;
                pc <= pc + 12'd1;
            end
            OP_IN: begin
                // IN releases the pin's OE (mirror of the C++ model) then
                // samples pin_in into bit 0 of regs[r].
                pin_oe[operand[10:8]] <= 1'b0;
                regs[operand[6:4]] <= {regs[operand[6:4]][7:1], pin_in[operand[10:8]]};
                pc <= pc + 12'd1;
            end
            OP_SHIFT: begin
                if(operand[7]) begin
                    regs[operand[10:8]] <= regs[operand[10:8]] >> operand[3:0];
                end else begin
                    regs[operand[10:8]] <= regs[operand[10:8]] << operand[3:0];
                end
                pc <= pc + 12'd1;
            end
            OP_ROT: begin
                if (operand[7]) begin
                    // right rotate: (v >> n) | (v << (8 - n))
                    regs[operand[10:8]] <=
                        (regs[operand[10:8]] >> operand[2:0]) |
                        (regs[operand[10:8]] << (4'd8 - {1'b0, operand[2:0]}));
                end else begin
                    // left rotate:  (v << n) | (v >> (8 - n))
                    regs[operand[10:8]] <=
                        (regs[operand[10:8]] << operand[2:0]) |
                        (regs[operand[10:8]] >> (4'd8 - {1'b0, operand[2:0]}));
                end
                pc <= pc + 12'd1;
            end
            OP_DELAY: begin
                // Consume max(n, 1) cycles. Issue counts as 1; set stall_remaining
                // to n-1 (or 0 if n <= 1) so total elapsed = max(n, 1).
                stall_remaining <= (operand > 12'd1) ? (operand - 12'd1) : 12'd0;
                pc <= pc + 12'd1;
            end
            OP_WAIT: begin
                // operand: pin[11:8], val[7], timeout[6:0]
                pin_oe[operand[10:8]] <= 1'b0;   // release OE like IN
                if (pin_in[operand[10:8]] == operand[7]) begin
                    // condition met at issue — 1 cycle total, no stall
                end else if (operand[6:0] == 7'd0) begin
                    // timeout=0 → wait forever
                    waiting_for_pin <= 1'b1;
                    wait_forever    <= 1'b1;
                    wait_pin        <= operand[10:8];
                    wait_val        <= operand[7];
                end else if (operand[6:0] == 7'd1) begin
                    // timeout=1 → only issue cycle allowed, already exhausted
                end else begin
                    waiting_for_pin <= 1'b1;
                    wait_forever    <= 1'b0;
                    wait_pin        <= operand[10:8];
                    wait_val        <= operand[7];
                    stall_remaining <= {5'd0, operand[6:0]} - 12'd1;
                end
                pc <= pc + 12'd1;
            end
            OP_PUSH: begin
                // operand: reg[10:8] — append regs[r] to RX FIFO.
                // On overflow (rx_count == 16) silently drop; in the model
                // this throws, but real hardware would prefer a flag/IRQ.
                if (rx_count < 5'd16) begin
                    rx_fifo[rx_tail] <= regs[operand[10:8]];
                    rx_tail          <= rx_tail + 4'd1;
                    rx_count         <= rx_count + 5'd1;
                end
                pc <= pc + 12'd1;
            end
            OP_PULL: begin
                // operand: reg[10:8] — pop TX FIFO head into regs[r].
                // If empty, park in waiting_for_pull; stall service completes it.
                if (tx_count > 5'd0) begin
                    regs[operand[10:8]] <= tx_fifo[tx_head];
                    tx_head             <= tx_head + 4'd1;
                    tx_count            <= tx_count - 5'd1;
                end else begin
                    waiting_for_pull <= 1'b1;
                    pull_reg         <= operand[10:8];
                end
                pc <= pc + 12'd1;
            end
            OP_IRQ: begin
                // operand: irq index at [11:8]. Set that bit in irq_lines.
                irq_lines[operand[11:8]] <= 1'b1;
                pc <= pc + 12'd1;
            end
            OP_OUT_OD: begin
                // operand: pin[11:8], reg[6:4]. Open-drain drive:
                //   bit 0 == 0 → drive pin low (OE asserted)
                //   bit 0 == 1 → release pin   (OE deasserted, external pullup wins)
                if (regs[operand[6:4]][0] == 1'b0) begin
                    pin_out[operand[10:8]] <= 1'b0;
                    pin_oe [operand[10:8]] <= 1'b1;
                end else begin
                    pin_oe [operand[10:8]] <= 1'b0;
                end
                pc <= pc + 12'd1;
            end
            default: begin
                // Not yet implemented — freeze PC.
            end
        endcase
    end
end

endmodule
