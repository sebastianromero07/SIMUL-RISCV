#pragma once

#include <cstdint>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>

class Disassembler {
public:
    static const char* reg_abi_name(uint32_t reg) {
        static const char* names[32] = {
            "zero", "ra", "sp",  "gp",  "tp", "t0", "t1", "t2",
            "s0",   "s1", "a0",  "a1",  "a2", "a3", "a4", "a5",
            "a6",   "a7", "s2",  "s3",  "s4", "s5", "s6", "s7",
            "s8",   "s9", "s10", "s11", "t3", "t4", "t5", "t6"
        };
        return (reg < 32) ? names[reg] : "??";
    }

    // Parse register identifier like "x5", "x14", "a0", "sp", "zero", "fp" -> 0..31, or -1 if invalid
    static int parse_register(const std::string& token) {
        if (token.empty()) return -1;
        if (token[0] == 'x' || token[0] == 'X') {
            try {
                size_t idx = 0;
                int val = std::stoi(token.substr(1), &idx, 10);
                if (idx == token.size() - 1 && val >= 0 && val < 32) {
                    return val;
                }
            } catch (...) {
                return -1;
            }
        }
        if (token == "fp") return 8;
        for (uint32_t i = 0; i < 32; ++i) {
            if (token == reg_abi_name(i)) return static_cast<int>(i);
        }
        return -1;
    }

    static int32_t sign_extend(uint32_t val, int bits) {
        uint32_t shift = 32 - bits;
        return static_cast<int32_t>(val << shift) >> shift;
    }

    static std::string format_hex32(uint32_t val) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", val);
        return std::string(buf);
    }

    static std::string disassemble(
        uint32_t instr,
        uint32_t pc = 0,
        const std::map<uint32_t, std::string>* symbols = nullptr,
        bool use_abi_names = true
    ) {
        auto rname = [&](uint32_t r) -> std::string {
            if (use_abi_names) {
                return std::string(reg_abi_name(r));
            }
            return "x" + std::to_string(r);
        };

        auto resolve_sym = [&](uint32_t target_addr) -> std::string {
            char hexbuf[24];
            std::snprintf(hexbuf, sizeof(hexbuf), "0x%08X", target_addr);
            if (symbols) {
                auto it = symbols->find(target_addr);
                if (it != symbols->end()) {
                    return std::string(hexbuf) + " <" + it->second + ">";
                }
            }
            return std::string(hexbuf);
        };

        uint32_t opcode = instr & 0x7F;
        uint32_t rd     = (instr >> 7) & 0x1F;
        uint32_t funct3 = (instr >> 12) & 0x07;
        uint32_t rs1    = (instr >> 15) & 0x1F;
        uint32_t rs2    = (instr >> 20) & 0x1F;
        uint32_t funct7 = (instr >> 25) & 0x7F;

        int32_t imm_i = sign_extend(instr >> 20, 12);
        int32_t imm_s = sign_extend(((instr >> 25) << 5) | ((instr >> 7) & 0x1F), 12);
        int32_t imm_b = sign_extend(
            (((instr >> 31) & 0x1) << 12) |
            (((instr >> 7)  & 0x1) << 11) |
            (((instr >> 25) & 0x3F) << 5) |
            (((instr >> 8)  & 0x0F) << 1),
            13
        );
        uint32_t imm_u = (instr >> 12) & 0xFFFFF;
        int32_t imm_j = sign_extend(
            (((instr >> 31) & 0x1) << 20) |
            (((instr >> 12) & 0xFF) << 12) |
            (((instr >> 20) & 0x1) << 11) |
            (((instr >> 21) & 0x3FF) << 1),
            21
        );

        std::ostringstream oss;

        switch (opcode) {
            case 0x37: { // LUI
                char buf[16];
                std::snprintf(buf, sizeof(buf), "0x%X", imm_u);
                oss << "lui " << rname(rd) << ", " << buf;
                return oss.str();
            }
            case 0x17: { // AUIPC
                char buf[16];
                std::snprintf(buf, sizeof(buf), "0x%X", imm_u);
                oss << "auipc " << rname(rd) << ", " << buf;
                return oss.str();
            }
            case 0x6F: { // JAL
                uint32_t target = pc + static_cast<uint32_t>(imm_j);
                oss << "jal " << rname(rd) << ", " << imm_j << "  # " << resolve_sym(target);
                return oss.str();
            }
            case 0x67: { // JALR
                if (funct3 == 0x0) {
                    oss << "jalr " << rname(rd) << ", " << rname(rs1) << ", " << imm_i;
                    if (rd == 0 && rs1 == 1 && imm_i == 0) {
                        oss << "  # ret";
                    }
                    return oss.str();
                }
                break;
            }
            case 0x63: { // Branches
                const char* mnem = nullptr;
                switch (funct3) {
                    case 0x0: mnem = "beq";  break;
                    case 0x1: mnem = "bne";  break;
                    case 0x4: mnem = "blt";  break;
                    case 0x5: mnem = "bge";  break;
                    case 0x6: mnem = "bltu"; break;
                    case 0x7: mnem = "bgeu"; break;
                }
                if (mnem) {
                    uint32_t target = pc + static_cast<uint32_t>(imm_b);
                    oss << mnem << " " << rname(rs1) << ", " << rname(rs2) << ", " << imm_b
                        << "  # " << resolve_sym(target);
                    return oss.str();
                }
                break;
            }
            case 0x03: { // Loads
                const char* mnem = nullptr;
                switch (funct3) {
                    case 0x0: mnem = "lb";  break;
                    case 0x1: mnem = "lh";  break;
                    case 0x2: mnem = "lw";  break;
                    case 0x4: mnem = "lbu"; break;
                    case 0x5: mnem = "lhu"; break;
                }
                if (mnem) {
                    oss << mnem << " " << rname(rd) << ", " << imm_i << "(" << rname(rs1) << ")";
                    return oss.str();
                }
                break;
            }
            case 0x23: { // Stores
                const char* mnem = nullptr;
                switch (funct3) {
                    case 0x0: mnem = "sb"; break;
                    case 0x1: mnem = "sh"; break;
                    case 0x2: mnem = "sw"; break;
                }
                if (mnem) {
                    oss << mnem << " " << rname(rs2) << ", " << imm_s << "(" << rname(rs1) << ")";
                    return oss.str();
                }
                break;
            }
            case 0x13: { // OP-IMM
                uint32_t shamt = rs2;
                switch (funct3) {
                    case 0x0:
                        if (instr == 0x00000013) {
                            return "addi zero, zero, 0  # nop";
                        }
                        oss << "addi " << rname(rd) << ", " << rname(rs1) << ", " << imm_i;
                        return oss.str();
                    case 0x1:
                        if (funct7 == 0x00) {
                            oss << "slli " << rname(rd) << ", " << rname(rs1) << ", " << shamt;
                            return oss.str();
                        }
                        break;
                    case 0x2:
                        oss << "slti " << rname(rd) << ", " << rname(rs1) << ", " << imm_i;
                        return oss.str();
                    case 0x3:
                        oss << "sltiu " << rname(rd) << ", " << rname(rs1) << ", " << imm_i;
                        return oss.str();
                    case 0x4:
                        oss << "xori " << rname(rd) << ", " << rname(rs1) << ", " << imm_i;
                        return oss.str();
                    case 0x5:
                        if (funct7 == 0x00) {
                            oss << "srli " << rname(rd) << ", " << rname(rs1) << ", " << shamt;
                            return oss.str();
                        } else if (funct7 == 0x20) {
                            oss << "srai " << rname(rd) << ", " << rname(rs1) << ", " << shamt;
                            return oss.str();
                        }
                        break;
                    case 0x6:
                        oss << "ori " << rname(rd) << ", " << rname(rs1) << ", " << imm_i;
                        return oss.str();
                    case 0x7:
                        oss << "andi " << rname(rd) << ", " << rname(rs1) << ", " << imm_i;
                        return oss.str();
                }
                break;
            }
            case 0x33: { // OP (Register-Register RV32I + RV32M)
                if (funct7 == 0x00) {
                    const char* mnem = nullptr;
                    switch (funct3) {
                        case 0x0: mnem = "add";  break;
                        case 0x1: mnem = "sll";  break;
                        case 0x2: mnem = "slt";  break;
                        case 0x3: mnem = "sltu"; break;
                        case 0x4: mnem = "xor";  break;
                        case 0x5: mnem = "srl";  break;
                        case 0x6: mnem = "or";   break;
                        case 0x7: mnem = "and";  break;
                    }
                    if (mnem) {
                        oss << mnem << " " << rname(rd) << ", " << rname(rs1) << ", " << rname(rs2);
                        return oss.str();
                    }
                } else if (funct7 == 0x20) {
                    if (funct3 == 0x0) {
                        oss << "sub " << rname(rd) << ", " << rname(rs1) << ", " << rname(rs2);
                        return oss.str();
                    } else if (funct3 == 0x5) {
                        oss << "sra " << rname(rd) << ", " << rname(rs1) << ", " << rname(rs2);
                        return oss.str();
                    }
                } else if (funct7 == 0x01) { // RV32M extension
                    const char* mnem = nullptr;
                    switch (funct3) {
                        case 0x0: mnem = "mul";    break;
                        case 0x1: mnem = "mulh";   break;
                        case 0x2: mnem = "mulhsu"; break;
                        case 0x3: mnem = "mulhu";  break;
                        case 0x4: mnem = "div";    break;
                        case 0x5: mnem = "divu";   break;
                        case 0x6: mnem = "rem";    break;
                        case 0x7: mnem = "remu";   break;
                    }
                    if (mnem) {
                        oss << mnem << " " << rname(rd) << ", " << rname(rs1) << ", " << rname(rs2);
                        return oss.str();
                    }
                }
                break;
            }
            case 0x73: { // SYSTEM
                if (instr == 0x00000073) return "ecall";
                if (instr == 0x00100073) return "ebreak";
                break;
            }
            case 0x0F: { // FENCE
                return "fence";
            }
        }

        char unk[32];
        std::snprintf(unk, sizeof(unk), ".word 0x%08X  # desconocida", instr);
        return std::string(unk);
    }
};
