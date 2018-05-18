# Instruction List

Here are the instructions which are supported in the pipeline cpu. All are from `machine.def`

You can find the detail of each instruction:

| Instruction | OP Code | ALU Source A | ALU Source B | ALU Operation | Destenation | Branch |
|-------------|---------|--------------|--------------|---------------|-------------|--------|
| `nop` | 0x00 | `DNA` | `DNA` | ALU_NOP | `DNA` | × |
| `j` | 0x01 | `DNA` | `DNA` | ALU_NOP | `DNA` | jump |
| `bne` | 0x06 | in1 | in2 | ALU_SUB | `DNA` | on not zero |
| `lw` | 0x28 | in2 | imm | ALU_ADD | out1 | × |
| `sw` | 0x34 | in2 | imm | ALU_ADD | `DNA` | × |
| `add` | 0x40 | in1 | in2 | ALU_ADD | out1 | × |
| `addu` | 0x42 | in1 | in2 | ALU_ADD | out1 | × |
| `addiu` | 0x43 | in1 | imm | ALU_ADD | out1 | × |
| `andi` | 0x4f | in1 | imm | ALU_AND | out1 | × |
| `sll` | 0x55 | in1 | shamt | ALU_SHTL | out1 | × |
| `slti` | 0x5c | in1 | imm | ALU_SLT | out1 | × |
| `syscall` | 0xa0 | `DNA` | `DNA` | ALU_NOP | `DNA` | × |
| `lui` | 0xa2 | `DNA` | imm | ALU_ADD | out1 | × |
