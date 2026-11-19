# Informe del Simulador RISC-V (RV32I)

**Curso:** CS3051 Arquitectura de Computadores 2026-II (HW#5)

## 1. Descripción general

El proyecto implementa un simulador del conjunto de instrucciones RISC-V de 32 bits (RV32I). El núcleo principal está escrito en C++17 para ejecutarse desde la terminal (con soporte de depuración remota vía GDB) y se complementa con una interfaz web en HTML/CSS/JavaScript inspirada en CPUlator.

El simulador mantiene el estado arquitectural de la CPU: contador de programa (`PC`), banco de 32 registros enteros de 32 bits (`x0`–`x31`, con `x0` siempre en `0`) y una memoria plana *byte-addressable* en formato *little-endian*.

## 2. Decisiones de implementación

- **Lenguaje principal (C++17):** se eligió C++17 sin librerías externas para mantener el simulador rápido, portable y fácil de compilar en Windows (`g++` / MinGW) y Linux (`make`).
- **Memoria (`include/memory.hpp`):** espacio de direcciones de 32 bits (`0x00000000` – `0xFFFFFFFF`) en formato *little-endian*. Utiliza un vector contiguo de 16 MB para las direcciones bajas (pila, código y datos cercanos) y páginas de 4 KB bajo demanda (`std::unordered_map`) para el resto del espacio de 4 GB.
- **Interfaz Web (`web/`):** implementada en JavaScript puro (`web/simulator.js`) para poder abrir `web/index.html` directamente en el navegador sin necesidad de levantar un servidor backend.

## 3. Funcionalidades implementadas

### ISA Base RV32I (y extensión RV32M)
- **Cargas y tiendas:** `lb`, `lh`, `lw`, `lbu`, `lhu`, `sb`, `sh`, `sw`.
- **Operaciones con inmediatos:** `addi`, `slli`, `slti`, `sltiu`, `xori`, `srli`, `srai`, `ori`, `andi`, `lui`, `auipc`.
- **Operaciones registro-registro:** `add`, `sub`, `sll`, `slt`, `sltu`, `xor`, `srl`, `sra`, `or`, `and` (además de `mul`, `mulh`, `mulhsu`, `mulhu`, `div`, `divu`, `rem`, `remu`).
- **Control de flujo:** `beq`, `bne`, `blt`, `bge`, `bltu`, `bgeu`, `jal`, `jalr`.

### Carga de programas (.bin y .elf)
- **Binarios crudos (`.bin`):** se cargan directamente en `0x00000000` e inicializan `PC = 0x00000000`.
- **Archivos `.elf` (`include/elf_loader.hpp`):** lee ejecutables ELF32 *little-endian* de RISC-V (`e_machine = 243`), carga los segmentos `PT_LOAD` en sus direcciones virtuales, limpia `.bss`, ajusta `PC = e_entry` y extrae los símbolos de `.symtab` para mostrar etiquetas al desensamblar.

### Ejecución, inspección y depuración
- **Modo terminal (`src/main.cpp`):** permite avanzar paso a paso (`step`, `stepi`), correr hasta finalizar o detenerse en un breakpoint (`run`), consultar el `PC` (`pc`), inspeccionar registros por número o nombre ABI (`regs`), inspeccionar memoria en bytes o palabras (`mem`, `memw`) y desensamblar (`disasm`).
- **Llamadas al sistema (`ecall`):** implementa las syscalls más comunes de SPIM/CPUlator usando `a7` como selector: `1` (`print_int`), `4` (`print_string`), `5` (`read_int`), `8` (`read_string`), `9` (`sbrk`), `10` (`exit`), `11` (`print_char`), `12` (`read_char`), `34` (`print_hex`) y `93` (`exit2`).
- **Servidor GDB (`include/gdb_server.hpp`):** implementa el protocolo GDB Remote Serial Protocol sobre TCP (`--gdb [puerto]`), permitiendo conectar `riscv64-unknown-elf-gdb` para inspeccionar registros, memoria, avanzar con `stepi` y poner breakpoints.

## 4. Pruebas realizadas

Los programas de prueba se encuentran en `tests/` (código ensamblador `.s`, binario crudo `.bin` y ejecutable `.elf`) y pueden ejecutarse con `.\run_tests.ps1`:

1. **`programa_demo.s` (`programa.bin`):** prueba de ejecución paso a paso, lectura de `PC`, `regs x5 x14` y lectura de memoria en `0x1000..0x1003` (`00 05 7C E8`).
2. **`riscvtest.s`:** prueba de aritmética, memoria y saltos (`add`, `sub`, `and`, `or`, `slt`, `addi`, `lw`, `sw`, `beq`, `jal`); al finalizar escribe `25` (`0x19`) en la dirección `100` (`0x64`).
3. **`quicksort.s`:** coloca `{6, 4, 3, 2, 1, 8, 9}` en `0x1000`, aplica *quicksort* recursivo *in-place* y deja `{1, 2, 3, 4, 6, 8, 9}` en esa misma dirección.
4. **`symmetric_tree.s`:** coloca `{6, 4, 4, 1, 2, 2, 1}` en `0x1000`, evalúa `is_symmetric` y termina con `a0 = 1`.
5. **`ecall_demo.s`:** calcula los primeros 8 términos de Fibonacci usando memoria dinámica (`sbrk`) e imprime el resultado mediante syscalls de SPIM.
6. **`full_rv32i_test.s`:** prueba el conjunto completo de instrucciones RV32I (incluyendo extensiones de signo y comparaciones con/sin signo), escribiendo `0xCAFEBABE` en `0x1000` y `a0 = 1`.
