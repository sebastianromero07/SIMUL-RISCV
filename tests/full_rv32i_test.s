# full_rv32i_test.s
# Prueba de las instrucciones del ISA base RV32I:
#   Loads:    lb, lh, lw, lbu, lhu
#   Stores:   sb, sh, sw
#   ALU-Imm:  addi, slli, slti, sltiu, xori, srli, srai, ori, andi
#   Upper:    lui, auipc
#   ALU-Reg:  add, sub, sll, slt, sltu, xor, srl, sra, or, and
#   Branches: beq, bne, blt, bge, bltu, bgeu
#   Jumps:    jal, jalr
# Si todas las pruebas pasan, escribe 0xCAFEBABE en 0x1000 y deja a0 = 1.

.global _start
.section .text
_start:
    li   sp, 0x10000
    li   s0, 0x1000          # Base de memoria de prueba

    # 1. LUI & ADDI & SW & LW
    lui  t0, 0x12345         # t0 = 0x12345000
    addi t0, t0, 0x678       # t0 = 0x12345678
    sw   t0, 0(s0)           # mem[0x1000] = 0x12345678 (bytes: 78 56 34 12)
    lw   t1, 0(s0)
    bne  t0, t1, fail

    # 2. Little-endian byte & halfword loads (LB, LBU, LH, LHU)
    lbu  t2, 0(s0)           # 0x78
    li   t3, 0x78
    bne  t2, t3, fail

    # Probar extensión de signo con byte negativo (0x80 = -128)
    addi t2, zero, -128      # 0xFFFFFF80
    sb   t2, 4(s0)           # mem[0x1004] = 0x80
    lb   t3, 4(s0)           # debe extender signo a 0xFFFFFF80 (-128)
    bne  t2, t3, fail
    lbu  t4, 4(s0)           # debe extender con ceros a 0x00000080 (128)
    li   t5, 128
    bne  t4, t5, fail

    # Probar SH, LH, LHU con media palabra negativa (0xFF05 = -251)
    li   t2, -251
    sh   t2, 8(s0)
    lh   t3, 8(s0)
    bne  t2, t3, fail
    lhu  t4, 8(s0)
    li   t5, 0x0000FF05
    bne  t4, t5, fail

    # 3. SLLI, SRLI, SRAI, SLL, SRL, SRA
    li   t0, -16             # 0xFFFFFFF0
    srai t1, t0, 2           # -4 (0xFFFFFFFC)
    li   t2, -4
    bne  t1, t2, fail
    srli t1, t0, 28          # 0x0000000F (15)
    li   t2, 15
    bne  t1, t2, fail
    slli t1, t2, 4           # 15 << 4 = 240
    li   t3, 4
    sll  t4, t2, t3          # 15 << 4 = 240
    bne  t1, t4, fail
    srl  t5, t4, t3          # 240 >> 4 = 15
    bne  t5, t2, fail
    sra  t6, t0, t3          # -16 >> 4 = -1
    li   t2, -1
    bne  t6, t2, fail

    # 4. SLTI, SLTIU, SLT, SLTU
    li   t0, -5
    li   t1, 5
    slti  t2, t0, 0          # (-5 < 0) signed -> 1
    beq   t2, zero, fail
    sltiu t2, t0, 0          # (0xFFFFFFFB < 0) unsigned -> 0
    bne   t2, zero, fail
    slt   t2, t0, t1         # (-5 < 5) signed -> 1
    beq   t2, zero, fail
    sltu  t2, t0, t1         # (0xFFFFFFFB < 5) unsigned -> 0
    bne   t2, zero, fail

    # 5. XORI, ORI, ANDI, XOR, OR, AND, ADD, SUB, AUIPC
    li   t0, 0x55
    xori t1, t0, 0xFF        # 0xAA
    ori  t2, t0, 0xAA        # 0xFF
    andi t3, t2, 0x0F        # 0x0F
    xor  t4, t1, t0          # 0xFF
    bne  t2, t4, fail
    or   t5, t0, t1          # 0xFF
    bne  t5, t2, fail
    and  t6, t1, t0          # 0x00
    bne  t6, zero, fail
    add  t4, t0, t1          # 0x55 + 0xAA = 0xFF
    sub  t4, t4, t1          # 0xFF - 0xAA = 0x55
    bne  t4, t0, fail
    auipc t5, 0

    # 6. Branches: BLT, BGE, BLTU, BGEU
    li   t0, -10
    li   t1, 10
    blt  t0, t1, branch_ok1
    j    fail
branch_ok1:
    bge  t1, t0, branch_ok2
    j    fail
branch_ok2:
    bltu t1, t0, branch_ok3  # 10 < 0xFFFFFFF6 unsigned
    j    fail
branch_ok3:
    bgeu t0, t1, branch_ok4  # 0xFFFFFFF6 >= 10 unsigned
    j    fail
branch_ok4:

    # 7. JAL & JALR
    jal  ra, helper_fn
    li   t0, 42
    bne  a0, t0, fail

    # ÉXITO: escribir 0xCAFEBABE en 0x1000 y poner a0 = 1
    li   t0, 0xCAFEBABE
    sw   t0, 0(s0)
    li   a0, 1
done:
    j    done

helper_fn:
    li   a0, 42
    jalr zero, ra, 0

fail:
    li   a0, 0
    sw   zero, 0(s0)
fail_loop:
    j    fail_loop
