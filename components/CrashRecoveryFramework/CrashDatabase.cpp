#include "CrashRecoveryFramework.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include "cJSON.h"

namespace CRF {

// We extend CrashManager in this compilation unit to link the CrashDatabase functionality
// and read/write the JSON files to SPIFFS.

static const char* DB_DIR = "/spiffs";

// Serializes a CrashSignature to JSON string
static std::string serializeReportToJson(const CrashSignature& report) {
    cJSON* root = cJSON_CreateObject();
    if (!root) return "";

    cJSON_AddStringToObject(root, "uuid", report.uuid);
    cJSON_AddNumberToObject(root, "timestamp_us", report.timestamp_us);
    cJSON_AddNumberToObject(root, "reset_reason", report.reset_reason);
    cJSON_AddStringToObject(root, "task_name", report.task_name);
    cJSON_AddNumberToObject(root, "fault_address", report.fault_address);
    cJSON_AddStringToObject(root, "assert_file", report.assert_file);
    cJSON_AddNumberToObject(root, "assert_line", report.assert_line);
    cJSON_AddStringToObject(root, "assert_expression", report.assert_expression);

    // Call stack PCs
    cJSON* call_stack = cJSON_CreateArray();
    for (uint32_t i = 0; i < report.call_stack_depth; ++i) {
        cJSON_AddItemToArray(call_stack, cJSON_CreateNumber(report.call_stack[i]));
    }
    cJSON_AddItemToObject(root, "call_stack", call_stack);

    // Registers
    cJSON* registers = cJSON_CreateArray();
    for (int i = 0; i < 16; ++i) {
        cJSON_AddItemToArray(registers, cJSON_CreateNumber(report.registers[i]));
    }
    cJSON_AddItemToObject(root, "registers", registers);

    char* rendered = cJSON_PrintUnformatted(root);
    std::string json_str = rendered ? rendered : "";
    
    cJSON_free(rendered);
    cJSON_Delete(root);
    return json_str;
}

// Parses JSON string back to CrashSignature
static bool deserializeJsonToReport(const std::string& json_str, CrashSignature& report) {
    std::memset(&report, 0, sizeof(CrashSignature));
    
    cJSON* root = cJSON_Parse(json_str.c_str());
    if (!root) return false;

    cJSON* item = cJSON_GetObjectItem(root, "uuid");
    if (item && cJSON_IsString(item)) std::strncpy(report.uuid, item->valuestring, sizeof(report.uuid) - 1);

    item = cJSON_GetObjectItem(root, "timestamp_us");
    if (item && cJSON_IsNumber(item)) report.timestamp_us = item->valuedouble;

    item = cJSON_GetObjectItem(root, "reset_reason");
    if (item && cJSON_IsNumber(item)) report.reset_reason = item->valueint;

    item = cJSON_GetObjectItem(root, "task_name");
    if (item && cJSON_IsString(item)) std::strncpy(report.task_name, item->valuestring, sizeof(report.task_name) - 1);

    item = cJSON_GetObjectItem(root, "fault_address");
    if (item && cJSON_IsNumber(item)) report.fault_address = item->valueint;

    item = cJSON_GetObjectItem(root, "assert_file");
    if (item && cJSON_IsString(item)) std::strncpy(report.assert_file, item->valuestring, sizeof(report.assert_file) - 1);

    item = cJSON_GetObjectItem(root, "assert_line");
    if (item && cJSON_IsNumber(item)) report.assert_line = item->valueint;

    item = cJSON_GetObjectItem(root, "assert_expression");
    if (item && cJSON_IsString(item)) std::strncpy(report.assert_expression, item->valuestring, sizeof(report.assert_expression) - 1);

    item = cJSON_GetObjectItem(root, "call_stack");
    if (item && cJSON_IsArray(item)) {
        int size = cJSON_GetArraySize(item);
        report.call_stack_depth = std::min(size, 16);
        for (int i = 0; i < (int)report.call_stack_depth; ++i) {
            cJSON* val = cJSON_GetArrayItem(item, i);
            if (val) report.call_stack[i] = val->valueint;
        }
    }

    item = cJSON_GetObjectItem(root, "registers");
    if (item && cJSON_IsArray(item)) {
        int size = cJSON_GetArraySize(item);
        for (int i = 0; i < std::min(size, 16); ++i) {
            cJSON* val = cJSON_GetArrayItem(item, i);
            if (val) report.registers[i] = val->valueint;
        }
    }

    cJSON_Delete(root);
    report.magic = 0xDEADBEEF;
    return true;
}

// Scans SPIFFS partition for crash JSON archives and loads them
void CrashManager::loadCrashHistory() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_history.clear();

    DIR* dir = opendir(DB_DIR);
    if (!dir) return;

    struct dirent* entry;
    std::vector<std::string> crash_files;

    while ((entry = readdir(dir)) != nullptr) {
        if (std::strncmp(entry->d_name, "crash_", 6) == 0 && std::strstr(entry->d_name, ".json") != nullptr) {
            crash_files.push_back(std::string(DB_DIR) + "/" + entry->d_name);
        }
    }
    closedir(dir);

    // Limit to last 20 reports. Sort by filename (timestamp embedded or alphabetical)
    std::sort(crash_files.begin(), crash_files.end());

    for (const auto& filepath : crash_files) {
        FILE* f = fopen(filepath.c_str(), "r");
        if (f) {
            std::fseek(f, 0, SEEK_END);
            size_t size = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);

            std::vector<char> buf(size + 1);
            size_t read_bytes = std::fread(buf.data(), 1, size, f);
            buf[read_bytes] = '\0';
            std::fclose(f);

            CrashSignature report;
            if (deserializeJsonToReport(buf.data(), report)) {
                m_history.push_back(report);
            }
        }
    }
    
    CRF_LOG_INFO("Loaded %u crash reports from SPIFFS.", m_history.size());
}

bool CrashManager::saveCrashReport(const CrashSignature& report) {
    // Generate JSON string
    std::string json_str = serializeReportToJson(report);
    if (json_str.empty()) return false;

    // Purge old files if list exceeds limit
    DIR* dir = opendir(DB_DIR);
    if (dir) {
        struct dirent* entry;
        std::vector<std::string> crash_files;
        while ((entry = readdir(dir)) != nullptr) {
            if (std::strncmp(entry->d_name, "crash_", 6) == 0 && std::strstr(entry->d_name, ".json") != nullptr) {
                crash_files.push_back(std::string(DB_DIR) + "/" + entry->d_name);
            }
        }
        closedir(dir);

        // Keep database files count below 10 to protect limited SPIFFS storage space
        if (crash_files.size() >= 10) {
            std::sort(crash_files.begin(), crash_files.end());
            // Delete oldest reports
            size_t to_delete = crash_files.size() - 9;
            for (size_t i = 0; i < to_delete; ++i) {
                std::remove(crash_files[i].c_str());
            }
        }
    }

    // Write new report
    char filepath[64];
    std::snprintf(filepath, sizeof(filepath), "%s/crash_%012llx.json", DB_DIR, report.timestamp_us);
    
    FILE* f = fopen(filepath, "w");
    if (f) {
        std::fputs(json_str.c_str(), f);
        std::fclose(f);
        CRF_LOG_INFO("Crash report saved to persistent storage: %s", filepath);
        
        // Push alert notification
        NotificationSystem::getInstance().sendCrashAlert(report);
        
        return true;
    }
    
    CRF_LOG_ERR("Failed to open file to save crash report: %s", filepath);
    return false;
}

void CrashManager::clearCrashHistory() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_history.clear();

    DIR* dir = opendir(DB_DIR);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (std::strncmp(entry->d_name, "crash_", 6) == 0 && std::strstr(entry->d_name, ".json") != nullptr) {
                std::string path = std::string(DB_DIR) + "/" + entry->d_name;
                std::remove(path.c_str());
            }
        }
        closedir(dir);
        CRF_LOG_INFO("Cleared all crash reports from SPIFFS.");
    }
}

} // namespace CRF
