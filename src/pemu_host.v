/*
 * Copyright (c) 2026 Devanandan J
 * SPDX-License-Identifier: Apache-2.0
 */

`default_nettype none
`timescale 1ns / 1ps

// Host port: an SPI mode 0 slave, MSB first, SCK at most clk/8. It is the
// chip's only path to program memory, the FIFOs, the IRQ lines and run
// control. A frame is CS_N low, one command byte, then data bytes. Every byte
// the host does not read as data returns the status byte:
//   {run, irq_pending, tx_full, rx_count[4:0]}
// See docs/architecture.md for the command table.
module pemu_host (
    input   wire          clk,
    input   wire          rst_n,
    input   wire          sck,
    input   wire          mosi,
    input   wire          cs_n,
    output  wire          miso,
    // Core halted while run=0; cleared by rst_n.
    output  reg           run,
    // Program memory write (one-cycle pulse, only while halted).
    output  wire          prog_we,
    output  reg   [7:0]   prog_addr,
    output  wire  [15:0]  prog_data,
    // TX FIFO write (one-cycle pulse).
    output  wire          tx_wr,
    output  wire  [7:0]   tx_data,
    input   wire          tx_full,
    // RX FIFO head and pop (one-cycle pulse).
    input   wire  [7:0]   rx_data,
    input   wire  [4:0]   rx_count,
    output  wire          rx_rd,
    // IRQ lines and the bits to clear (one-cycle pulse).
    input   wire  [15:0]  irq_lines,
    output  wire  [15:0]  irq_clr
);

  localparam [2:0] CMD_NOP  = 3'd0;
  localparam [2:0] CMD_CTRL = 3'd1;   // data: bit 0 = run
  localparam [2:0] CMD_PROG = 3'd2;   // data: addr, then {lo, hi} words
  localparam [2:0] CMD_TX   = 3'd3;   // data: bytes into the TX FIFO
  localparam [2:0] CMD_RX   = 3'd4;   // returns bytes popped from the RX FIFO
  localparam [2:0] CMD_IRQ  = 3'd5;   // returns irq[7:0], irq[15:8]; clears them

  // Pins are asynchronous to clk. All three share one synchronizer depth so
  // MOSI and CS_N stay aligned with the SCK edges detected below.
  reg [2:0] sck_s;
  reg [1:0] mosi_s;
  reg [1:0] cs_s;

  wire sck_rise = sck_s[1] && !sck_s[2];
  wire sck_fall = !sck_s[1] && sck_s[2];

  reg [2:0] bit_cnt;
  reg [6:0] in_sr;
  reg [7:0] out_sr;
  reg [7:0] shown;        // byte being returned, for clear-what-was-read
  reg       shown_valid;  // shown came from the RX FIFO
  reg       in_cmd;       // next completed byte is the command
  reg [2:0] cmd;
  reg [1:0] idx;          // PROG: 0 addr, 1 lo, 2 hi. IRQ: 0 lo, 1 hi.
  reg [7:0] prog_lo;

  wire [7:0] rx_byte   = {in_sr, mosi_s[1]};
  wire       byte_done = !cs_s[1] && sck_rise && (bit_cnt == 3'd7);
  wire       data_done = byte_done && !in_cmd;

  wire [7:0] status = {run, |irq_lines, tx_full, rx_count};

  assign miso      = out_sr[7];
  assign prog_data = {rx_byte, prog_lo};
  assign prog_we   = data_done && (cmd == CMD_PROG) && (idx == 2'd2) && !run;
  assign tx_data   = rx_byte;
  assign tx_wr     = data_done && (cmd == CMD_TX);
  // Pop when the byte finishes, not when it loads: the load after a frame's
  // last byte is never clocked out, and popping there would lose a byte.
  assign rx_rd     = data_done && (cmd == CMD_RX) && shown_valid;
  assign irq_clr   = !(data_done && (cmd == CMD_IRQ)) ? 16'd0 :
                     (idx == 2'd0) ? {8'd0, shown} : {shown, 8'd0};

  // Next byte to return, chosen at the SCK falling edge after a byte ends.
  reg [7:0] next_out;
  always @(*) begin
      case (in_cmd ? CMD_NOP : cmd)
          CMD_RX:  next_out = (rx_count != 5'd0) ? rx_data : 8'd0;
          CMD_IRQ: next_out = (idx == 2'd0) ? irq_lines[7:0] : irq_lines[15:8];
          default: next_out = status;
      endcase
  end

  always @(posedge clk) begin
      sck_s  <= {sck_s[1:0], sck};
      mosi_s <= {mosi_s[0], mosi};
      cs_s   <= {cs_s[0], cs_n};

      if (!rst_n) begin
          sck_s       <= 3'd0;
          cs_s        <= 2'b11;
          run         <= 1'b0;
          bit_cnt     <= 3'd0;
          in_cmd      <= 1'b1;
          cmd         <= CMD_NOP;
          idx         <= 2'd0;
          shown       <= 8'd0;
          shown_valid <= 1'b0;
          out_sr      <= 8'd0;
      end else if (cs_s[1]) begin
          // Deselected: abandon any partial byte and stage status for MISO.
          bit_cnt     <= 3'd0;
          in_cmd      <= 1'b1;
          shown_valid <= 1'b0;
          out_sr      <= status;
      end else begin
          if (sck_rise) begin
              in_sr   <= rx_byte[6:0];
              bit_cnt <= bit_cnt + 3'd1;
          end

          if (byte_done) begin
              if (in_cmd) begin
                  in_cmd <= 1'b0;
                  idx    <= 2'd0;
                  // Unknown commands act as NOP.
                  cmd    <= (rx_byte[7:3] == 5'd0) ? rx_byte[2:0] : CMD_NOP;
              end else begin
                  case (cmd)
                      CMD_CTRL: run <= rx_byte[0];
                      CMD_PROG: begin
                          case (idx)
                              2'd0: begin
                                  prog_addr <= rx_byte;
                                  idx       <= 2'd1;
                              end
                              2'd1: begin
                                  prog_lo <= rx_byte;
                                  idx     <= 2'd2;
                              end
                              default: begin
                                  prog_addr <= prog_addr + 8'd1;
                                  idx       <= 2'd1;
                              end
                          endcase
                      end
                      CMD_IRQ: idx <= {1'b0, !idx[0]};
                      default: begin
                      end
                  endcase
              end
          end

          if (sck_fall) begin
              if (bit_cnt == 3'd0) begin
                  out_sr      <= next_out;
                  shown       <= next_out;
                  shown_valid <= (cmd == CMD_RX) && !in_cmd && (rx_count != 5'd0);
              end else begin
                  out_sr <= {out_sr[6:0], 1'b0};
              end
          end
      end
  end

endmodule
