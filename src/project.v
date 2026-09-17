/*
 * Copyright (c) 2024 Devanandan J
 * SPDX-License-Identifier: Apache-2.0
 */

`default_nettype none

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

  wire [23:0] pin_in;
  wire [23:0] pin_out;
  wire [23:0] pin_oe;

  assign pin_in[7:0]   = uio_in;
  assign pin_in[23:8]  = 16'b0;

  assign uio_out  = pin_out[7:0];
  assign uio_oe   = pin_oe[7:0];
  assign uo_out   = 8'b0;

  pemu_core core(
    .clk(clk),
    .rst_n(rst_n),
    .pin_in(pin_in),
    .pin_out(pin_out),
    .pin_oe(pin_oe)
  );

  // List all unused inputs to prevent warnings
  wire _unused = &{ena,ui_in, pin_out[23:8], pin_oe[23:8], 1'b0};

endmodule
