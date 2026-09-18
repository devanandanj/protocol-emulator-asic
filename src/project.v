/*
 * Copyright (c) 2024 Devanandan J
 * SPDX-License-Identifier: Apache-2.0
 */

`default_nettype none
`timescale 1ns / 1ps

module tt_um_devanandanj_pemu (
    input  wire [7:0] ui_in,    // Dedicated inputs
    output wire [7:0] uo_out,   // Dedicated outputs
    input  wire [7:0] uio_in,   // IOs: Input path
    output wire [7:0] uio_out,  // IOs: Output path
    output wire [7:0] uio_oe,   // IOs: Enable path (active high: 0=input, 1=output)
    input  wire       ena,      // always 1 when the design is powered, so you can ignore it
    input  wire       clk,      // clock
    input  wire       rst_n     // reset_n - low to reset
);

  wire [7:0] pin_in;
  wire [7:0] pin_out;
  wire [7:0] pin_oe;

  assign pin_in = uio_in;
  assign uio_out  = pin_out;
  assign uio_oe   = pin_oe;
  assign uo_out   = 8'b0;

  // Program loader control lives on the dedicated inputs. uio_in doubles as
  // the load-data byte while rst_n=0 (no pin traffic happens in reset), then
  // reverts to pin_in[7:0] during run.
  pemu_core core(
    .clk(clk),
    .rst_n(rst_n),
    .pin_in(pin_in),
    .pin_out(pin_out),
    .pin_oe(pin_oe),
    .prog_we(ui_in[0]),
    .prog_hi(ui_in[1]),
    .prog_rst(ui_in[2]),
    .prog_data(uio_in)
  );

  // List all unused inputs to prevent warnings
  wire _unused = &{ena, ui_in[7:3], 1'b0};

endmodule
