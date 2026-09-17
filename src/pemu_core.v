`default_nettype none

module pemu_core (
    input   wire          clk,
    input   wire          rst_n,
    input   wire  [23:0]  pin_in,
    output  reg   [23:0]  pin_out,
    output  reg   [23:0]  pin_oe
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

  // Program memory temporary rom
  reg[15:0] program_mem[15:0];
  integer i;
  initial begin
    for(i = 0; i < 16; i = i + 1) program_mem[i] = 16'h0000;
    program_mem[15:0] = 16'h7000;
end

reg[11:0] pc;

wire[15:0] instr    = program_mem[pc[3:0]];
wire[3:0]  op       = instr[15:12];
wire[11:0] operand  = instr[11:0];

always @(posedge clk) begin
    if (!rst_n) begin
        pc      <= 12'd0;
        pin_out <= 24'd0;
          pin_oe  <= 24'd0;
    end else begin
        case (op)
            OP_NOP: begin
                pc <= pc + 12'd1;
            end
            OP_JMP: begin
                pc <= operand;
            end
            default: begin
                // Not yet implemented — freeze PC deliberately so the
                // missing case is visible in a waveform.
            end
        endcase
    end
end

wire _unused = &{pin_in, 1'b0};
endmodule
