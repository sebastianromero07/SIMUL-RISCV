#pragma once

#include "disassembler.hpp"
#include "elf_loader.hpp"
#include "memory.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <string>
#include <unordered_set>

enum class StepStatus {
    OK,
    HALTED,
    BREAKPOINT,
    SELF_LOOP,
    EBREAK,
    ILLEGAL_INSTRUCTION
};

struct StepResult {
    StepStatus status = StepStatus::OK;
    uint32_t executed_pc = 0;
    uint32_t instr = 0;
    std::string message;
};

class Simulator {
private:
    bool halted_ = false;
    int exit_code_ = 0;
    uint32_t pc_ = 0;
    std::array<uint32_t, 32> regs_{};
    Memory memory_;
    uint64_t instructions_executed_ = 0;
    uint32_t heap_break_ = 0x20000000; // Default heap start for SPIM sbrk (syscall 9)

    LoadResult last_load_info_;
    std::string loaded_filename_;
    std::unordered_set<uint32_t> breakpoints_;

    void set_reg(uint32_t rd, uint32_t val) {
        if (rd != 0 && rd < 32) {
            regs_[rd] = val;
        }
        regs_[0] = 0; // x0 is hardwired to 0
    }

public:
    Simulator() {
        reset();
    }

    void reset() {
        halted_ = false;
        exit_code_ = 0;
        pc_ = 0;
        regs_.fill(0);
        memory_.reset();
        instructions_executed_ = 0;
        heap_break_ = 0x20000000;
    }

    bool load_from_file(const std::string& filename) {
        reset();
        last_load_info_ = ProgramLoader::load(filename, memory_);
        if (last_load_info_.success) {
            loaded_filename_ = filename;
            pc_ = last_load_info_.entry_pc;
            return true;
        }
        return false;
    }

    const LoadResult& load_info() const { return last_load_info_; }
    const std::string& loaded_filename() const { return loaded_filename_; }

    bool is_halted() const { return halted_; }
    void set_halted(bool h) { halted_ = h; }
    int exit_code() const { return exit_code_; }

    uint32_t get_pc() const { return pc_; }
    void set_pc(uint32_t new_pc) { pc_ = new_pc; }

    uint32_t get_reg(uint32_t idx) const {
        return (idx < 32) ? regs_[idx] : 0;
    }

    void set_reg_direct(uint32_t idx, uint32_t val) {
        set_reg(idx, val);
    }

    Memory& memory() { return memory_; }
    const Memory& memory() const { return memory_; }

    uint64_t instructions_executed() const { return instructions_executed_; }

    // Breakpoint management
    void add_breakpoint(uint32_t addr) { breakpoints_.insert(addr); }
    void remove_breakpoint(uint32_t addr) { breakpoints_.erase(addr); }
    bool has_breakpoint(uint32_t addr) const { return breakpoints_.count(addr) > 0; }
    const std::unordered_set<uint32_t>& breakpoints() const { return breakpoints_; }

    // Execute a single instruction pointed to by PC
    StepResult step() {
        StepResult res;
        res.executed_pc = pc_;

        if (halted_) {
            res.status = StepStatus::HALTED;
            res.message = "El programa ya ha terminado.";
            return res;
        }

        uint32_t instr = memory_.read32(pc_);
        res.instr = instr;

        uint32_t opcode = instr & 0x7F;
        uint32_t rd     = (instr >> 7) & 0x1F;
        uint32_t funct3 = (instr >> 12) & 0x07;
        uint32_t rs1    = (instr >> 15) & 0x1F;
        uint32_t rs2    = (instr >> 20) & 0x1F;
        uint32_t funct7 = (instr >> 25) & 0x7F;

        int32_t imm_i = Disassembler::sign_extend(instr >> 20, 12);
        int32_t imm_s = Disassembler::sign_extend(((instr >> 25) << 5) | ((instr >> 7) & 0x1F), 12);
        int32_t imm_b = Disassembler::sign_extend(
            (((instr >> 31) & 0x1) << 12) |
            (((instr >> 7)  & 0x1) << 11) |
            (((instr >> 25) & 0x3F) << 5) |
            (((instr >> 8)  & 0x0F) << 1),
            13
        );
        int32_t imm_j = Disassembler::sign_extend(
            (((instr >> 31) & 0x1) << 20) |
            (((instr >> 12) & 0xFF) << 12) |
            (((instr >> 20) & 0x1) << 11) |
            (((instr >> 21) & 0x3FF) << 1),
            21
        );

        uint32_t next_pc = pc_ + 4;
        bool is_control_flow = false;

        switch (opcode) {
            case 0x37: { // lui rd, imm
                set_reg(rd, instr & 0xFFFFF000u);
                break;
            }
            case 0x17: { // auipc rd, imm
                set_reg(rd, pc_ + (instr & 0xFFFFF000u));
                break;
            }
            case 0x6F: { // jal rd, offset
                uint32_t target = pc_ + static_cast<uint32_t>(imm_j);
                set_reg(rd, pc_ + 4);
                next_pc = target;
                is_control_flow = true;
                break;
            }
            case 0x67: { // jalr rd, rs1, imm
                if (funct3 != 0x0) {
                    return illegal_instruction(res);
                }
                uint32_t target = (regs_[rs1] + static_cast<uint32_t>(imm_i)) & ~1u;
                set_reg(rd, pc_ + 4);
                next_pc = target;
                is_control_flow = true;
                break;
            }
            case 0x63: { // Branch instructions
                bool take = false;
                switch (funct3) {
                    case 0x0: // beq
                        take = (regs_[rs1] == regs_[rs2]);
                        break;
                    case 0x1: // bne
                        take = (regs_[rs1] != regs_[rs2]);
                        break;
                    case 0x4: // blt
                        take = (static_cast<int32_t>(regs_[rs1]) < static_cast<int32_t>(regs_[rs2]));
                        break;
                    case 0x5: // bge
                        take = (static_cast<int32_t>(regs_[rs1]) >= static_cast<int32_t>(regs_[rs2]));
                        break;
                    case 0x6: // bltu
                        take = (regs_[rs1] < regs_[rs2]);
                        break;
                    case 0x7: // bgeu
                        take = (regs_[rs1] >= regs_[rs2]);
                        break;
                    default:
                        return illegal_instruction(res);
                }
                if (take) {
                    next_pc = pc_ + static_cast<uint32_t>(imm_b);
                    is_control_flow = true;
                }
                break;
            }
            case 0x03: { // Load instructions
                uint32_t addr = regs_[rs1] + static_cast<uint32_t>(imm_i);
                switch (funct3) {
                    case 0x0: { // lb
                        int8_t val = static_cast<int8_t>(memory_.read8(addr));
                        set_reg(rd, static_cast<uint32_t>(static_cast<int32_t>(val)));
                        break;
                    }
                    case 0x1: { // lh
                        int16_t val = static_cast<int16_t>(memory_.read16(addr));
                        set_reg(rd, static_cast<uint32_t>(static_cast<int32_t>(val)));
                        break;
                    }
                    case 0x2: { // lw
                        set_reg(rd, memory_.read32(addr));
                        break;
                    }
                    case 0x4: { // lbu
                        set_reg(rd, static_cast<uint32_t>(memory_.read8(addr)));
                        break;
                    }
                    case 0x5: { // lhu
                        set_reg(rd, static_cast<uint32_t>(memory_.read16(addr)));
                        break;
                    }
                    default:
                        return illegal_instruction(res);
                }
                break;
            }
            case 0x23: { // Store instructions
                uint32_t addr = regs_[rs1] + static_cast<uint32_t>(imm_s);
                switch (funct3) {
                    case 0x0: // sb
                        memory_.write8(addr, static_cast<uint8_t>(regs_[rs2] & 0xFFu));
                        break;
                    case 0x1: // sh
                        memory_.write16(addr, static_cast<uint16_t>(regs_[rs2] & 0xFFFFu));
                        break;
                    case 0x2: // sw
                        memory_.write32(addr, regs_[rs2]);
                        break;
                    default:
                        return illegal_instruction(res);
                }
                break;
            }
            case 0x13: { // Register-Immediate ALU instructions
                uint32_t shamt = rs2;
                switch (funct3) {
                    case 0x0: // addi
                        set_reg(rd, regs_[rs1] + static_cast<uint32_t>(imm_i));
                        break;
                    case 0x1: // slli
                        if (funct7 != 0x00) return illegal_instruction(res);
                        set_reg(rd, regs_[rs1] << shamt);
                        break;
                    case 0x2: // slti
                        set_reg(rd, (static_cast<int32_t>(regs_[rs1]) < imm_i) ? 1u : 0u);
                        break;
                    case 0x3: // sltiu
                        set_reg(rd, (regs_[rs1] < static_cast<uint32_t>(imm_i)) ? 1u : 0u);
                        break;
                    case 0x4: // xori
                        set_reg(rd, regs_[rs1] ^ static_cast<uint32_t>(imm_i));
                        break;
                    case 0x5: // srli / srai
                        if (funct7 == 0x00) { // srli
                            set_reg(rd, regs_[rs1] >> shamt);
                        } else if (funct7 == 0x20) { // srai
                            set_reg(rd, static_cast<uint32_t>(static_cast<int32_t>(regs_[rs1]) >> shamt));
                        } else {
                            return illegal_instruction(res);
                        }
                        break;
                    case 0x6: // ori
                        set_reg(rd, regs_[rs1] | static_cast<uint32_t>(imm_i));
                        break;
                    case 0x7: // andi
                        set_reg(rd, regs_[rs1] & static_cast<uint32_t>(imm_i));
                        break;
                    default:
                        return illegal_instruction(res);
                }
                break;
            }
            case 0x33: { // Register-Register ALU instructions (RV32I + RV32M)
                if (funct7 == 0x00) {
                    switch (funct3) {
                        case 0x0: // add
                            set_reg(rd, regs_[rs1] + regs_[rs2]);
                            break;
                        case 0x1: // sll
                            set_reg(rd, regs_[rs1] << (regs_[rs2] & 0x1Fu));
                            break;
                        case 0x2: // slt
                            set_reg(rd, (static_cast<int32_t>(regs_[rs1]) < static_cast<int32_t>(regs_[rs2])) ? 1u : 0u);
                            break;
                        case 0x3: // sltu
                            set_reg(rd, (regs_[rs1] < regs_[rs2]) ? 1u : 0u);
                            break;
                        case 0x4: // xor
                            set_reg(rd, regs_[rs1] ^ regs_[rs2]);
                            break;
                        case 0x5: // srl
                            set_reg(rd, regs_[rs1] >> (regs_[rs2] & 0x1Fu));
                            break;
                        case 0x6: // or
                            set_reg(rd, regs_[rs1] | regs_[rs2]);
                            break;
                        case 0x7: // and
                            set_reg(rd, regs_[rs1] & regs_[rs2]);
                            break;
                    }
                } else if (funct7 == 0x20) {
                    switch (funct3) {
                        case 0x0: // sub
                            set_reg(rd, regs_[rs1] - regs_[rs2]);
                            break;
                        case 0x5: // sra
                            set_reg(rd, static_cast<uint32_t>(static_cast<int32_t>(regs_[rs1]) >> (regs_[rs2] & 0x1Fu)));
                            break;
                        default:
                            return illegal_instruction(res);
                    }
                } else if (funct7 == 0x01) { // RV32M (Multiply/Divide extension)
                    int64_t  s1 = static_cast<int32_t>(regs_[rs1]);
                    int64_t  s2 = static_cast<int32_t>(regs_[rs2]);
                    uint64_t u1 = regs_[rs1];
                    uint64_t u2 = regs_[rs2];
                    switch (funct3) {
                        case 0x0: // mul
                            set_reg(rd, static_cast<uint32_t>(s1 * s2));
                            break;
                        case 0x1: // mulh
                            set_reg(rd, static_cast<uint32_t>(static_cast<uint64_t>(s1 * s2) >> 32));
                            break;
                        case 0x2: // mulhsu
                            set_reg(rd, static_cast<uint32_t>(static_cast<uint64_t>(s1 * static_cast<int64_t>(u2)) >> 32));
                            break;
                        case 0x3: // mulhu
                            set_reg(rd, static_cast<uint32_t>((u1 * u2) >> 32));
                            break;
                        case 0x4: // div
                            if (s2 == 0) set_reg(rd, 0xFFFFFFFFu);
                            else if (s1 == INT32_MIN && s2 == -1) set_reg(rd, static_cast<uint32_t>(INT32_MIN));
                            else set_reg(rd, static_cast<uint32_t>(s1 / s2));
                            break;
                        case 0x5: // divu
                            if (u2 == 0) set_reg(rd, 0xFFFFFFFFu);
                            else set_reg(rd, static_cast<uint32_t>(u1 / u2));
                            break;
                        case 0x6: // rem
                            if (s2 == 0) set_reg(rd, static_cast<uint32_t>(s1));
                            else if (s1 == INT32_MIN && s2 == -1) set_reg(rd, 0u);
                            else set_reg(rd, static_cast<uint32_t>(s1 % s2));
                            break;
                        case 0x7: // remu
                            if (u2 == 0) set_reg(rd, static_cast<uint32_t>(u1));
                            else set_reg(rd, static_cast<uint32_t>(u1 % u2));
                            break;
                    }
                } else {
                    return illegal_instruction(res);
                }
                break;
            }
            case 0x73: { // SYSTEM: ecall / ebreak
                if (instr == 0x00000073) { // ecall
                    pc_ = next_pc;
                    instructions_executed_++;
                    handle_ecall(res);
                    return res;
                } else if (instr == 0x00100073) { // ebreak
                    pc_ = next_pc;
                    instructions_executed_++;
                    res.status = StepStatus::EBREAK;
                    res.message = "EBREAK alcanzado en PC = " + Disassembler::format_hex32(res.executed_pc);
                    return res;
                } else {
                    return illegal_instruction(res);
                }
            }
            case 0x0F: { // fence (no-op in single-core sequential simulator)
                break;
            }
            default:
                return illegal_instruction(res);
        }

        instructions_executed_++;

        // Detect infinite self-loop (e.g., "j _end", "j .", "beq x2, x2, done")
        bool self_loop = is_control_flow && (next_pc == pc_);
        pc_ = next_pc;

        if (self_loop) {
            res.status = StepStatus::SELF_LOOP;
            return res;
        }

        if (breakpoints_.count(pc_) > 0) {
            res.status = StepStatus::BREAKPOINT;
            res.message = "Breakpoint alcanzado en PC = " + Disassembler::format_hex32(pc_);
        }

        return res;
    }

private:
    StepResult illegal_instruction(StepResult& res) {
        char buf[128];
        std::snprintf(
            buf, sizeof(buf),
            "Error de ejecución: Instrucción inválida 0x%08X en PC = 0x%08X",
            res.instr, res.executed_pc
        );
        res.status = StepStatus::ILLEGAL_INSTRUCTION;
        res.message = buf;
        halted_ = true;
        return res;
    }

    // CPUlator / SPIM-compatible environment calls (https://cpulator.01xz.net/doc/#syscall)
    // Register convention in RISC-V SPIM / CPUlator:
    //   a7 (x17): Syscall code
    //   a0 (x10), a1 (x11): Arguments / Return value
    void handle_ecall(StepResult& res) {
        uint32_t syscall_id = regs_[17]; // a7
        uint32_t a0 = regs_[10];
        uint32_t a1 = regs_[11];

        switch (syscall_id) {
            case 1: { // print_int
                std::cout << static_cast<int32_t>(a0) << std::flush;
                break;
            }
            case 4: { // print_string (null-terminated string at address a0)
                uint32_t addr = a0;
                while (true) {
                    uint8_t ch = memory_.read8(addr++);
                    if (ch == 0) break;
                    std::cout << static_cast<char>(ch);
                }
                std::cout << std::flush;
                break;
            }
            case 5: { // read_int
                int32_t val = 0;
                std::cin >> val;
                set_reg(10, static_cast<uint32_t>(val));
                break;
            }
            case 8: { // read_string (a0 = buffer address, a1 = max length)
                if (a1 > 0) {
                    std::string line;
                    if (std::cin.peek() == '\n') std::cin.get();
                    std::getline(std::cin, line);
                    uint32_t max_chars = a1 - 1;
                    uint32_t n = static_cast<uint32_t>(line.size());
                    if (n > max_chars) n = max_chars;
                    for (uint32_t i = 0; i < n; ++i) {
                        memory_.write8(a0 + i, static_cast<uint8_t>(line[i]));
                    }
                    memory_.write8(a0 + n, 0);
                }
                break;
            }
            case 9: { // sbrk (allocate a0 bytes on heap, return pointer in a0)
                uint32_t old_break = heap_break_;
                heap_break_ = (heap_break_ + a0 + 3u) & ~3u;
                set_reg(10, old_break);
                break;
            }
            case 10: { // exit
                halted_ = true;
                exit_code_ = 0;
                res.status = StepStatus::HALTED;
                res.message = "Programa finalizado mediante ecall exit (10).";
                break;
            }
            case 11: { // print_char
                std::cout << static_cast<char>(a0 & 0xFFu) << std::flush;
                break;
            }
            case 12: { // read_char
                char ch = 0;
                std::cin.get(ch);
                set_reg(10, static_cast<uint32_t>(static_cast<uint8_t>(ch)));
                break;
            }
            case 34: { // print_hex
                char hexbuf[16];
                std::snprintf(hexbuf, sizeof(hexbuf), "0x%08X", a0);
                std::cout << hexbuf << std::flush;
                break;
            }
            case 36: { // print_uint
                std::cout << a0 << std::flush;
                break;
            }
            case 93: { // exit2 (exit with code in a0)
                halted_ = true;
                exit_code_ = static_cast<int>(a0);
                res.status = StepStatus::HALTED;
                res.message = "Programa finalizado mediante ecall exit2 (93) con código " + std::to_string(exit_code_) + ".";
                break;
            }
            default: {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "Advertencia: ecall desconocida (a7 = %u) en PC = 0x%08X", syscall_id, res.executed_pc);
                res.message = buf;
                break;
            }
        }
    }
};
