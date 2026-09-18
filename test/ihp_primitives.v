// SPDX-License-Identifier: Apache-2.0
//
// IHP internal primitives — behavioral UDP definitions used by the
// sg13cmos5l stdcell library. The CMOS5L PDK's stdcell.v references
// these but the flow doesn't ship a matching *_udp.v for iverilog to
// pick up, so gate-level sim fails at elaboration with:
//   "Unknown module type: ihp_dff_r / ihp_mux2 / ihp_mux4"
//
// Definitions are the sg13g2 UDPs from IHP-GmbH/IHP-Open-PDK
// (ihp-sg13g2/libs.ref/sg13g2_stdcell/verilog/sg13g2_udp.v). The
// primitives are common across IHP's Sigma-13 family, so the sg13g2
// tables apply to sg13cmos5l too. Only included in the GATES=yes
// branch of the test Makefile — never in RTL sim.

primitive ihp_dff_r (q, v, clk, d, r, xcr);
    output q;
    reg    q;
    input  v, clk, d, r, xcr;
    table
        //  v      clk   d   r    xcr : q  : q'
            *      ?     ?   ?    ?   : ?  : x;
            ?      ?     ?   1    ?   : ?  : 0;
            ?      b     ?  (1?)  ?   : 0  : -;
            ?      x     0  (1?)  ?   : 0  : -;
            ?      ?     ?  (10)  ?   : ?  : -;
            ?      ?     ?  (x0)  ?   : ?  : -;
            ?      ?     ?  (0x)  ?   : 0  : -;
            ?     (x1)   0   ?    0   : ?  : 0;
            ?     (x1)   1   0    0   : ?  : 1;
            ?     (x1)   0   ?    1   : 0  : 0;
            ?     (x1)   1   0    1   : 1  : 1;
            ?     (x1)   ?   ?    x   : ?  : -;
            ?     (bx)   0   ?    ?   : 0  : -;
            ?     (bx)   1   0    ?   : 1  : -;
            ?     (x0)   0   ?    ?   : ?  : -;
            ?     (x0)   1   0    ?   : ?  : -;
            ?     (x0)   ?   0    x   : ?  : -;
            ?     (01)   0   ?    ?   : ?  : 0;
            ?     (01)   1   0    ?   : ?  : 1;
            ?     (10)   ?   ?    ?   : ?  : -;
            ?      b     *   ?    ?   : ?  : -;
            ?      ?     ?   ?    *   : ?  : -;
    endtable
endprimitive

primitive ihp_mux2 (z, a, b, s);
    output z;
    input  a, b, s;
    table
        //  a  b  s : z
            1  ?  0 : 1;
            0  ?  0 : 0;
            ?  1  1 : 1;
            ?  0  1 : 0;
            0  0  x : 0;
            1  1  x : 1;
    endtable
endprimitive

primitive ihp_mux4 (z, a, b, c, d, s0, s1);
    output z;
    input  d, c, b, a, s1, s0;
    table
        //  a  b  c  d  s0 s1 : z
            0  ?  ?  ?  0  0  : 0;
            1  ?  ?  ?  0  0  : 1;
            ?  0  ?  ?  1  0  : 0;
            ?  1  ?  ?  1  0  : 1;
            ?  ?  0  ?  0  1  : 0;
            ?  ?  1  ?  0  1  : 1;
            ?  ?  ?  0  1  1  : 0;
            ?  ?  ?  1  1  1  : 1;
            0  0  ?  ?  x  0  : 0;
            1  1  ?  ?  x  0  : 1;
            ?  ?  0  0  x  1  : 0;
            ?  ?  1  1  x  1  : 1;
            0  ?  0  ?  0  x  : 0;
            1  ?  1  ?  0  x  : 1;
            ?  0  ?  0  1  x  : 0;
            ?  1  ?  1  1  x  : 1;
            1  1  1  1  x  x  : 1;
            0  0  0  0  x  x  : 0;
    endtable
endprimitive
