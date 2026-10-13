`timescale 1ns/1ps
module tb_scheduler;
    parameter IMAGE = "";
    parameter BUNDLES = 0;
    logic clk = 0;
    logic rst_n = 0;
    logic bad_alignment, bad_text, bad_data;
    always #5 clk = ~clk;
    porygon_system #(
        .INSTRUCTION_INIT_FILE(IMAGE),
        .INSTRUCTION_INIT_BUNDLES(BUNDLES)
    ) dut (
        .clk_i(clk), .rst_ni(rst_n),
        .invalid_address_o(bad_alignment),
        .instruction_access_error_o(bad_text),
        .data_access_error_o(bad_data)
    );
    always @(posedge clk) if (rst_n && (bad_alignment || bad_text || bad_data))
        $fatal(1, "Error de direccionamiento");
    initial begin
        repeat (2) @(posedge clk);
        @(negedge clk); rst_n = 1;
        repeat (200) @(posedge clk);
        #1;
        if (dut.u_processor.u_register_file.registers_q[2] !== 0) $fatal(1, "r2");
        if (dut.u_processor.u_register_file.registers_q[3] !== 21) $fatal(1, "r3");
        if (dut.u_processor.u_register_file.registers_q[4] !== 24) $fatal(1, "r4");
        if (dut.u_processor.u_register_file.registers_q[5] !== 3) $fatal(1, "r5");
        if (dut.u_data_memory.memory[0] !== 21) $fatal(1, "store MUL");
        if (dut.u_data_memory.memory[4] !== 24) $fatal(1, "store final");
        $display("PASS scheduler -> .mem -> RTL: MUL, ALUs, LSU, bucle y ambos caminos de JNZ");
        $finish;
    end
endmodule
