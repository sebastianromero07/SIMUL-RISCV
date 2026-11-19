#pragma once

#include "memory.hpp"
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

struct LoadResult {
    bool success = false;
    bool is_elf = false;
    uint32_t entry_pc = 0x00000000;
    uint32_t code_start = 0x00000000;
    uint32_t code_end = 0x00000000;
    size_t bytes_loaded = 0;
    std::string error_msg;
    std::map<uint32_t, std::string> symbols; // addr -> symbol name
};

class ProgramLoader {
private:
    static uint16_t read_le16(const uint8_t* p) {
        return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
    }

    static uint32_t read_le32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) |
               (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    }

public:
    static LoadResult load(const std::string& filename, Memory& mem) {
        LoadResult res;
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            res.error_msg = "No se pudo abrir el archivo: " + filename;
            return res;
        }

        std::streamsize file_size = file.tellg();
        if (file_size <= 0) {
            res.error_msg = "El archivo está vacío: " + filename;
            return res;
        }

        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buf(static_cast<size_t>(file_size));
        if (!file.read(reinterpret_cast<char*>(buf.data()), file_size)) {
            res.error_msg = "Error al leer el archivo: " + filename;
            return res;
        }

        // Check for ELF magic header: 0x7F 'E' 'L' 'F'
        if (buf.size() >= 52 &&
            buf[0] == 0x7F && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') {
            return load_elf32(buf, mem);
        }

        // Raw binary (.bin): load at address 0x00000000 as required in section 1.1
        mem.write_block(0x00000000, buf.data(), buf.size());
        res.success = true;
        res.is_elf = false;
        res.entry_pc = 0x00000000;
        res.code_start = 0x00000000;
        res.code_end = static_cast<uint32_t>(buf.size());
        res.bytes_loaded = buf.size();
        return res;
    }

private:
    static LoadResult load_elf32(const std::vector<uint8_t>& buf, Memory& mem) {
        LoadResult res;
        res.is_elf = true;

        uint8_t ei_class = buf[4]; // 1 = 32-bit, 2 = 64-bit
        uint8_t ei_data  = buf[5]; // 1 = little-endian
        if (ei_class != 1) {
            res.error_msg = "Archivo ELF no es de 32 bits (ELFCLASS32 requerido).";
            return res;
        }
        if (ei_data != 1) {
            res.error_msg = "Archivo ELF no es little-endian (ELFDATA2LSB requerido).";
            return res;
        }

        uint16_t e_type      = read_le16(&buf[16]); // 1 = ET_REL, 2 = ET_EXEC, 3 = ET_DYN
        uint16_t e_machine   = read_le16(&buf[18]); // 243 (0xF3) = EM_RISCV
        uint32_t e_entry     = read_le32(&buf[24]);
        uint32_t e_phoff     = read_le32(&buf[28]);
        uint32_t e_shoff     = read_le32(&buf[32]);
        uint16_t e_phentsize = read_le16(&buf[42]);
        uint16_t e_phnum     = read_le16(&buf[44]);
        uint16_t e_shentsize = read_le16(&buf[46]);
        uint16_t e_shnum     = read_le16(&buf[48]);

        if (e_machine != 243) {
            res.error_msg = "Archivo ELF no corresponde a arquitectura RISC-V (e_machine != 243).";
            return res;
        }

        size_t total_loaded = 0;
        uint32_t min_addr = 0xFFFFFFFF;
        uint32_t max_addr = 0;

        // 1. Load PT_LOAD segments from Program Header Table (if present)
        if (e_phoff != 0 && e_phnum > 0 && e_phentsize >= 32) {
            for (uint16_t i = 0; i < e_phnum; ++i) {
                size_t ph_pos = static_cast<size_t>(e_phoff) + static_cast<size_t>(i) * e_phentsize;
                if (ph_pos + 32 > buf.size()) break;

                uint32_t p_type   = read_le32(&buf[ph_pos + 0]);
                uint32_t p_offset = read_le32(&buf[ph_pos + 4]);
                uint32_t p_vaddr  = read_le32(&buf[ph_pos + 8]);
                uint32_t p_filesz = read_le32(&buf[ph_pos + 16]);
                uint32_t p_memsz  = read_le32(&buf[ph_pos + 20]);

                // PT_LOAD == 1
                if (p_type == 1 && p_memsz > 0) {
                    if (static_cast<size_t>(p_offset) + p_filesz > buf.size()) {
                        res.error_msg = "Segmento PT_LOAD excede tamaño del archivo ELF.";
                        return res;
                    }
                    mem.write_block(p_vaddr, &buf[p_offset], p_filesz);
                    // Zero-fill BSS region (p_memsz > p_filesz)
                    for (uint32_t b = p_filesz; b < p_memsz; ++b) {
                        mem.write8(p_vaddr + b, 0);
                    }
                    total_loaded += p_filesz;
                    if (p_vaddr < min_addr) min_addr = p_vaddr;
                    if (p_vaddr + p_memsz > max_addr) max_addr = p_vaddr + p_memsz;
                }
            }
        }

        // 2. Fallback for relocatable object files (.o / ET_REL) without PT_LOAD segments,
        //    plus symbol extraction from .symtab / .strtab
        if (e_shoff != 0 && e_shnum > 0 && e_shentsize >= 40) {
            uint32_t reloc_alloc_cursor = 0x00000000;
            for (uint16_t i = 0; i < e_shnum; ++i) {
                size_t sh_pos = static_cast<size_t>(e_shoff) + static_cast<size_t>(i) * e_shentsize;
                if (sh_pos + 40 > buf.size()) break;

                uint32_t sh_type   = read_le32(&buf[sh_pos + 4]);
                uint32_t sh_flags  = read_le32(&buf[sh_pos + 8]);
                uint32_t sh_addr   = read_le32(&buf[sh_pos + 12]);
                uint32_t sh_offset = read_le32(&buf[sh_pos + 16]);
                uint32_t sh_size   = read_le32(&buf[sh_pos + 20]);
                uint32_t sh_link   = read_le32(&buf[sh_pos + 24]);
                uint32_t sh_entsize = read_le32(&buf[sh_pos + 36]);

                // If no PT_LOAD segments were loaded (e.g., ET_REL), load SHF_ALLOC (0x2) sections
                if (e_type == 1 && (sh_flags & 0x2) && sh_size > 0) {
                    uint32_t target_addr = sh_addr != 0 ? sh_addr : reloc_alloc_cursor;
                    if (sh_type == 1 /* SHT_PROGBITS */ && static_cast<size_t>(sh_offset) + sh_size <= buf.size()) {
                        mem.write_block(target_addr, &buf[sh_offset], sh_size);
                        total_loaded += sh_size;
                    } else if (sh_type == 8 /* SHT_NOBITS */) {
                        for (uint32_t b = 0; b < sh_size; ++b) {
                            mem.write8(target_addr + b, 0);
                        }
                    }
                    if (target_addr < min_addr) min_addr = target_addr;
                    if (target_addr + sh_size > max_addr) max_addr = target_addr + sh_size;
                    reloc_alloc_cursor = (target_addr + sh_size + 3u) & ~3u;
                }

                // Extract symbols from SHT_SYMTAB (2)
                if (sh_type == 2 && sh_entsize >= 16 && sh_link < e_shnum) {
                    size_t str_sh_pos = static_cast<size_t>(e_shoff) + static_cast<size_t>(sh_link) * e_shentsize;
                    if (str_sh_pos + 40 <= buf.size()) {
                        uint32_t str_offset = read_le32(&buf[str_sh_pos + 16]);
                        uint32_t str_size   = read_le32(&buf[str_sh_pos + 20]);
                        if (static_cast<size_t>(str_offset) + str_size <= buf.size()) {
                            uint32_t num_syms = sh_size / sh_entsize;
                            for (uint32_t s = 0; s < num_syms; ++s) {
                                size_t sym_pos = static_cast<size_t>(sh_offset) + static_cast<size_t>(s) * sh_entsize;
                                if (sym_pos + 16 > buf.size()) break;
                                uint32_t st_name  = read_le32(&buf[sym_pos + 0]);
                                uint32_t st_value = read_le32(&buf[sym_pos + 4]);
                                uint8_t  st_info  = buf[sym_pos + 12];
                                uint16_t st_shndx = read_le16(&buf[sym_pos + 14]);
                                uint8_t  st_type  = st_info & 0xF;

                                // Keep valid function/notype/object labels (skip section/file/mapping symbols like $x)
                                if (st_name > 0 && st_name < str_size && st_shndx != 0 &&
                                    (st_type == 0 || st_type == 1 || st_type == 2)) {
                                    const char* cstr = reinterpret_cast<const char*>(&buf[str_offset + st_name]);
                                    std::string sym_name(cstr);
                                    if (!sym_name.empty() && sym_name[0] != '$') {
                                        res.symbols[st_value] = sym_name;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        res.success = true;
        res.entry_pc = e_entry;
        res.code_start = (min_addr == 0xFFFFFFFF) ? 0x00000000 : min_addr;
        res.code_end = (max_addr == 0) ? 0x00000100 : max_addr;
        res.bytes_loaded = total_loaded;
        return res;
    }
};
