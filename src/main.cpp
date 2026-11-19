#include "cpu.hpp"
#include "disassembler.hpp"
#include "elf_loader.hpp"
#include "gdb_server.hpp"
#include "memory.hpp"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

static bool parse_u32(const std::string& s, uint32_t& out) {
    try {
        size_t idx = 0;
        unsigned long val = std::stoul(s, &idx, 0);
        if (idx != s.size()) return false;
        out = static_cast<uint32_t>(val);
        return true;
    } catch (...) {
        return false;
    }
}

static std::vector<std::string> tokenize(std::string line) {
    // Strip UTF-8 BOM if present (common when piping from Windows PowerShell)
    if (line.size() >= 3 &&
        static_cast<uint8_t>(line[0]) == 0xEF &&
        static_cast<uint8_t>(line[1]) == 0xBB &&
        static_cast<uint8_t>(line[2]) == 0xBF) {
        line.erase(0, 3);
    }
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) {
        tokens.push_back(tok);
    }
    return tokens;
}

static void print_all_registers(const Simulator& sim) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "pc          = 0x%08X (%u)", sim.get_pc(), sim.get_pc());
    std::cout << buf << "\n";
    for (uint32_t i = 0; i < 32; i += 2) {
        uint32_t v0 = sim.get_reg(i);
        uint32_t v1 = sim.get_reg(i + 1);
        std::string r0 = "x" + std::to_string(i) + " (" + Disassembler::reg_abi_name(i) + ")";
        std::string r1 = "x" + std::to_string(i + 1) + " (" + Disassembler::reg_abi_name(i + 1) + ")";
        std::snprintf(
            buf, sizeof(buf),
            "%-11s = 0x%08X (%-11d)   %-11s = 0x%08X (%d)",
            r0.c_str(), v0, static_cast<int32_t>(v0),
            r1.c_str(), v1, static_cast<int32_t>(v1)
        );
        std::cout << buf << "\n";
    }
}

static void print_disassembly(const Simulator& sim, uint32_t start_addr, uint32_t count) {
    const auto& syms = sim.load_info().symbols;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t addr = start_addr + i * 4;
        auto it = syms.find(addr);
        if (it != syms.end()) {
            char sym_hdr[128];
            std::snprintf(sym_hdr, sizeof(sym_hdr), "\n%08X <%s>:", addr, it->second.c_str());
            std::cout << sym_hdr << "\n";
        }
        uint32_t instr = sim.memory().read32(addr);
        std::string asm_str = Disassembler::disassemble(instr, addr, &syms, true);
        const char* prefix = (addr == sim.get_pc()) ? "-> " : "   ";
        const char* bp_mark = sim.has_breakpoint(addr) ? "[*] " : "    ";
        char line[256];
        std::snprintf(line, sizeof(line), "%s%s0x%08X:  %08X    %s", prefix, bp_mark, addr, instr, asm_str.c_str());
        std::cout << line << "\n";
    }
}

static void print_help() {
    std::cout
        << "Comandos disponibles:\n"
        << "  pc                       Muestra el valor actual del contador de programa (PC)\n"
        << "  step [N]                 Ejecuta 1 (o N) instrucción(es) paso por paso\n"
        << "  stepi [N] / si [N]       Ejecuta 1 (o N) instrucción(es) mostrando el desensamblado\n"
        << "  run [max] / c            Ejecuta hasta terminar, breakpoint o bucle infinito final\n"
        << "  regs [r1 r2 ...]         Muestra todos los registros o los especificados (ej: regs x5 x14 a0)\n"
        << "  mem <ini> [fin]          Inspecciona bytes en memoria [ini..fin] (ej: mem 0x1000 0x1003)\n"
        << "  memw <ini> [fin]         Inspecciona palabras de 32 bits (little-endian) en [ini..fin]\n"
        << "  disasm [addr] [N] / u    Desensambla N instrucciones desde addr (por defecto PC)\n"
        << "  break <addr> / b <addr>  Agrega un breakpoint en la dirección indicada\n"
        << "  del <addr>               Elimina un breakpoint en la dirección indicada\n"
        << "  breaks                   Lista los breakpoints activos\n"
        << "  gdb [puerto]             Inicia servidor GDB Remote Serial Protocol (defecto: 1234)\n"
        << "  reset                    Reinicia el simulador y recarga el programa actual\n"
        << "  load <archivo>           Carga un nuevo archivo (.bin o .elf) en memoria\n"
        << "  help                     Muestra esta ayuda\n"
        << "  exit / quit              Sale del simulador\n";
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    if (argc < 2) {
        std::cerr << "Uso: " << argv[0] << " <programa.bin|programa.elf> [--run] [--gdb [puerto]]\n";
        return 1;
    }

    std::string filename = argv[1];
    bool batch_run = false;
    bool start_gdb = false;
    uint16_t gdb_port = 1234;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--run") {
            batch_run = true;
        } else if (arg == "--gdb") {
            start_gdb = true;
            if (i + 1 < argc) {
                uint32_t p = 0;
                if (parse_u32(argv[i + 1], p) && p > 0 && p <= 65535) {
                    gdb_port = static_cast<uint16_t>(p);
                    ++i;
                }
            }
        }
    }

    Simulator sim{};
    if (!sim.load_from_file(filename)) {
        std::cerr << sim.load_info().error_msg << "\n";
        return 1;
    }

    std::cout << "\"" << filename << "\" cargado a memoria.\n";

    if (start_gdb) {
        GdbServer::serve(sim, gdb_port);
        return 0;
    }

    if (batch_run) {
        const uint64_t max_steps = 10000000;
        uint64_t steps = 0;
        StepResult last_res;
        while (!sim.is_halted() && steps < max_steps) {
            last_res = sim.step();
            steps++;
            if (last_res.status != StepStatus::OK) break;
        }
        if (!last_res.message.empty()) {
            std::cout << last_res.message << "\n";
        }
        return sim.exit_code();
    }

    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) {
            break;
        }

        auto tokens = tokenize(line);
        if (tokens.empty()) continue;

        const std::string& cmd = tokens[0];

        if (cmd == "exit" || cmd == "quit" || cmd == "q") {
            std::cout << "See you next time...\n";
            std::cout << "Program exited with code " << sim.exit_code() << ".\n";
            break;
        } else if (cmd == "pc") {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "pc = 0x%08X", sim.get_pc());
            std::cout << buf << "\n";
        } else if (cmd == "step" || cmd == "s") {
            uint32_t count = 1;
            if (tokens.size() >= 2) {
                if (!parse_u32(tokens[1], count) || count == 0) {
                    std::cout << "Cantidad de pasos inválida.\n";
                    continue;
                }
            }
            for (uint32_t i = 0; i < count; ++i) {
                std::cout << "Ejecutando instrucción.\n";
                StepResult r = sim.step();
                if (r.status == StepStatus::ILLEGAL_INSTRUCTION) {
                    std::cout << r.message << "\n";
                    break;
                }
                if (r.status == StepStatus::HALTED) {
                    if (!r.message.empty()) std::cout << r.message << "\n";
                    break;
                }
                if (count > 1 && (r.status == StepStatus::BREAKPOINT || r.status == StepStatus::EBREAK)) {
                    std::cout << r.message << "\n";
                    break;
                }
            }
        } else if (cmd == "stepi" || cmd == "si") {
            uint32_t count = 1;
            if (tokens.size() >= 2) {
                if (!parse_u32(tokens[1], count) || count == 0) {
                    std::cout << "Cantidad de pasos inválida.\n";
                    continue;
                }
            }
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t cur_pc = sim.get_pc();
                uint32_t instr = sim.memory().read32(cur_pc);
                std::string dis = Disassembler::disassemble(instr, cur_pc, &sim.load_info().symbols, true);
                char buf[256];
                std::snprintf(buf, sizeof(buf), "[0x%08X] %08X  %s", cur_pc, instr, dis.c_str());
                std::cout << buf << "\n";
                StepResult r = sim.step();
                if (!r.message.empty()) {
                    std::cout << r.message << "\n";
                }
                if (r.status == StepStatus::ILLEGAL_INSTRUCTION || r.status == StepStatus::HALTED) {
                    break;
                }
                if (count > 1 && (r.status == StepStatus::BREAKPOINT || r.status == StepStatus::EBREAK)) {
                    break;
                }
            }
        } else if (cmd == "run" || cmd == "continue" || cmd == "c") {
            uint32_t max_steps = 10000000;
            if (tokens.size() >= 2) {
                parse_u32(tokens[1], max_steps);
            }
            uint32_t executed = 0;
            StepResult r;
            while (!sim.is_halted() && executed < max_steps) {
                r = sim.step();
                executed++;
                if (r.status != StepStatus::OK) break;
            }
            char summary[128];
            std::snprintf(summary, sizeof(summary), "Ejecutadas %u instrucciones. PC actual = 0x%08X", executed, sim.get_pc());
            std::cout << summary << "\n";
            if (!r.message.empty()) {
                std::cout << r.message << "\n";
            } else if (r.status == StepStatus::SELF_LOOP) {
                std::cout << "Bucle infinito de terminación detectado en PC = "
                          << Disassembler::format_hex32(sim.get_pc()) << ".\n";
            }
        } else if (cmd == "regs" || cmd == "reg") {
            if (tokens.size() == 1) {
                print_all_registers(sim);
            } else {
                for (size_t i = 1; i < tokens.size(); ++i) {
                    int r = Disassembler::parse_register(tokens[i]);
                    if (r < 0) {
                        std::cout << "Registro desconocido: " << tokens[i] << "\n";
                    } else {
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "%s = 0x%08X", tokens[i].c_str(), sim.get_reg(static_cast<uint32_t>(r)));
                        std::cout << buf << "\n";
                    }
                }
            }
        } else if (cmd == "mem") {
            if (tokens.size() < 2) {
                std::cout << "Uso: mem <direccion_inicio> [direccion_fin]\n";
                continue;
            }
            uint32_t start_addr = 0, end_addr = 0;
            if (!parse_u32(tokens[1], start_addr)) {
                std::cout << "Dirección inicial inválida: " << tokens[1] << "\n";
                continue;
            }
            std::string start_label = tokens[1];
            std::string end_label;
            if (tokens.size() >= 3) {
                if (!parse_u32(tokens[2], end_addr)) {
                    std::cout << "Dirección final inválida: " << tokens[2] << "\n";
                    continue;
                }
                end_label = tokens[2];
            } else {
                end_addr = start_addr + 3;
                char tmp[16];
                std::snprintf(tmp, sizeof(tmp), "0x%X", end_addr);
                end_label = tmp;
            }
            if (end_addr < start_addr) {
                std::cout << "El rango de memoria es inválido (fin < inicio).\n";
                continue;
            }
            std::cout << "Memoria (" << start_label << "-" << end_label << "):";
            for (uint64_t a = start_addr; a <= end_addr; ++a) {
                char hex[8];
                std::snprintf(hex, sizeof(hex), " %02X", sim.memory().read8(static_cast<uint32_t>(a)));
                std::cout << hex;
            }
            std::cout << "\n";
        } else if (cmd == "memw") {
            if (tokens.size() < 2) {
                std::cout << "Uso: memw <direccion_inicio> [direccion_fin]\n";
                continue;
            }
            uint32_t start_addr = 0, end_addr = 0;
            if (!parse_u32(tokens[1], start_addr)) {
                std::cout << "Dirección inicial inválida.\n";
                continue;
            }
            if (tokens.size() >= 3) {
                if (!parse_u32(tokens[2], end_addr)) {
                    std::cout << "Dirección final inválida.\n";
                    continue;
                }
            } else {
                end_addr = start_addr + 28;
            }
            for (uint64_t a = start_addr; a <= end_addr; a += 4) {
                uint32_t addr32 = static_cast<uint32_t>(a);
                uint32_t w = sim.memory().read32(addr32);
                char line_buf[96];
                std::snprintf(
                    line_buf, sizeof(line_buf),
                    "[0x%08X] = 0x%08X  (%d)",
                    addr32, w, static_cast<int32_t>(w)
                );
                std::cout << line_buf << "\n";
            }
        } else if (cmd == "disasm" || cmd == "u") {
            uint32_t addr = sim.get_pc();
            uint32_t count = 12;
            if (tokens.size() >= 2) parse_u32(tokens[1], addr);
            if (tokens.size() >= 3) parse_u32(tokens[2], count);
            print_disassembly(sim, addr, count);
        } else if (cmd == "break" || cmd == "b") {
            if (tokens.size() < 2) {
                std::cout << "Uso: break <direccion>\n";
                continue;
            }
            uint32_t addr = 0;
            if (!parse_u32(tokens[1], addr)) {
                std::cout << "Dirección inválida.\n";
                continue;
            }
            sim.add_breakpoint(addr);
            std::cout << "Breakpoint agregado en " << Disassembler::format_hex32(addr) << ".\n";
        } else if (cmd == "del") {
            if (tokens.size() < 2) {
                std::cout << "Uso: del <direccion>\n";
                continue;
            }
            uint32_t addr = 0;
            if (!parse_u32(tokens[1], addr)) {
                std::cout << "Dirección inválida.\n";
                continue;
            }
            sim.remove_breakpoint(addr);
            std::cout << "Breakpoint eliminado en " << Disassembler::format_hex32(addr) << ".\n";
        } else if (cmd == "breaks") {
            if (sim.breakpoints().empty()) {
                std::cout << "No hay breakpoints activos.\n";
            } else {
                std::cout << "Breakpoints activos:\n";
                for (uint32_t bp : sim.breakpoints()) {
                    std::cout << "  - " << Disassembler::format_hex32(bp) << "\n";
                }
            }
        } else if (cmd == "gdb") {
            uint16_t port = 1234;
            if (tokens.size() >= 2) {
                uint32_t p = 0;
                if (parse_u32(tokens[1], p) && p > 0 && p <= 65535) {
                    port = static_cast<uint16_t>(p);
                }
            }
            GdbServer::serve(sim, port);
        } else if (cmd == "reset") {
            if (sim.load_from_file(sim.loaded_filename())) {
                std::cout << "Simulador reiniciado. \"" << sim.loaded_filename() << "\" recargado a memoria.\n";
            }
        } else if (cmd == "load") {
            if (tokens.size() < 2) {
                std::cout << "Uso: load <archivo>\n";
                continue;
            }
            if (sim.load_from_file(tokens[1])) {
                std::cout << "\"" << tokens[1] << "\" cargado a memoria.\n";
            } else {
                std::cout << sim.load_info().error_msg << "\n";
            }
        } else if (cmd == "help" || cmd == "?") {
            print_help();
        } else {
            std::cout << "Comando desconocido: \"" << cmd << "\". Escribe 'help' para ver los comandos disponibles.\n";
        }
    }

    return sim.exit_code();
}
