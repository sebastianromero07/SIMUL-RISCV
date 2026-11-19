#pragma once

#include "cpu.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#define INVALID_SOCK INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#define INVALID_SOCK (-1)
#endif

// GDB Remote Serial Protocol (RSP) Server for RV32
// Usage from riscv64-unknown-elf-gdb / gdb-multiarch:
//   (gdb) set architecture riscv:rv32
//   (gdb) target remote localhost:1234
class GdbServer {
private:
    static void close_sock(socket_t s) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
    }

    static std::string to_le_hex32(uint32_t val) {
        char buf[16];
        std::snprintf(
            buf, sizeof(buf), "%02x%02x%02x%02x",
            val & 0xFF,
            (val >> 8) & 0xFF,
            (val >> 16) & 0xFF,
            (val >> 24) & 0xFF
        );
        return std::string(buf);
    }

    static uint32_t from_le_hex32(const std::string& hex) {
        if (hex.size() < 8) return 0;
        uint32_t b0 = std::stoul(hex.substr(0, 2), nullptr, 16);
        uint32_t b1 = std::stoul(hex.substr(2, 2), nullptr, 16);
        uint32_t b2 = std::stoul(hex.substr(4, 2), nullptr, 16);
        uint32_t b3 = std::stoul(hex.substr(6, 2), nullptr, 16);
        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }

    static bool send_packet(socket_t client, const std::string& payload) {
        uint8_t csum = 0;
        for (char c : payload) {
            csum += static_cast<uint8_t>(c);
        }
        char footer[8];
        std::snprintf(footer, sizeof(footer), "#%02x", csum);
        std::string pkt = "$" + payload + footer;
        int sent = send(client, pkt.c_str(), static_cast<int>(pkt.size()), 0);
        return sent == static_cast<int>(pkt.size());
    }

public:
    static bool serve(Simulator& sim, uint16_t port = 1234) {
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            std::cerr << "Error al inicializar Winsock.\n";
            return false;
        }
#endif
        socket_t srv = socket(AF_INET, SOCK_STREAM, 0);
        if (srv == INVALID_SOCK) {
            std::cerr << "Error al crear socket TCP para servidor GDB.\n";
            return false;
        }

        int opt = 1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);

        if (bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            listen(srv, 1) != 0) {
            std::cerr << "Error al enlazar el puerto TCP " << port << " para GDB.\n";
            close_sock(srv);
            return false;
        }

        std::cout << "[GDB] Escuchando en localhost:" << port << " ...\n";
        std::cout << "[GDB] Conéctate con: riscv64-unknown-elf-gdb -ex \"set arch riscv:rv32\" -ex \"target remote localhost:" << port << "\"\n";

        sockaddr_in cli_addr{};
#ifdef _WIN32
        int cli_len = sizeof(cli_addr);
#else
        socklen_t cli_len = sizeof(cli_addr);
#endif
        socket_t client = accept(srv, reinterpret_cast<sockaddr*>(&cli_addr), &cli_len);
        if (client == INVALID_SOCK) {
            std::cerr << "Error al aceptar conexión GDB.\n";
            close_sock(srv);
            return false;
        }

        std::cout << "[GDB] Cliente conectado.\n";

        std::string rx_buf;
        char chunk[1024];
        bool running = true;

        while (running) {
            int n = recv(client, chunk, sizeof(chunk), 0);
            if (n <= 0) break;
            rx_buf.append(chunk, n);

            while (!rx_buf.empty()) {
                if (rx_buf[0] == '+' || rx_buf[0] == '-') {
                    rx_buf.erase(0, 1);
                    continue;
                }
                if (rx_buf[0] == 0x03) { // Ctrl-C interrupt
                    rx_buf.erase(0, 1);
                    send_packet(client, "S05");
                    continue;
                }
                size_t start = rx_buf.find('$');
                if (start == std::string::npos) {
                    rx_buf.clear();
                    break;
                }
                if (start > 0) {
                    rx_buf.erase(0, start);
                }
                size_t hash = rx_buf.find('#');
                if (hash == std::string::npos || hash + 2 >= rx_buf.size()) {
                    break; // Wait for more bytes
                }

                std::string cmd = rx_buf.substr(1, hash - 1);
                rx_buf.erase(0, hash + 3);

                // Acknowledge packet
                send(client, "+", 1, 0);

                if (cmd == "?") {
                    send_packet(client, "S05"); // SIGTRAP
                } else if (cmd.rfind("qSupported", 0) == 0) {
                    send_packet(client, "PacketSize=4000;swbreak+;hwbreak+");
                } else if (cmd == "qAttached") {
                    send_packet(client, "1");
                } else if (cmd.rfind("H", 0) == 0) {
                    send_packet(client, "OK");
                } else if (cmd == "g") {
                    std::ostringstream oss;
                    for (uint32_t i = 0; i < 32; ++i) {
                        oss << to_le_hex32(sim.get_reg(i));
                    }
                    oss << to_le_hex32(sim.get_pc());
                    send_packet(client, oss.str());
                } else if (cmd.rfind("G", 0) == 0) {
                    std::string data = cmd.substr(1);
                    for (uint32_t i = 0; i < 32 && (i + 1) * 8 <= data.size(); ++i) {
                        sim.set_reg_direct(i, from_le_hex32(data.substr(i * 8, 8)));
                    }
                    if (data.size() >= 33 * 8) {
                        sim.set_pc(from_le_hex32(data.substr(32 * 8, 8)));
                    }
                    send_packet(client, "OK");
                } else if (cmd.rfind("p", 0) == 0) {
                    uint32_t reg_id = std::stoul(cmd.substr(1), nullptr, 16);
                    if (reg_id < 32) {
                        send_packet(client, to_le_hex32(sim.get_reg(reg_id)));
                    } else if (reg_id == 32) {
                        send_packet(client, to_le_hex32(sim.get_pc()));
                    } else {
                        send_packet(client, "00000000");
                    }
                } else if (cmd.rfind("P", 0) == 0) {
                    size_t eq = cmd.find('=');
                    if (eq != std::string::npos) {
                        uint32_t reg_id = std::stoul(cmd.substr(1, eq - 1), nullptr, 16);
                        uint32_t val = from_le_hex32(cmd.substr(eq + 1));
                        if (reg_id < 32) sim.set_reg_direct(reg_id, val);
                        else if (reg_id == 32) sim.set_pc(val);
                        send_packet(client, "OK");
                    } else {
                        send_packet(client, "E01");
                    }
                } else if (cmd.rfind("m", 0) == 0) {
                    size_t comma = cmd.find(',');
                    if (comma != std::string::npos) {
                        uint32_t addr = std::stoul(cmd.substr(1, comma - 1), nullptr, 16);
                        uint32_t len  = std::stoul(cmd.substr(comma + 1), nullptr, 16);
                        std::ostringstream oss;
                        char hex[4];
                        for (uint32_t i = 0; i < len; ++i) {
                            std::snprintf(hex, sizeof(hex), "%02x", sim.memory().read8(addr + i));
                            oss << hex;
                        }
                        send_packet(client, oss.str());
                    } else {
                        send_packet(client, "E01");
                    }
                } else if (cmd.rfind("M", 0) == 0) {
                    size_t comma = cmd.find(',');
                    size_t colon = cmd.find(':');
                    if (comma != std::string::npos && colon != std::string::npos && colon > comma) {
                        uint32_t addr = std::stoul(cmd.substr(1, comma - 1), nullptr, 16);
                        uint32_t len  = std::stoul(cmd.substr(comma + 1, colon - comma - 1), nullptr, 16);
                        std::string hex = cmd.substr(colon + 1);
                        for (uint32_t i = 0; i < len && (i * 2 + 1) < hex.size(); ++i) {
                            uint8_t b = static_cast<uint8_t>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
                            sim.memory().write8(addr + i, b);
                        }
                        send_packet(client, "OK");
                    } else {
                        send_packet(client, "E01");
                    }
                } else if (cmd == "s" || cmd.rfind("vCont;s", 0) == 0) {
                    StepResult r = sim.step();
                    if (r.status == StepStatus::HALTED) {
                        send_packet(client, "W00");
                    } else if (r.status == StepStatus::ILLEGAL_INSTRUCTION) {
                        send_packet(client, "S04"); // SIGILL
                    } else {
                        send_packet(client, "S05"); // SIGTRAP
                    }
                } else if (cmd == "c" || cmd.rfind("vCont;c", 0) == 0) {
                    const uint64_t max_steps = 10000000;
                    uint64_t count = 0;
                    StepResult r;
                    while (count < max_steps && !sim.is_halted()) {
                        r = sim.step();
                        count++;
                        if (r.status != StepStatus::OK) break;
                    }
                    if (r.status == StepStatus::HALTED) {
                        send_packet(client, "W00");
                    } else if (r.status == StepStatus::ILLEGAL_INSTRUCTION) {
                        send_packet(client, "S04");
                    } else {
                        send_packet(client, "S05");
                    }
                } else if (cmd == "vCont?") {
                    send_packet(client, "vCont;c;s");
                } else if (cmd.rfind("Z0,", 0) == 0 || cmd.rfind("Z1,", 0) == 0) {
                    size_t c1 = cmd.find(',');
                    size_t c2 = cmd.find(',', c1 + 1);
                    uint32_t addr = std::stoul(cmd.substr(c1 + 1, c2 - c1 - 1), nullptr, 16);
                    sim.add_breakpoint(addr);
                    send_packet(client, "OK");
                } else if (cmd.rfind("z0,", 0) == 0 || cmd.rfind("z1,", 0) == 0) {
                    size_t c1 = cmd.find(',');
                    size_t c2 = cmd.find(',', c1 + 1);
                    uint32_t addr = std::stoul(cmd.substr(c1 + 1, c2 - c1 - 1), nullptr, 16);
                    sim.remove_breakpoint(addr);
                    send_packet(client, "OK");
                } else if (cmd == "D" || cmd == "k") {
                    send_packet(client, "OK");
                    running = false;
                } else {
                    // Unsupported packet -> empty response per RSP spec
                    send_packet(client, "");
                }
            }
        }

        close_sock(client);
        close_sock(srv);
#ifdef _WIN32
        WSACleanup();
#endif
        std::cout << "[GDB] Sesión finalizada.\n";
        return true;
    }
};
