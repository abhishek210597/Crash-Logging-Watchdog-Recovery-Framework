#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <map>
#include <chrono>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_http_server.h"

namespace CRF {

// ==========================================
// 1. Core Types & Constants
// ==========================================

enum class LogLevel {
    DEBUG = 0,
    INFO,
    WARNING,
    ERROR,
    CRITICAL,
    FATAL
};

enum class RecoveryAction {
    NONE = 0,
    RESTART_TASK,
    RESTART_MODULE,
    RESTART_SYSTEM,
    SAFE_MODE
};

struct CrashSignature {
    uint32_t magic;
    uint32_t reset_reason;
    char task_name[16];
    uint32_t fault_address;
    uint32_t registers[16]; // RISC-V registers (x1-x15, epc)
    uint32_t call_stack[16]; // Stack trace PC addresses
    uint32_t call_stack_depth;
    char assert_expression[64];
    char assert_file[32];
    uint32_t assert_line;
    uint64_t timestamp_us;
    char uuid[37];
};

struct TaskWatchdogEntry {
    TaskHandle_t task_handle;
    std::string task_name;
    uint32_t timeout_ms;
    uint32_t last_heartbeat;
    bool active;
    std::function<void()> on_timeout_callback;
};

struct SystemMetrics {
    uint32_t uptime_s;
    size_t free_heap;
    size_t min_free_heap;
    float cpu_usage_pct;
    float temperature_c;
    size_t flash_used;
    size_t flash_total;
    uint32_t crash_count;
    uint32_t recovery_count;
    bool in_safe_mode;
};

// ==========================================
// 2. Structured Logging Framework
// ==========================================

class Logger {
public:
    static Logger& getInstance();

    void init(const std::string& log_file_path = "/spiffs/app.log", size_t max_file_size = 512 * 1024, uint8_t max_rotations = 3);
    void log(LogLevel level, const char* file, int line, const char* func, const char* format, ...);
    void setConsoleOutput(bool enabled) { m_console_enabled = enabled; }
    void setFileOutput(bool enabled) { m_file_enabled = enabled; }
    void setLogRotation(size_t max_size, uint8_t max_rotations);
    void setRedactionWords(const std::vector<std::string>& words);
    void dumpLogsToStream(std::function<void(const std::string&)> out_fn);
    void clearLogs();

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string formatLog(LogLevel level, const char* file, int line, const char* func, const std::string& message);
    std::string redactMessage(const std::string& msg);
    void processWriteQueue();
    static void asyncWriteTask(void* pvParameters);

    std::string m_log_path;
    size_t m_max_file_size = 256 * 1024;
    uint8_t m_max_rotations = 3;
    bool m_console_enabled = true;
    bool m_file_enabled = false;
    std::vector<std::string> m_redact_words;
    
    std::mutex m_mutex;
    QueueHandle_t m_queue_handle = nullptr;
    TaskHandle_t m_task_handle = nullptr;
};

#define CRF_LOG_DEBUG(fmt, ...) CRF::Logger::getInstance().log(CRF::LogLevel::DEBUG, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define CRF_LOG_INFO(fmt, ...)  CRF::Logger::getInstance().log(CRF::LogLevel::INFO,  __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define CRF_LOG_WARN(fmt, ...)  CRF::Logger::getInstance().log(CRF::LogLevel::WARNING, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define CRF_LOG_ERR(fmt, ...)   CRF::Logger::getInstance().log(CRF::LogLevel::ERROR, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define CRF_LOG_CRIT(fmt, ...)  CRF::Logger::getInstance().log(CRF::LogLevel::CRITICAL, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define CRF_LOG_FATAL(fmt, ...) CRF::Logger::getInstance().log(CRF::LogLevel::FATAL, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

// ==========================================
// 3. Crash Detection & Database Module
// ==========================================

class CrashManager {
public:
    static CrashManager& getInstance();

    void init();
    bool hasPendingCrashReport() const;
    CrashSignature getPendingCrashReport() const;
    void clearPendingCrashReport();
    
    std::vector<CrashSignature> getCrashHistory();
    void clearCrashHistory();
    bool saveCrashReport(const CrashSignature& report);

    // Fault injection helpers
    void injectNullPointerCrash();
    void injectDivideByZero();
    void injectAssertFailure();
    void injectStackOverflow(uint32_t depth = 1);
    void injectMemoryCorruption();
    void injectWatchdogTimeout();

private:
    CrashManager() = default;
    
    void loadCrashHistory();
    std::vector<CrashSignature> m_history;
    mutable std::mutex m_mutex;
};

// ==========================================
// 4. Watchdog & Heartbeat System
// ==========================================

class WatchdogManager {
public:
    static WatchdogManager& getInstance();

    void init(uint32_t check_interval_ms = 1000);
    esp_err_t registerTask(TaskHandle_t task_handle, uint32_t timeout_ms, std::function<void()> on_timeout = nullptr);
    esp_err_t unregisterTask(TaskHandle_t task_handle);
    esp_err_t heartbeat(TaskHandle_t task_handle = nullptr);
    void getWatchdogStatus(std::function<void(const std::string&, uint32_t, uint32_t, bool)> status_cb);

private:
    WatchdogManager() = default;
    static void watchdogDaemonTask(void* pvParameters);
    void monitorTasks();

    uint32_t m_interval_ms = 1000;
    std::map<TaskHandle_t, TaskWatchdogEntry> m_monitored_tasks;
    std::mutex m_mutex;
    TaskHandle_t m_daemon_handle = nullptr;
};

// ==========================================
// 5. Recovery Engine
// ==========================================

class RecoveryEngine {
public:
    static RecoveryEngine& getInstance();

    void init();
    void handleTaskTimeout(TaskHandle_t task_handle);
    void registerModuleResetCallback(const std::string& name, std::function<void()> reset_cb);
    void unregisterModuleResetCallback(const std::string& name);
    
    bool isSafeMode() const { return m_safe_mode; }
    uint32_t getBootCount() const { return m_boot_count; }
    uint32_t getCrashCount() const { return m_crash_count; }
    void clearBootStats();

    void triggerSystemRestart(RecoveryAction reason);

private:
    RecoveryEngine() = default;
    void checkBootLoop();
    void saveBootStats();

    bool m_safe_mode = false;
    uint32_t m_boot_count = 0;
    uint32_t m_crash_count = 0;
    std::map<std::string, std::function<void()>> m_module_reset_callbacks;
    std::mutex m_mutex;
};

// ==========================================
// 6. State Checkpointing
// ==========================================

class StateCheckpoint {
public:
    static StateCheckpoint& getInstance();

    void init();
    bool saveCheckpoint(const std::string& key, const std::string& value);
    bool loadCheckpoint(const std::string& key, std::string& value);
    bool saveCheckpointBinary(const std::string& key, const void* data, size_t size);
    bool loadCheckpointBinary(const std::string& key, void* out_data, size_t size);
    void clearAllCheckpoints();

private:
    StateCheckpoint() = default;
};

// ==========================================
// 7. Resource Monitor
// ==========================================

class ResourceMonitor {
public:
    static ResourceMonitor& getInstance();

    void init();
    SystemMetrics getMetrics();
    std::string getTaskStatsTable();

private:
    ResourceMonitor() = default;
};

// ==========================================
// 8. Notification System
// ==========================================

class NotificationSystem {
public:
    static NotificationSystem& getInstance();

    void init(const std::string& webhook_url = "");
    void setWebhookUrl(const std::string& url) { m_webhook_url = url; }
    bool sendCrashAlert(const CrashSignature& report);
    bool sendWatchdogAlert(const std::string& task_name, uint32_t timeout_ms);

private:
    NotificationSystem() = default;
    std::string m_webhook_url;
    std::mutex m_mutex;
};

// ==========================================
// 9. Dashboard (Web UI & REST API)
// ==========================================

class Dashboard {
public:
    static Dashboard& getInstance();

    esp_err_t start(uint16_t port = 80);
    void stop();

private:
    Dashboard() = default;
    static esp_err_t indexHandler(httpd_req_t* req);
    static esp_err_t metricsApiHandler(httpd_req_t* req);
    static esp_err_t crashesApiHandler(httpd_req_t* req);
    static esp_err_t logsApiHandler(httpd_req_t* req);
    static esp_err_t triggerFaultApiHandler(httpd_req_t* req);

    httpd_handle_t m_server_handle = nullptr;
    uint16_t m_port = 80;
};

// ==========================================
// 10. CLI Console
// ==========================================

class CLI {
public:
    static CLI& getInstance();

    void init();
    void start();

private:
    CLI() = default;
    static int cmdStatus(int argc, char** argv);
    static int cmdCrashInfo(int argc, char** argv);
    static int cmdInjectFault(int argc, char** argv);
    static int cmdLogs(int argc, char** argv);
    static int cmdClearCrashes(int argc, char** argv);
};

} // namespace CRF
