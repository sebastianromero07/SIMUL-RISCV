CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Iinclude

ifeq ($(OS),Windows_NT)
    LDFLAGS = -lws2_32 -static
    TARGET = build/awesome-simulator.exe
else
    LDFLAGS =
    TARGET = build/awesome-simulator
endif

all: $(TARGET)

$(TARGET): src/main.cpp include/cpu.hpp include/memory.hpp include/disassembler.hpp include/elf_loader.hpp include/gdb_server.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/main.cpp $(LDFLAGS) -o $(TARGET)

clean:
	rm -rf build/*

.PHONY: all clean
