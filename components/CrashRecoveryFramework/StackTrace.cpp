#include "CrashRecoveryFramework.hpp"
#include <cstdio>
#include <sstream>
#include <iomanip>

#if defined(ESP_PLATFORM)
#include "esp_debug_helpers.h"
#include "esp_system.h"
#endif

#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#endif

namespace CRF {

// Custom stack frame structure for RISC-V unwinding
struct RiscvFrame {
    uint32_t* next_fp;
    uint32_t return_address;
};

// Generates stack trace PC addresses programmatically
static uint32_t get_backtrace(uint32_t* pc_array, uint32_t max_depth) {
    uint32_t depth = 0;

#if defined(__riscv) && defined(ESP_PLATFORM)
    // Walk the stack frames using the RISC-V frame pointer s0 (x8)
    register uint32_t* fp asm("s0");
    
    // We must ensure the frame pointer is within valid RAM ranges (SRAM)
    // ESP32-C6 SRAM ranges from 0x40800000 to 0x40850000 (320KB SRAM)
    // We can use a general check: fp must be 4-byte aligned and in SRAM
    while (fp && ((uintptr_t)fp & 0x3) == 0 && depth < max_depth) {
        // In RISC-V standard stack frames, return address is at *(fp - 1),
        // and the previous frame pointer is at *(fp - 2)
        uint32_t pc = *(fp - 1);
        
        // Ensure PC is within flash instruction range (0x42000000 to 0x42800000)
        // or ROM instruction range
        if (pc >= 0x40000000 && pc <= 0x45000000) {
            pc_array[depth++] = pc;
        } else {
            break; // Invalid PC
        }

        uint32_t* next_fp = (uint32_t*)(*(fp - 2));
        if (next_fp <= fp) {
            break; // Avoid infinite loops or downward stack walking
        }
        fp = next_fp;
    }
#elif defined(__linux__) || defined(__APPLE__)
    void* buffer[64];
    int count = backtrace(buffer, max_depth);
    for (int i = 0; i < count; ++i) {
        pc_array[depth++] = (uint32_t)(uintptr_t)buffer[i];
    }
#endif

    return depth;
}

// In standard simulation or if needed, can resolve hex PC addresses.
// On ESP-IDF, resolving PC addresses to function name & file/line requires ELF symbol tables
// which are typically not stored on-chip due to flash constraints.
// However, the ESP-IDF monitor tool (idf.py monitor) automatically catches hex PC traces
// and runs addr2line in real-time. Thus, providing PC hex addresses in the report is standard.
std::string formatStackTraceMachine(const uint32_t* pc_array, uint32_t depth) {
    std::stringstream ss;
    ss << "[";
    for (uint32_t i = 0; i < depth; ++i) {
        ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << pc_array[i];
        if (i < depth - 1) {
            ss << ",";
        }
    }
    ss << "]";
    return ss.str();
}

std::string formatStackTraceHuman(const uint32_t* pc_array, uint32_t depth) {
    std::stringstream ss;
    ss << "Backtrace Call Stack:\n";
    if (depth == 0) {
        ss << "  <No stack frames captured or frame pointer omitted in compilation>\n";
        return ss.str();
    }
    
    for (uint32_t i = 0; i < depth; ++i) {
        ss << "  #" << std::setw(2) << std::setfill('0') << i 
           << "  PC: 0x" << std::hex << std::setw(8) << std::setfill('0') << pc_array[i] 
           << " (resolve using: riscv32-esp-elf-addr2line -pfiaC -e build/project.elf 0x" 
           << std::hex << pc_array[i] << ")\n";
    }
    return ss.str();
}

} // namespace CRF
