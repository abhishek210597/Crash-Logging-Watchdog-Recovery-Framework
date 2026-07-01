#include "CrashRecoveryFramework.hpp"
#include <cstdio>
#include <vector>
#include "nvs_flash.h"
#include "nvs.h"

namespace CRF {

StateCheckpoint& StateCheckpoint::getInstance() {
    static StateCheckpoint instance;
    return instance;
}

void StateCheckpoint::init() {
    // NVS is already initialized by RecoveryEngine::init()
    CRF_LOG_INFO("State Checkpoint system initialized.");
}

bool StateCheckpoint::saveCheckpoint(const std::string& key, const std::string& value) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("crf_checkpoint", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs_handle, key.c_str(), value.c_str());
        if (err == ESP_OK) {
            nvs_commit(nvs_handle);
        }
        nvs_close(nvs_handle);
        return (err == ESP_OK);
    }
    return false;
}

bool StateCheckpoint::loadCheckpoint(const std::string& key, std::string& value) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("crf_checkpoint", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        size_t required_size = 0;
        err = nvs_get_str(nvs_handle, key.c_str(), nullptr, &required_size);
        if (err == ESP_OK && required_size > 0) {
            std::vector<char> buf(required_size);
            err = nvs_get_str(nvs_handle, key.c_str(), buf.data(), &required_size);
            if (err == ESP_OK) {
                value = buf.data();
            }
        }
        nvs_close(nvs_handle);
        return (err == ESP_OK);
    }
    return false;
}

bool StateCheckpoint::saveCheckpointBinary(const std::string& key, const void* data, size_t size) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("crf_checkpoint", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs_handle, key.c_str(), data, size);
        if (err == ESP_OK) {
            nvs_commit(nvs_handle);
        }
        nvs_close(nvs_handle);
        return (err == ESP_OK);
    }
    return false;
}

bool StateCheckpoint::loadCheckpointBinary(const std::string& key, void* out_data, size_t size) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("crf_checkpoint", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        size_t required_size = size;
        err = nvs_get_blob(nvs_handle, key.c_str(), out_data, &required_size);
        nvs_close(nvs_handle);
        return (err == ESP_OK && required_size == size);
    }
    return false;
}

void StateCheckpoint::clearAllCheckpoints() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("crf_checkpoint", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        CRF_LOG_INFO("All state checkpoints cleared.");
    }
}

} // namespace CRF
