/*
 * Protocol Emulator ASIC - Phase 0 blinky
 *
 * Minimal design whose entire purpose is to prove the RTL -> GDS
 * toolchain (Icarus + cocotb locally, OpenLane in CI). Once the flow
 * is green end-to-end this file gets replaced by the actual PEmu core.
 *
 * ui_in[7:0]  : divider preload value (higher = slower blink)
 * uo_out[0]   : blink output (toggles every 2^(ui_in+1) cycles)
 * uo_out[7:1] : top bits of internal counter (scope-friendly)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

`default_nettype none

module tt_um_devanandanj_pemu (
    input  wire [7:0] ui_in,
    output wire [7:0] uo_out,
    input  wire [7:0] uio_in,
    output wire [7:0] uio_out,
    output wire [7:0] uio_oe,
    input  wire       ena,
    input  wire       clk,
    input  wire       rst_n
);

  reg [23:0] counter;

  always @(posedge clk) begin
    if (!rst_n) counter <= 24'd0;
    else        counter <= counter + 24'd1;
  end

  // ui_in selects which counter bit is exposed on uo_out[0].
  // Range: bit 0 (fastest) .. bit 23 (slowest).
  wire [4:0] tap = ui_in[4:0] > 5'd23 ? 5'd23 : ui_in[4:0];
  assign uo_out[0]   = counter[tap];
  assign uo_out[7:1] = counter[23:17];

  // Bidirectional pins unused for now.
  assign uio_out = 8'd0;
  assign uio_oe  = 8'd0;

  wire _unused = &{ena, uio_in, ui_in[7:5], 1'b0};

endmodule
