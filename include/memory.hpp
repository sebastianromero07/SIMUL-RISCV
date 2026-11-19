#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

// Flat 32-bit byte-addressable little-endian memory for RISC-V (RV32).
// Uses a fast contiguous 16 MB buffer for low addresses (0x00000000 - 0x00FFFFFF)
// and on-demand 4 KB pages for the rest of the 32-bit address space (up to 0xFFFFFFFF).
class Memory {
public:
    static constexpr uint32_t FAST_MEM_SIZE = 16 * 1024 * 1024; // 16 MB fast region
    static constexpr uint32_t PAGE_SHIFT = 12;                  // 4 KB pages
    static constexpr uint32_t PAGE_SIZE = 1u << PAGE_SHIFT;     // 4096 bytes
    static constexpr uint32_t PAGE_MASK = PAGE_SIZE - 1;

private:
    std::vector<uint8_t> fast_mem_;
    std::unordered_map<uint32_t, std::array<uint8_t, PAGE_SIZE>> high_pages_;

public:
    Memory() : fast_mem_(FAST_MEM_SIZE, 0) {}

    void reset() {
        std::fill(fast_mem_.begin(), fast_mem_.end(), 0);
        high_pages_.clear();
    }

    uint8_t read8(uint32_t addr) const {
        if (addr < FAST_MEM_SIZE) {
            return fast_mem_[addr];
        }
        uint32_t page_idx = addr >> PAGE_SHIFT;
        auto it = high_pages_.find(page_idx);
        if (it == high_pages_.end()) {
            return 0;
        }
        return it->second[addr & PAGE_MASK];
    }

    void write8(uint32_t addr, uint8_t val) {
        if (addr < FAST_MEM_SIZE) {
            fast_mem_[addr] = val;
            return;
        }
        uint32_t page_idx = addr >> PAGE_SHIFT;
        auto it = high_pages_.find(page_idx);
        if (it == high_pages_.end()) {
            std::array<uint8_t, PAGE_SIZE> page{};
            page[addr & PAGE_MASK] = val;
            high_pages_.emplace(page_idx, page);
        } else {
            it->second[addr & PAGE_MASK] = val;
        }
    }

    // Little-endian 16-bit load
    uint16_t read16(uint32_t addr) const {
        if (addr + 1 < FAST_MEM_SIZE) {
            return static_cast<uint16_t>(fast_mem_[addr]) |
                   (static_cast<uint16_t>(fast_mem_[addr + 1]) << 8);
        }
        uint16_t b0 = read8(addr);
        uint16_t b1 = read8(addr + 1);
        return b0 | static_cast<uint16_t>(b1 << 8);
    }

    // Little-endian 16-bit store
    void write16(uint32_t addr, uint16_t val) {
        if (addr + 1 < FAST_MEM_SIZE) {
            fast_mem_[addr]     = static_cast<uint8_t>(val & 0xFF);
            fast_mem_[addr + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
            return;
        }
        write8(addr,     static_cast<uint8_t>(val & 0xFF));
        write8(addr + 1, static_cast<uint8_t>((val >> 8) & 0xFF));
    }

    // Little-endian 32-bit load
    uint32_t read32(uint32_t addr) const {
        if (addr + 3 < FAST_MEM_SIZE) {
            return static_cast<uint32_t>(fast_mem_[addr]) |
                   (static_cast<uint32_t>(fast_mem_[addr + 1]) << 8) |
                   (static_cast<uint32_t>(fast_mem_[addr + 2]) << 16) |
                   (static_cast<uint32_t>(fast_mem_[addr + 3]) << 24);
        }
        uint32_t b0 = read8(addr);
        uint32_t b1 = read8(addr + 1);
        uint32_t b2 = read8(addr + 2);
        uint32_t b3 = read8(addr + 3);
        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }

    // Little-endian 32-bit store
    void write32(uint32_t addr, uint32_t val) {
        if (addr + 3 < FAST_MEM_SIZE) {
            fast_mem_[addr]     = static_cast<uint8_t>(val & 0xFF);
            fast_mem_[addr + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
            fast_mem_[addr + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
            fast_mem_[addr + 3] = static_cast<uint8_t>((val >> 24) & 0xFF);
            return;
        }
        write8(addr,     static_cast<uint8_t>(val & 0xFF));
        write8(addr + 1, static_cast<uint8_t>((val >> 8) & 0xFF));
        write8(addr + 2, static_cast<uint8_t>((val >> 16) & 0xFF));
        write8(addr + 3, static_cast<uint8_t>((val >> 24) & 0xFF));
    }

    // Bulk write into memory starting at base_addr
    void write_block(uint32_t base_addr, const uint8_t* data, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            write8(base_addr + static_cast<uint32_t>(i), data[i]);
        }
    }
};
