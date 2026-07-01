#include "CrashRecoveryFramework.hpp"
#include <cstdio>
#include <cstring>
#include "esp_http_client.h"

namespace CRF {

NotificationSystem& NotificationSystem::getInstance() {
    static NotificationSystem instance;
    return instance;
}

void NotificationSystem::init(const std::string& webhook_url) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_webhook_url = webhook_url;
    CRF_LOG_INFO("Notification System initialized. Webhook URL configured: %s", 
                 webhook_url.empty() ? "None" : "Yes");
}

bool NotificationSystem::sendCrashAlert(const CrashSignature& report) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_webhook_url.empty()) {
        return false;
    }

    char content[512];
    if (report.assert_line != 0) {
        std::snprintf(content, sizeof(content),
                     "{\n"
                     "  \"content\": \"🚨 **Crash Recovery Framework Alert** 🚨\\n"
                     "**Type**: Assertion Failure\\n"
                     "**Task**: %s\\n"
                     "**File**: %s:%lu\\n"
                     "**Expr**: `%s`\\n"
                     "**UUID**: %s\"\n"
                     "}",
                     report.task_name, report.assert_file, (unsigned long)report.assert_line, 
                     report.assert_expression, report.uuid);
    } else {
        std::snprintf(content, sizeof(content),
                     "{\n"
                     "  \"content\": \"🚨 **Crash Recovery Framework Alert** 🚨\\n"
                     "**Type**: Hardware Crash / System Panic\\n"
                     "**Task**: %s\\n"
                     "**Fault Address**: 0x%08lx\\n"
                     "**Reset Reason**: %lu\\n"
                     "**UUID**: %s\"\n"
                     "}",
                     report.task_name, (unsigned long)report.fault_address, 
                     (unsigned long)report.reset_reason, report.uuid);
    }

    esp_http_client_config_t config = {};
    config.url = m_webhook_url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 3000;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, content, std::strlen(content));

    esp_err_t err = esp_http_client_perform(client);
    bool success = (err == ESP_OK);
    
    if (success) {
        int status = esp_http_client_get_status_code(client);
        CRF_LOG_INFO("Webhook alert sent. Response status: %d", status);
    } else {
        CRF_LOG_WARN("Failed to send Webhook alert. HTTP Client error: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return success;
}

bool NotificationSystem::sendWatchdogAlert(const std::string& task_name, uint32_t timeout_ms) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_webhook_url.empty()) {
        return false;
    }

    char content[256];
    std::snprintf(content, sizeof(content),
                 "{\n"
                 "  \"content\": \"⚠️ **Watchdog Alert** ⚠️\\n"
                 "**Task Hung**: `%s`\\n"
                 "**Timeout Threshold**: %lu ms\\n"
                 "**Status**: Triggering recovery...\"\n"
                 "}",
                 task_name.c_str(), (unsigned long)timeout_ms);

    esp_http_client_config_t config = {};
    config.url = m_webhook_url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 3000;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, content, std::strlen(content));

    esp_err_t err = esp_http_client_perform(client);
    bool success = (err == ESP_OK);
    
    esp_http_client_cleanup(client);
    return success;
}

} // namespace CRF
