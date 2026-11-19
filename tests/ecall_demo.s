# ecall_demo.s
# Demostración de soporte para ecall (SPIM / CPUlator syscalls en RV32):
#   a7 = 4  (print_string)
#   a7 = 1  (print_int)
#   a7 = 11 (print_char)
#   a7 = 9  (sbrk - memoria dinámica)
#   a7 = 10 (exit)
# Calcula los primeros 8 números de Fibonacci, los guarda en 0x1000 y los imprime.

.global _start
.section .text
_start:
    li   sp, 0x10000

    # Imprimir encabezado con syscall 4 (print_string)
    la   a0, msg_header
    li   a7, 4
    ecall

    # Reservar 32 bytes en el heap con syscall 9 (sbrk)
    li   a0, 32
    li   a7, 9
    ecall
    mv   s2, a0              # s2 = puntero devuelto por sbrk

    li   s0, 0x1000          # s0 = arreglo en 0x1000
    li   t0, 0               # fib(0) = 0
    li   t1, 1               # fib(1) = 1
    li   t2, 0               # i = 0
    li   t3, 8               # N = 8 elementos

fib_loop:
    bge  t2, t3, fib_done

    # Guardar término actual en 0x1000 + 4*i y en heap s2 + 4*i
    slli t4, t2, 2
    add  t5, s0, t4
    sw   t0, 0(t5)
    add  t6, s2, t4
    sw   t0, 0(t6)

    # Imprimir entero actual (syscall 1: print_int)
    mv   a0, t0
    li   a7, 1
    ecall

    # Imprimir espacio ' ' (syscall 11: print_char)
    li   a0, 32
    li   a7, 11
    ecall

    # Siguiente Fibonacci: next = t0 + t1
    add  t4, t0, t1
    mv   t0, t1
    mv   t1, t4

    addi t2, t2, 1
    j    fib_loop

fib_done:
    # Imprimir salto de línea '\n' (syscall 11: print_char)
    li   a0, 10
    li   a7, 11
    ecall

    # Dejar en a0 el último Fibonacci calculado (13) y terminar con syscall 10 (exit)
    li   a0, 13
    li   a7, 10
    ecall

msg_header:
    .asciz "Fibonacci (8 terminos): "
