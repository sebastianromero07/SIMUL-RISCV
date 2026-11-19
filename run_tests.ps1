# run_tests.ps1 - Ejecuta y verifica automáticamente los 6 ejemplos en el simulador
$ErrorActionPreference = "Stop"

$sim = ".\build\awesome-simulator.exe"
if (-not (Test-Path $sim)) {
    .\build.ps1
}

Write-Host "`n==============================================================" -ForegroundColor Cyan
Write-Host " EJEMPLO 1: Sesión interactiva paso a paso (programa.bin)" -ForegroundColor Cyan
Write-Host "==============================================================" -ForegroundColor Cyan
@("pc", "step", "step", "step", "pc", "regs x5 x14", "step", "mem 0x1000 0x1003", "exit") | & $sim programa.bin

Write-Host "`n==============================================================" -ForegroundColor Cyan
Write-Host " EJEMPLO 2: riscvtest.s (Binario crudo .bin -> [0x64] = 25)" -ForegroundColor Cyan
Write-Host "==============================================================" -ForegroundColor Cyan
@("disasm 0x0 17", "run", "mem 0x64 0x67", "memw 0x64 0x64", "exit") | & $sim tests/riscvtest.bin

Write-Host "`n==============================================================" -ForegroundColor Cyan
Write-Host " EJEMPLO 3: Quicksort recursivo in-place (0x1000..0x1018)" -ForegroundColor Cyan
Write-Host "==============================================================" -ForegroundColor Cyan
@("step 18", "memw 0x1000 0x1018", "run", "memw 0x1000 0x1018", "exit") | & $sim tests/quicksort.bin

Write-Host "`n==============================================================" -ForegroundColor Cyan
Write-Host " EJEMPLO 4: Árbol Simétrico cargado desde .elf (a0 = 1)" -ForegroundColor Cyan
Write-Host "==============================================================" -ForegroundColor Cyan
@("run", "regs a0 x10", "memw 0x1000 0x1018", "exit") | & $sim tests/symmetric_tree.elf

Write-Host "`n==============================================================" -ForegroundColor Cyan
Write-Host " EJEMPLO 5: SPIM ecall (Fibonacci I/O + sbrk + exit)" -ForegroundColor Cyan
Write-Host "==============================================================" -ForegroundColor Cyan
@("run", "memw 0x1000 0x101C", "exit") | & $sim tests/ecall_demo.elf

Write-Host "`n==============================================================" -ForegroundColor Cyan
Write-Host " EJEMPLO 6: Suite completa de 37 instrucciones RV32I" -ForegroundColor Cyan
Write-Host "==============================================================" -ForegroundColor Cyan
@("run", "regs a0", "memw 0x1000 0x1000", "exit") | & $sim tests/full_rv32i_test.elf
