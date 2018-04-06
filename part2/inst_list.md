# Instruction List

Here are the instructions which are supported in the pipeline cpu. All are from `machine.def`

You can find the detail of each instruction:

| Instruction | OP Code | ALU Source A | ALU Source B | ALU Operation | Destenation | Branch |
|-------------|---------|--------------|--------------|---------------|-------------|--------|
| `nop` | 0x00 | 0 | ALU_NOP | null | null |
| `j` | 0x01 | in1 | imm | ALU_ADD | null | jump |
| `bne` | 0x06 | in1 | in2 | ALU_SUB | null | on not zero |
| `lw` | 0x28 | in2 | imm | ALU_ADD | out1 | null |
| `sw` | 0x34 | in2 | imm | ALU_ADD | null | null |
| `add` | 0x40 | in1 | in2 | ALU_ADD | out1 | null |
| `addu` | 0x42 | in1 | in2 | ALU_ADD | out1 | null |
| `addiu` | 0x43 | in1 | imm | ALU_ADD | out1 | null |
| `andi` | 0x4f | in1 | imm | ALU_AND | out1 | null |
| `sll` | 0x55 | in1 | shamt | ALU_SHTL | out1 | null |
| `slti` | 0x5c | in1 | imm | ALU_SLT | out1 | null |
| `syscall` | 0xa0 | 0 | 0 | ALU_NOP | null | null |
| `lui` | 0xa2 | 0 | imm | ALU_ADD | out1 | null |
