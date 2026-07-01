#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "CrashRecoveryFramework.hpp"

static const char* TAG = "MainApp";
static TaskHandle_t s_worker_task_handle = nullptr;
static bool s_worker_should_hang = false;

// Simulated worker thread that performs "sensor data logging"
static void sensor_logging_task(void* pvParameters) {
    CRF_LOG_INFO("Sensor Logging Task started. Registering with Watchdog...");
    
    // Register current task with watchdog, 3-second timeout threshold
    CRF::WatchdogManager::getInstance().registerTask(NULL, 3000, []() {
        CRF_LOG_WARN("Sensor task callback: I am about to hang or get killed!");
    });

    uint32_t count = 0;
    while (true) {
        if (s_worker_should_hang) {
            CRF_LOG_WARN("Sensor task is entering simulated infinite hang...");
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1000)); // Hang forever
            }
        }

        // Simulating normal work
        float mock_temperature = 22.5f + (rand() % 100) / 50.0f;
        CRF_LOG_DEBUG("Sensor read #%u - Temperature: %.2f C", count++, mock_temperature);

        // Save mock checkpoint of the reading count
        CRF::StateCheckpoint::getInstance().saveCheckpoint("sensor_reads", std::to_string(count));

        // Signal healthy state to watchdog
        CRF::WatchdogManager::getInstance().heartbeat();

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Function to initialize Wi-Fi Access Point so user can connect to Web Dashboard
static void init_wifi_ap() {
    CRF_LOG_INFO("Configuring local Wi-Fi Access Point...");
    
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {};
    std::strcpy((char*)wifi_config.ap.ssid, "FaultShield_Recovery");
    std::strcpy((char*)wifi_config.ap.password, "recovery123");
    wifi_config.ap.ssid_len = std::strlen("FaultShield_Recovery");
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    if (std::strlen("recovery123") == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    CRF_LOG_INFO("Access Point 'FaultShield_Recovery' started. Password: 'recovery123'");
    CRF_LOG_INFO("Open Web Dashboard at: http://192.168.4.1");
}

extern "C" void app_main() {
    // 1. Initialize SPIFFS
    CRF_LOG_INFO("Mounting SPIFFS partition...");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = nullptr,
        .max_files = 8,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        std::printf("Failed to mount SPIFFS (%s)\n", esp_err_to_name(ret));
    }

    // 2. Initialize NVS (used for recovery boot loops and state checkpointing)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 3. Initialize core fault recovery modules (NVS loop detection)
    CRF::RecoveryEngine::getInstance().init();

    // 4. Initialize Logger (Outputs to console & persistent flash logs on SPIFFS)
    CRF::Logger::getInstance().init("/spiffs/app.log", 256 * 1024, 3);
    
    // Register sensitive patterns to redact for security demo
    CRF::Logger::getInstance().setRedactionWords({"recovery123", "secretToken99"});

    // 5. Initialize post-mortem crash manager (reads RTC RAM for previous crashes)
    CRF::CrashManager::getInstance().init();

    CRF_LOG_INFO("=================================================");
    CRF_LOG_INFO(" FaultShield Crash Watchdog Framework Booted");
    CRF_LOG_INFO(" Version: 1.0.0 | Commit: v1.0.0-release");
    CRF_LOG_INFO(" Boot Count: %u | Safe Mode: %s", 
                 CRF::RecoveryEngine::getInstance().getBootCount(),
                 CRF::RecoveryEngine::getInstance().isSafeMode() ? "YES" : "NO");
    CRF_LOG_INFO("=================================================");

    // Check if we are booted in Safe Mode (due to rapid crash loops)
    if (CRF::RecoveryEngine::getInstance().isSafeMode()) {
        CRF_LOG_CRIT("!!! SYSTEM OPERATING IN SAFE BOOT CONFIGURATION !!!");
        CRF_LOG_CRIT("Peripherals bypassed. Starting interactive Serial debug console only.");
        
        // In safe mode, we only load minimal CLI console
        CRF::CLI::getInstance().init();
        CRF::CLI::getInstance().start();
    } 
    else {
        // Normal boot: Load web dashboard and logging workers
        CRF_LOG_INFO("Normal boot mode. Starting network and application tasks.");
        
        // 6. Initialize State Checkpoint system
        CRF::StateCheckpoint::getInstance().init();
        std::string loaded_reads;
        if (CRF::StateCheckpoint::getInstance().loadCheckpoint("sensor_reads", loaded_reads)) {
            CRF_LOG_INFO("Restored checkpoint: Previous count of sensor reads: %s", loaded_reads.c_str());
        }

        // 7. Initialize resource monitors (CPU, temp)
        CRF::ResourceMonitor::getInstance().init();

        // 8. Start Wi-Fi Access Point & Web Dashboard
        init_wifi_ap();
        CRF::Dashboard::getInstance().start(80);

        // 9. Start watchdog monitor
        CRF::WatchdogManager::getInstance().init(1000); // Check tasks every 1 second

        // 10. Start interactive UART console
        CRF::CLI::getInstance().init();
        CRF::CLI::getInstance().start();

        // 11. Launch background sensor worker thread
        xTaskCreatePinnedToCore(
            sensor_logging_task,
            "CRF_SensorLog",
            3072,
            nullptr,
            5, // Medium priority
            &s_worker_task_handle,
            0  // CPU Core 0
        );

        // Setup module reset/reinit callback
        CRF::RecoveryEngine::getInstance().registerModuleResetCallback("SensorModule", []() {
            CRF_LOG_INFO("Reinitializing Sensor Module...");
            s_worker_should_hang = false;
        });
    }

    // Keep app_main thread alive
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
