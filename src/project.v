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
  wire       host_miso;
  wire       irq_pending;
  assign uo_out = {6'b0, irq_pending, host_miso};

  pemu_core core(
    .clk(clk),
    .rst_n(rst_n),
    .pin_in(pin_in),
    .pin_out(pin_out),
    .pin_oe(pin_oe),
    .host_sck(ui_in[0]),
    .host_mosi(ui_in[1]),
    .host_cs_n(ui_in[2]),
    .host_miso(host_miso),
    .irq_pending(irq_pending)
  );

  // List all unused inputs to prevent warnings
  wire _unused = &{ena, ui_in[7:3], 1'b0};

endmodule
