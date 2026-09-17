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

  // Program memory temporary rom (32 words for step 7's extended program)
  reg[15:0] program_mem[31:0];
  integer i;
  initial begin
    for (i = 0; i < 32; i = i + 1) program_mem[i] = 16'h0000;
    program_mem[0] = 16'hC001;   // LDI  r0, 0x01
    program_mem[1] = 16'h1001;   // SET  pin 0, val 1     (drive pin 0 high)
    program_mem[2] = 16'h1100;   // SET  pin 1, val 0     (drive pin 1 low)
    program_mem[3] = 16'h2200;   // OUT  pin 2, r0        (drive pin 2 from r0 bit 0)
    program_mem[4] = 16'h4310;   // IN   pin 3, r1        (sample pin 3 into r1 bit 0)
    program_mem[5] = 16'hC281;   // LDI  r2, 0x81
    program_mem[6] = 16'h3281;   // SHIFT r2, right, 1    → r2 = 0x40
    program_mem[7] = 16'hC381;   // LDI  r3, 0x81
    program_mem[8] = 16'hD381;   // ROT  r3, right, 1     → r3 = 0xC0
    program_mem[9]  = 16'h6003;  // DELAY 3
    program_mem[10] = 16'h5385;  // WAIT pin=3, val=1, timeout=5 (matches immediately)
    program_mem[11] = 16'h5305;  // WAIT pin=3, val=0, timeout=5 (times out after 5 cycles)
    program_mem[12] = 16'hC403;  // LDI  r4, 3
    program_mem[13] = 16'h840D;  // JCND r4, 13   (self-loop: dec-and-branch until r4=0)
    program_mem[14] = 16'hC5AA;  // LDI  r5, 0xAA
    program_mem[15] = 16'h9500;  // PUSH r5                (→ rx_fifo)
    program_mem[16] = 16'hB300;  // IRQ  3                 (set irq_lines[3])
    program_mem[17] = 16'hC701;  // LDI  r7, 1
    program_mem[18] = 16'hE570;  // OUT_OD pin 5, r7       (bit=1 → release pin 5)
    program_mem[19] = 16'hC700;  // LDI  r7, 0
    program_mem[20] = 16'hE570;  // OUT_OD pin 5, r7       (bit=0 → drive pin 5 low)
    program_mem[21] = 16'hA600;  // PULL r6                (stalls until host provides data)
    program_mem[22] = 16'h7000;  // JMP 0
end

reg[11:0] pc;

reg[7:0] regs[0:7];
integer j;

// Multi-cycle stall state (mirror of C++ model)
reg [11:0] stall_remaining;
reg        waiting_for_pin;
reg        wait_forever;
reg [3:0]  wait_pin;
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

wire[15:0] instr    = program_mem[pc[4:0]];
wire[3:0]  op       = instr[15:12];
wire[11:0] operand  = instr[11:0];

always @(posedge clk) begin
    if (!rst_n) begin
        pc               <= 12'd0;
        pin_out          <= 24'd0;
        pin_oe           <= 24'd0;
        stall_remaining  <= 12'd0;
        waiting_for_pin  <= 1'b0;
        wait_forever     <= 1'b0;
        wait_pin         <= 4'd0;
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
                pin_out[operand[11:8]] <= operand[0];
                pin_oe[operand[11:8]]  <= 1'b1;
                pc <= pc + 12'd1;
            end
            OP_OUT: begin
                //operand layout: pin[11:8], reg[6:4] - 3bits, top bit reserved
                pin_out[operand[11:8]] <= regs[operand[6:4]][0];
                pin_oe[operand[11:8]]  <= 1'b1;
                pc <= pc + 12'd1;
            end
            OP_IN: begin
                // IN releases the pin's OE (mirror of the C++ model) then
                // samples pin_in into bit 0 of regs[r].
                pin_oe[operand[11:8]] <= 1'b0;
                regs[operand[6:4]] <= {regs[operand[6:4]][7:1], pin_in[operand[11:8]]};
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
                pin_oe[operand[11:8]] <= 1'b0;   // release OE like IN
                if (pin_in[operand[11:8]] == operand[7]) begin
                    // condition met at issue — 1 cycle total, no stall
                end else if (operand[6:0] == 7'd0) begin
                    // timeout=0 → wait forever
                    waiting_for_pin <= 1'b1;
                    wait_forever    <= 1'b1;
                    wait_pin        <= operand[11:8];
                    wait_val        <= operand[7];
                end else if (operand[6:0] == 7'd1) begin
                    // timeout=1 → only issue cycle allowed, already exhausted
                end else begin
                    waiting_for_pin <= 1'b1;
                    wait_forever    <= 1'b0;
                    wait_pin        <= operand[11:8];
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
                    pin_out[operand[11:8]] <= 1'b0;
                    pin_oe [operand[11:8]] <= 1'b1;
                end else begin
                    pin_oe [operand[11:8]] <= 1'b0;
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
