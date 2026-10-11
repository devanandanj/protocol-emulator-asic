/*
 * Copyright (c) 2026 Devanandan J
 * SPDX-License-Identifier: Apache-2.0
 *
 * Blackbox declaration of IHP's 256x16 single-port SRAM macro, used as
 * PEmu's program memory. LibreLane's Verilator lint step reads neither
 * LEF nor liberty, so it needs this port list to elaborate the design.
 * It is wired in as the macro's `vh` view in src/config.json. The layout,
 * LEF and liberty files come from the PDK
 * (libs.ref/sg13cmos5l_sram, a symlink to sg13g2_sram).
 *
 * Simulation uses the real behavioral model in test/models/, not this.
 */

`default_nettype none
`timescale 1ns / 1ps

/* verilator lint_off UNDRIVEN */
(* blackbox *)
module RM_IHPSG13_1P_256x16_c2_bm_bist (
    input  wire        A_CLK,
    input  wire        A_MEN,
    input  wire        A_WEN,
    input  wire        A_REN,
    input  wire [7:0]  A_ADDR,
    input  wire [15:0] A_DIN,
    input  wire        A_DLY,
    output wire [15:0] A_DOUT,
    input  wire [15:0] A_BM,
    input  wire        A_BIST_CLK,
    input  wire        A_BIST_EN,
    input  wire        A_BIST_MEN,
    input  wire        A_BIST_WEN,
    input  wire        A_BIST_REN,
    input  wire [7:0]  A_BIST_ADDR,
    input  wire [15:0] A_BIST_DIN,
    input  wire [15:0] A_BIST_BM
);
endmodule
/* verilator lint_on UNDRIVEN */

`default_nettype wire
