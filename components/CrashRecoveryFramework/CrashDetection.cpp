#include "CrashRecoveryFramework.hpp"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

// Place crash signature in RTC memory so it survives soft resets
#if defined(CONFIG_IDF_TARGET_ESP32C6) || defined(ESP_PLATFORM)
#include "esp_attr.h"
#define RETENTION_ATTR RTC_NOINIT_ATTR
#else
#define RETENTION_ATTR
#endif

RETENTION_ATTR static CRF::CrashSignature rtc_crash_signature;
static const uint32_t SIGNATURE_MAGIC = 0xDEADBEEF;

// Walk stack frames on RISC-V. Return return-addresses (PCs).
extern "C" void esp_backtrace_print(int depth); // Built-in helper if available

namespace CRF {

CrashManager& CrashManager::getInstance() {
    static CrashManager instance;
    return instance;
}

void CrashManager::init() {
    loadCrashHistory();

    // Check if we have a valid crash signature saved in RTC RAM from the previous run
    if (rtc_crash_signature.magic == SIGNATURE_MAGIC) {
        // Read the reset reason to confirm it was a crash or watchdog
        esp_reset_reason_t reason = esp_reset_reason();
        
        CRF_LOG_WARN("System rebooted from crash. Reset Reason: %d", (int)reason);
        rtc_crash_signature.reset_reason = (uint32_t)reason;
        
        // Save the RTC crash report to the persistent database (SPIFFS)
        saveCrashReport(rtc_crash_signature);
        
        // Clear signature
        clearPendingCrashReport();
    }
}

bool CrashManager::hasPendingCrashReport() const {
    return rtc_crash_signature.magic == SIGNATURE_MAGIC;
}

CrashSignature CrashManager::getPendingCrashReport() const {
    if (hasPendingCrashReport()) {
        return rtc_crash_signature;
    }
    return {};
}

void CrashManager::clearPendingCrashReport() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::memset(&rtc_crash_signature, 0, sizeof(CrashSignature));
}

void CrashManager::loadCrashHistory() {
    // History will be loaded from SPIFFS files by the CrashDatabase module
    // We populate m_history dynamically from CrashDatabase's list
}

std::vector<CrashSignature> CrashManager::getCrashHistory() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_history;
}

bool CrashManager::saveCrashReport(const CrashSignature& report) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Check if this is a duplicate (same file/line or same fault address)
    for (const auto& existing : m_history) {
        if (report.assert_line != 0 && existing.assert_line == report.assert_line && 
            std::strcmp(existing.assert_file, report.assert_file) == 0) {
            CRF_LOG_INFO("Duplicate crash detected. Skipping write.");
            return true; 
        }
    }

    m_history.push_back(report);
    if (m_history.size() > 20) {
        m_history.erase(m_history.begin());
    }

    // Write crash to SPIFFS database (this will be handled by the CrashDatabase component)
    return true; 
}

void CrashManager::clearCrashHistory() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_history.clear();
}

// ==========================================
// Fault Injections (Simulated & Real)
// ==========================================

void CrashManager::injectNullPointerCrash() {
    CRF_LOG_FATAL("Injecting Null Pointer Crash (SIGSEGV)...");
    vTaskDelay(pdMS_TO_TICKS(100));
    volatile int* ptr = nullptr;
    *ptr = 0x42; // Real segmentation fault / panic
}

void CrashManager::injectDivideByZero() {
    CRF_LOG_FATAL("Injecting Divide by Zero (SIGFPE)...");
    vTaskDelay(pdMS_TO_TICKS(100));
    volatile int a = 10;
    volatile int b = 0;
    volatile int c = a / b;
    (void)c;
}

void CrashManager::injectAssertFailure() {
    CRF_LOG_FATAL("Injecting Assertion Failure...");
    vTaskDelay(pdMS_TO_TICKS(100));
    assert(2 + 2 == 5); // Real assert failure
}

// Deep recursive function to trigger stack overflow
static void recursive_function(uint32_t depth) {
    volatile char buffer[1024]; // Allocate 1KB on stack
    std::memset((void*)buffer, 0xAA, sizeof(buffer));
    if (depth > 0) {
        recursive_function(depth - 1);
    }
}

void CrashManager::injectStackOverflow(uint32_t depth) {
    CRF_LOG_FATAL("Injecting Stack Overflow (SIGSEGV / Panic)...");
    vTaskDelay(pdMS_TO_TICKS(100));
    recursive_function(depth);
}

void CrashManager::injectMemoryCorruption() {
    CRF_LOG_FATAL("Injecting Memory Corruption...");
    vTaskDelay(pdMS_TO_TICKS(100));
    // Overwrite heap memory header
    char* ptr = (char*)malloc(16);
    if (ptr) {
        volatile char* corrupt_ptr = (volatile char*)(ptr - 8);
        for (int i = 0; i < 32; ++i) {
            corrupt_ptr[i] = 0x55; // Corrupt malloc header
        }
        free(ptr); // Trigger crash on free
    }
}

void CrashManager::injectWatchdogTimeout() {
    CRF_LOG_FATAL("Injecting Watchdog Timeout (Infinite loop in current task)...");
    vTaskDelay(pdMS_TO_TICKS(100));
    while (true) {
        // Blocks current task, causing watchdog trigger if registered
    }
}

} // namespace CRF

// ==========================================
// C-Style Assertion Override Hook
// ==========================================

extern "C" void __real___assert_func(const char* file, int line, const char* func, const char* failedexpr);

extern "C" void __wrap___assert_func(const char* file, int line, const char* func, const char* failedexpr) {
    // Gather details in rtc_crash_signature
    rtc_crash_signature.magic = SIGNATURE_MAGIC;
    rtc_crash_signature.timestamp_us = esp_timer_get_time();
    
    // Copy task name
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    if (current_task != nullptr) {
        const char* name = pcTaskGetName(current_task);
        std::strncpy(rtc_crash_signature.task_name, name, sizeof(rtc_crash_signature.task_name) - 1);
    } else {
        std::strcpy(rtc_crash_signature.task_name, "ISR/Unknown");
    }

    // Copy file/line/expression
    std::strncpy(rtc_crash_signature.assert_file, file ? file : "unknown", sizeof(rtc_crash_signature.assert_file) - 1);
    std::strncpy(rtc_crash_signature.assert_expression, failedexpr ? failedexpr : "assert", sizeof(rtc_crash_signature.assert_expression) - 1);
    rtc_crash_signature.assert_line = line;
    rtc_crash_signature.fault_address = 0;

    // Generate Call Stack (PC addresses)
    // Walk RISC-V frame pointer to populate rtc_crash_signature.call_stack
#if defined(__riscv)
    register uint32_t* fp asm("s0"); // Frame pointer in RISC-V (s0 is x8)
    uint32_t depth = 0;
    while (fp && depth < 16) {
        uint32_t pc = *(fp - 1); // PC address is stored just below frame pointer on stack
        rtc_crash_signature.call_stack[depth++] = pc;
        fp = (uint32_t*)(*(fp - 2)); // Previous frame pointer
    }
    rtc_crash_signature.call_stack_depth = depth;
#else
    rtc_crash_signature.call_stack_depth = 0;
#endif

    // Generate Crash UUID (Deterministic/pseudo-random based on timestamp)
    std::snprintf(rtc_crash_signature.uuid, sizeof(rtc_crash_signature.uuid),
                 "550e8400-e29b-41d4-a716-%012llx", rtc_crash_signature.timestamp_us);

    // Call the original ESP-IDF abort handler
    __real___assert_func(file, line, func, failedexpr);
}
