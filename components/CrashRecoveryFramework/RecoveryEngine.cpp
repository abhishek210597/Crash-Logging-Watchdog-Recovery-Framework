#include "CrashRecoveryFramework.hpp"
#include <cstdio>
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"

namespace CRF {

RecoveryEngine& RecoveryEngine::getInstance() {
    static RecoveryEngine instance;
    return instance;
}

void RecoveryEngine::init() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    checkBootLoop();
}

void RecoveryEngine::checkBootLoop() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("crf_recovery", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        // Read boot stats
        uint32_t boot_count = 0;
        uint32_t crash_count = 0;
        uint8_t safe_mode_flag = 0;
        uint64_t last_boot_time = 0;

        nvs_get_u32(nvs_handle, "boot_count", &boot_count);
        nvs_get_u32(nvs_handle, "crash_count", &crash_count);
        nvs_get_u8(nvs_handle, "safe_mode", &safe_mode_flag);
        nvs_get_u64(nvs_handle, "last_boot_time", &last_boot_time);

        // Increment boot count
        boot_count++;
        m_boot_count = boot_count;
        m_crash_count = crash_count;

        // Check time since last boot
        uint64_t now_us = esp_timer_get_time();
        
        // If system crashed / rebooted and time since last boot is < 20 seconds,
        // it indicates a potential bootloop.
        esp_reset_reason_t reason = esp_reset_reason();
        bool was_crash = (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || 
                           reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT);

        if (was_crash) {
            crash_count++;
            m_crash_count = crash_count;
            
            if (crash_count >= 3) {
                safe_mode_flag = 1;
                CRF_LOG_CRIT("BOOT LOOP DETECTED! Consecutive crashes: %u. Transitioning to SAFE MODE.", crash_count);
            } else {
                CRF_LOG_WARN("Consecutive crash detected. Crash count: %u/3", crash_count);
            }
        } else {
            // Normal software reset or power on reset reset the crash counter if it stays alive > 20s
            // We can do this in a check, but for now we write the stats
            if (reason == ESP_RST_POWERON || reason == ESP_RST_SW) {
                crash_count = 0;
                safe_mode_flag = 0;
                m_crash_count = 0;
            }
        }

        m_safe_mode = (safe_mode_flag != 0);

        // Update NVS
        nvs_set_u32(nvs_handle, "boot_count", boot_count);
        nvs_set_u32(nvs_handle, "crash_count", crash_count);
        nvs_set_u8(nvs_handle, "safe_mode", safe_mode_flag);
        nvs_set_u64(nvs_handle, "last_boot_time", now_us);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    } else {
        CRF_LOG_ERR("Failed to open NVS namespace 'crf_recovery'");
    }
}

void RecoveryEngine::handleTaskTimeout(TaskHandle_t task_handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* task_name = pcTaskGetName(task_handle);
    CRF_LOG_WARN("Recovery Engine acting on hang of task: %s", task_name ? task_name : "Unknown");

    // Send Watchdog Alert via Slack/Discord
    NotificationSystem::getInstance().sendWatchdogAlert(task_name ? task_name : "Unknown", 5000);

    // Apply configured Recovery Action (for this demo, we'll try system restart)
    RecoveryAction action = RecoveryAction::RESTART_SYSTEM;

    if (action == RecoveryAction::RESTART_SYSTEM) {
        CRF_LOG_CRIT("Recovery action: RESTART SYSTEM. Triggering reboot...");
        triggerSystemRestart(RecoveryAction::RESTART_SYSTEM);
    } else if (action == RecoveryAction::RESTART_TASK) {
        CRF_LOG_INFO("Recovery action: RESTART TASK. Deleting task '%s'...", task_name);
        // Note: Real FreeRTOS task recreation requires storing task parameters
        // which we can mock by calling registered reset handler or deleting task
        vTaskDelete(task_handle);
    }
}

void RecoveryEngine::registerModuleResetCallback(const std::string& name, std::function<void()> reset_cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_module_reset_callbacks[name] = reset_cb;
}

void RecoveryEngine::unregisterModuleResetCallback(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_module_reset_callbacks.erase(name);
}

void RecoveryEngine::clearBootStats() {
    std::lock_guard<std::mutex> lock(m_mutex);
    nvs_handle_t nvs_handle;
    if (nvs_open("crf_recovery", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_u32(nvs_handle, "boot_count", 0);
        nvs_set_u32(nvs_handle, "crash_count", 0);
        nvs_set_u8(nvs_handle, "safe_mode", 0);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        m_boot_count = 0;
        m_crash_count = 0;
        m_safe_mode = false;
        CRF_LOG_INFO("Boot stats cleared successfully.");
    }
}

void RecoveryEngine::triggerSystemRestart(RecoveryAction reason) {
    // Write restart reason to RTC RAM or NVS if needed before rebooting
    CRF_LOG_INFO("Triggering system restart via esp_restart()...");
    vTaskDelay(pdMS_TO_TICKS(500)); // Allow log pipeline to flush
    esp_restart();
}

} // namespace CRF
