# programa_demo.s
# Programa de prueba básico para verificar ejecución paso a paso,
# lectura de registros (x5, x14) e inspección de memoria en 0x1000..0x1003.

.global _start
.section .text
_start:
    addi x5, x0, 0x123       # 0x00000000: x5 = 0x00000123
    lui  x14, 0x10200        # 0x00000004: x14 = 0x10200000
    addi x14, x14, 0x405     # 0x00000008: x14 = 0x10200405 (PC = 0x0000000C)
    addi x6, x0, 1           # 0x0000000C: 4to step
_done:
    beq  x0, x0, _done       # 0x00000010: bucle final

.org 0x1000
    .byte 0x00, 0x05, 0x7C, 0xE8
