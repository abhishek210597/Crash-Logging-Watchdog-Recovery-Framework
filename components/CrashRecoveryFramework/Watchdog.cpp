#include "CrashRecoveryFramework.hpp"
#include <cstdio>
#include "esp_timer.h"

namespace CRF {

WatchdogManager& WatchdogManager::getInstance() {
    static WatchdogManager instance;
    return instance;
}

void WatchdogManager::init(uint32_t check_interval_ms) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_interval_ms = check_interval_ms;

    if (m_daemon_handle == nullptr) {
        xTaskCreatePinnedToCore(
            WatchdogManager::watchdogDaemonTask,
            "CRF_WdtDaemon",
            3072,
            this,
            configMAX_PRIORITIES - 1, // Run at very high priority to prevent starvation of the watchdog itself!
            &m_daemon_handle,
            1  // CPU Core
        );
        CRF_LOG_INFO("Watchdog Manager initialized. Check interval: %u ms", check_interval_ms);
    }
}

esp_err_t WatchdogManager::registerTask(TaskHandle_t task_handle, uint32_t timeout_ms, std::function<void()> on_timeout) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (task_handle == nullptr) {
        task_handle = xTaskGetCurrentTaskHandle();
    }

    const char* name = pcTaskGetName(task_handle);
    TaskWatchdogEntry entry;
    entry.task_handle = task_handle;
    entry.task_name = name ? name : "Unknown";
    entry.timeout_ms = timeout_ms;
    entry.last_heartbeat = xTaskGetTickCount();
    entry.active = true;
    entry.on_timeout_callback = on_timeout;

    m_monitored_tasks[task_handle] = entry;
    CRF_LOG_INFO("Watchdog registered task: %s, timeout: %u ms", entry.task_name.c_str(), timeout_ms);
    return ESP_OK;
}

esp_err_t WatchdogManager::unregisterTask(TaskHandle_t task_handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (task_handle == nullptr) {
        task_handle = xTaskGetCurrentTaskHandle();
    }

    auto it = m_monitored_tasks.find(task_handle);
    if (it != m_monitored_tasks.end()) {
        CRF_LOG_INFO("Watchdog unregistered task: %s", it->second.task_name.c_str());
        m_monitored_tasks.erase(it);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t WatchdogManager::heartbeat(TaskHandle_t task_handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (task_handle == nullptr) {
        task_handle = xTaskGetCurrentTaskHandle();
    }

    auto it = m_monitored_tasks.find(task_handle);
    if (it != m_monitored_tasks.end()) {
        it->second.last_heartbeat = xTaskGetTickCount();
        it->second.active = true;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

void WatchdogManager::watchdogDaemonTask(void* pvParameters) {
    WatchdogManager* self = static_cast<WatchdogManager*>(pvParameters);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(self->m_interval_ms));
        self->monitorTasks();
    }
}

void WatchdogManager::monitorTasks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint32_t current_ticks = xTaskGetTickCount();

    for (auto& pair : m_monitored_tasks) {
        TaskWatchdogEntry& entry = pair.second;
        if (!entry.active) continue;

        uint32_t elapsed_ticks = current_ticks - entry.last_heartbeat;
        uint32_t elapsed_ms = pdTICKS_TO_MS(elapsed_ticks);

        if (elapsed_ms > entry.timeout_ms) {
            CRF_LOG_CRIT("WATCHDOG TIMEOUT: Task '%s' hung! Missed heartbeat for %u ms (threshold: %u ms)",
                         entry.task_name.c_str(), elapsed_ms, entry.timeout_ms);

            // Execute local custom alert / task timeout routine
            if (entry.on_timeout_callback) {
                entry.on_timeout_callback();
            }

            // Deactivate monitoring for this task to prevent redundant actions
            entry.active = false;

            // Delegate recovery strategy to RecoveryEngine (runs asynchronously or reboots)
            RecoveryEngine::getInstance().handleTaskTimeout(entry.task_handle);
        }
    }
}

void WatchdogManager::getWatchdogStatus(std::function<void(const std::string&, uint32_t, uint32_t, bool)> status_cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint32_t current_ticks = xTaskGetTickCount();

    for (const auto& pair : m_monitored_tasks) {
        const TaskWatchdogEntry& entry = pair.second;
        uint32_t elapsed_ms = pdTICKS_TO_MS(current_ticks - entry.last_heartbeat);
        status_cb(entry.task_name, entry.timeout_ms, elapsed_ms, entry.active);
    }
}

} // namespace CRF
