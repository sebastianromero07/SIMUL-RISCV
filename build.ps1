# build.ps1 - Compila el simulador RISC-V (C++17) y ensambla los programas de prueba
New-Item -ItemType Directory -Force -Path build | Out-Null

# 1. Buscar g++ en PATH o en AMDDesignTools
$gpp = Get-Command g++ -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
if (-not $gpp) {
    $amdGpp = "C:\AMDDesignTools\2026.1\tps\mingw\10.0.0\win64.o\nt\bin\g++.exe"
    if (Test-Path $amdGpp) {
        $gpp = $amdGpp
    } else {
        Write-Error "No se encontró g++. Instala MinGW-w64 o agrégalo al PATH."
        exit 1
    }
}

Write-Host "[1/2] Compilando simulador C++ con: $gpp" -ForegroundColor Cyan
& $gpp -std=c++17 -O2 -Wall -Wextra -Iinclude src/main.cpp -lws2_32 -static -o build/awesome-simulator.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Copy-Item build/awesome-simulator.exe build/riscv-simulator.exe -Force
Write-Host "      -> build/awesome-simulator.exe generado correctamente." -ForegroundColor Green

# 2. Ensamblar programas de prueba si está disponible la toolchain RISC-V
$rvAs = "C:\AMDDesignTools\2026.1\gnu\riscv\nt\bin\riscv64-unknown-elf-as.exe"
$rvLd = "C:\AMDDesignTools\2026.1\gnu\riscv\nt\bin\riscv64-unknown-elf-ld.exe"
$rvObjcopy = "C:\AMDDesignTools\2026.1\gnu\riscv\nt\bin\riscv64-unknown-elf-objcopy.exe"

if ((Test-Path $rvAs) -and (Test-Path $rvLd) -and (Test-Path $rvObjcopy)) {
    Write-Host "[2/2] Ensamblando programas RISC-V (.s -> .elf y .bin)..." -ForegroundColor Cyan
    $progs = @("riscvtest", "quicksort", "symmetric_tree", "ecall_demo", "full_rv32i_test", "programa_demo")
    foreach ($p in $progs) {
        & $rvAs -march=rv32i -mabi=ilp32 "tests/$p.s" -o "tests/$p.o"
        & $rvLd -m elf32lriscv -Ttext=0x0 -e 0x0 "tests/$p.o" -o "tests/$p.elf"
        & $rvObjcopy -O binary "tests/$p.elf" "tests/$p.bin"
        Remove-Item "tests/$p.o" -Force -ErrorAction SilentlyContinue
        Write-Host "      -> tests/$p.elf y tests/$p.bin" -ForegroundColor Green
    }
    Copy-Item "tests/programa_demo.bin" "programa.bin" -Force
} else {
    Write-Host "[2/2] Los binarios precompilados (.bin y .elf) ya están listos en tests/." -ForegroundColor Yellow
}
