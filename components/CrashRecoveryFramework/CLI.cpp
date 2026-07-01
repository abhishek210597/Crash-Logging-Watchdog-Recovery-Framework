#include "CrashRecoveryFramework.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"

namespace CRF {

static struct {
    struct arg_int* fault_id;
    struct arg_end* end;
} inject_fault_args;

CLI& CLI::getInstance() {
    static CLI instance;
    return instance;
}

int CLI::cmdStatus(int argc, char** argv) {
    auto metrics = ResourceMonitor::getInstance().getMetrics();
    std::printf("\n================ CRF SYSTEM STATUS ================\n");
    std::printf("  Uptime:         %lu s\n", (unsigned long)metrics.uptime_s);
    std::printf("  Free Heap:      %zu bytes (Min ever: %zu)\n", metrics.free_heap, metrics.min_free_heap);
    std::printf("  Core Temp:      %.1f deg C\n", metrics.temperature_c);
    std::printf("  Safe Mode:      %s\n", metrics.in_safe_mode ? "YES (Active)" : "NO");
    std::printf("  Boots / Crashes: %lu / %lu\n", (unsigned long)metrics.recovery_count, (unsigned long)metrics.crash_count);
    std::printf("===================================================\n");
    
    // Print task details
    std::string task_table = ResourceMonitor::getInstance().getTaskStatsTable();
    std::printf("%s\n", task_table.c_str());

    // Print watchdog status
    std::printf("================ WATCHDOG TELEMETRY ===============\n");
    std::printf("  Monitored Tasks:\n");
    WatchdogManager::getInstance().getWatchdogStatus([](const std::string& name, uint32_t threshold, uint32_t elapsed, bool active) {
        std::printf("    - Task: %-15s Timeout: %4lu ms  Elapsed: %4lu ms  Status: %s\n",
                    name.c_str(), (unsigned long)threshold, (unsigned long)elapsed, active ? "OK" : "HUNG");
    });
    std::printf("===================================================\n\n");
    return 0;
}

int CLI::cmdCrashInfo(int argc, char** argv) {
    auto history = CrashManager::getInstance().getCrashHistory();
    std::printf("\n================ CRASH HISTORY DATABASE ================\n");
    if (history.empty()) {
        std::printf("  No crash reports saved in database.\n");
    } else {
        std::printf("  Total Reports: %u\n\n", history.size());
        for (size_t i = 0; i < history.size(); ++i) {
            const auto& r = history[i];
            std::printf("  Report #%u:\n", i + 1);
            std::printf("    UUID:          %s\n", r.uuid);
            std::printf("    Timestamp:     %llu us\n", r.timestamp_us);
            std::printf("    Task Name:     %s\n", r.task_name);
            if (r.assert_line != 0) {
                std::printf("    Fault Type:    Assertion Failure\n");
                std::printf("    File:Line:     %s:%lu\n", r.assert_file, (unsigned long)r.assert_line);
                std::printf("    Expression:    %s\n", r.assert_expression);
            } else {
                std::printf("    Fault Type:    System Panic / Exception\n");
                std::printf("    Fault Address: 0x%08lx\n", (unsigned long)r.fault_address);
                std::printf("    Reset Reason:  %lu\n", (unsigned long)r.reset_reason);
            }
            // Display stack call addresses
            std::printf("    Call Stack PC List:\n");
            for (uint32_t j = 0; j < r.call_stack_depth; ++j) {
                std::printf("      [%lu] 0x%08lx\n", (unsigned long)j, (unsigned long)r.call_stack[j]);
            }
            std::printf("------------------------------------------------------\n");
        }
    }
    std::printf("========================================================\n\n");
    return 0;
}

int CLI::cmdInjectFault(int argc, char** argv) {
    int nerrors = arg_parse(argc, argv, (void**)&inject_fault_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, inject_fault_args.end, argv[0]);
        std::printf("Use 'help inject_fault' to see available parameters.\n");
        return 1;
    }

    int id = inject_fault_args.fault_id->ival[0];
    std::printf("Injecting fault #%d. System will reset.\n", id);
    vTaskDelay(pdMS_TO_TICKS(500)); // Flush output

    switch (id) {
        case 1: CrashManager::getInstance().injectNullPointerCrash(); break;
        case 2: CrashManager::getInstance().injectDivideByZero(); break;
        case 3: CrashManager::getInstance().injectAssertFailure(); break;
        case 4: CrashManager::getInstance().injectWatchdogTimeout(); break;
        case 5: CrashManager::getInstance().injectStackOverflow(32); break;
        case 6: CrashManager::getInstance().injectMemoryCorruption(); break;
        default:
            std::printf("Invalid fault ID. Choose between 1 and 6:\n");
            std::printf("  1: Null Pointer Dereference\n");
            std::printf("  2: Divide by Zero\n");
            std::printf("  3: Assertion Failure\n");
            std::printf("  4: Task Watchdog Hang\n");
            std::printf("  5: Stack Overflow\n");
            std::printf("  6: Heap Memory Corruption\n");
            break;
    }
    return 0;
}

int CLI::cmdLogs(int argc, char** argv) {
    std::printf("\n=================== SYSTEM LOG DUMP ===================\n");
    Logger::getInstance().dumpLogsToStream([](const std::string& line) {
        std::printf("%s", line.c_str());
    });
    std::printf("=======================================================\n\n");
    return 0;
}

int CLI::cmdClearCrashes(int argc, char** argv) {
    CrashManager::getInstance().clearCrashHistory();
    std::printf("Crash history cleared successfully.\n");
    return 0;
}

void CLI::init() {
    esp_console_config_t console_config = ESP_CONSOLE_CONFIG_DEFAULT();
    esp_console_init(&console_config);

    // Register status command
    esp_console_cmd_t cmd = {};
    cmd.command = "status";
    cmd.help = "Show system health metrics, FreeRTOS tasks stats and watchdogs";
    cmd.hint = nullptr;
    cmd.func = &CLI::cmdStatus;
    esp_console_cmd_register(&cmd);

    // Register crashinfo command
    cmd.command = "crashinfo";
    cmd.help = "Show details of past crash reports stored in SPIFFS";
    cmd.hint = nullptr;
    cmd.func = &CLI::cmdCrashInfo;
    esp_console_cmd_register(&cmd);

    // Register logs command
    cmd.command = "logs";
    cmd.help = "Show complete diagnostic logs from persistent file storage";
    cmd.hint = nullptr;
    cmd.func = &CLI::cmdLogs;
    esp_console_cmd_register(&cmd);

    // Register clear_crashes command
    cmd.command = "clear_crashes";
    cmd.help = "Clear all crash logs database entries from SPIFFS";
    cmd.hint = nullptr;
    cmd.func = &CLI::cmdClearCrashes;
    esp_console_cmd_register(&cmd);

    // Register inject_fault command
    inject_fault_args.fault_id = arg_int1("i", "id", "<1-6>", "Fault ID to simulate");
    inject_fault_args.end = arg_end(2);
    
    cmd.command = "inject_fault";
    cmd.help = "Simulate hardware faults / software thread hangs";
    cmd.hint = nullptr;
    cmd.func = &CLI::cmdInjectFault;
    cmd.argtable = &inject_fault_args;
    esp_console_cmd_register(&cmd);
}

void CLI::start() {
    esp_console_repl_t* repl = nullptr;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "faultshield> ";
    repl_config.max_cmdline_length = 128;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_console_new_repl_uart(&uart_config, &repl_config, &repl);
    if (repl) {
        esp_console_start_repl(repl);
        CRF_LOG_INFO("CLI Interactive Console started on Serial UART.");
    } else {
        CRF_LOG_ERR("Failed to start CLI REPL console.");
    }
}

} // namespace CRF
