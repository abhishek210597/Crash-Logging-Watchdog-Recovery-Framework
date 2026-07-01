#include "CrashRecoveryFramework.hpp"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <algorithm>
#include <chrono>
#include "esp_log.h"
#include "esp_timer.h"

namespace CRF {

struct LogQueueMessage {
    LogLevel level;
    char file[32];
    int line;
    char func[32];
    char* message; // Dynamically allocated, must be freed by receiver
};

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::init(const std::string& log_file_path, size_t max_file_size, uint8_t max_rotations) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_log_path = log_file_path;
    m_max_file_size = max_file_size;
    m_max_rotations = max_rotations;

    // Create the FreeRTOS queue for async logging if it doesn't exist
    if (m_queue_handle == nullptr) {
        m_queue_handle = xQueueCreate(100, sizeof(LogQueueMessage));
    }

    // Launch background write task if not already running
    if (m_task_handle == nullptr && m_queue_handle != nullptr) {
        xTaskCreatePinnedToCore(
            Logger::asyncWriteTask,
            "CRF_LoggerTask",
            4096,
            this,
            2, // Priority
            &m_task_handle,
            1  // CPU Core
        );
    }
    
    m_file_enabled = true;
    CRF_LOG_INFO("Logger initialized. Output file: %s", log_file_path.c_str());
}

void Logger::setLogRotation(size_t max_size, uint8_t max_rotations) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_max_file_size = max_size;
    m_max_rotations = max_rotations;
}

void Logger::setRedactionWords(const std::vector<std::string>& words) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_redact_words = words;
}

std::string Logger::redactMessage(const std::string& msg) {
    if (m_redact_words.empty()) return msg;

    std::string redacted = msg;
    for (const auto& word : m_redact_words) {
        if (word.empty()) continue;
        size_t pos = 0;
        while ((pos = redacted.find(word, pos)) != std::string::npos) {
            redacted.replace(pos, word.length(), "[REDACTED]");
            pos += 10; // length of "[REDACTED]"
        }
    }
    return redacted;
}

std::string Logger::formatLog(LogLevel level, const char* file, int line, const char* func, const std::string& message) {
    const char* lvl_str = "INFO";
    switch (level) {
        case LogLevel::DEBUG:    lvl_str = "DEBUG"; break;
        case LogLevel::INFO:     lvl_str = "INFO"; break;
        case LogLevel::WARNING:  lvl_str = "WARN"; break;
        case LogLevel::ERROR:    lvl_str = "ERROR"; break;
        case LogLevel::CRITICAL: lvl_str = "CRIT"; break;
        case LogLevel::FATAL:    lvl_str = "FATAL"; break;
    }

    // Get time in seconds and milliseconds
    uint64_t now_us = esp_timer_get_time();
    uint32_t sec = now_us / 1000000;
    uint32_t ms = (now_us % 1000000) / 1000;

    // Get thread/task name
    const char* t_name = "ISR/Unknown";
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    if (current_task != nullptr) {
        t_name = pcTaskGetName(current_task);
    }

    // Extract file basename
    const char* base_file = strrchr(file, '/');
    if (!base_file) base_file = strrchr(file, '\\');
    base_file = base_file ? base_file + 1 : file;

    char header[128];
    std::snprintf(header, sizeof(header), "[%5lu.%03lu] [%s] [%s] (%s:%d): ", 
                 (unsigned long)sec, (unsigned long)ms, lvl_str, t_name, base_file, line);
    
    return std::string(header) + message + "\n";
}

void Logger::log(LogLevel level, const char* file, int line, const char* func, const char* format, ...) {
    char raw_buf[256];
    va_list args;
    va_start(args, format);
    int len = std::vsnprintf(raw_buf, sizeof(raw_buf), format, args);
    va_end(args);

    std::string msg;
    if (len >= (int)sizeof(raw_buf)) {
        // Truncated, allocate dynamic buffer
        std::vector<char> dyn_buf(len + 1);
        va_start(args, format);
        std::vsnprintf(dyn_buf.data(), dyn_buf.size(), format, args);
        va_end(args);
        msg = dyn_buf.data();
    } else {
        msg = raw_buf;
    }

    // Redact sensitive patterns
    std::string redacted_msg = redactMessage(msg);

    // Format the final log string
    std::string formatted = formatLog(level, file, line, func, redacted_msg);

    // If console is enabled, print directly to avoid delays or queue issues on panic
    if (m_console_enabled) {
        // ESP-IDF standard printf or direct console out
        std::printf("%s", formatted.c_str());
    }

    // If async file queue is enabled, push it to the background task
    if (m_file_enabled && m_queue_handle != nullptr) {
        LogQueueMessage q_msg;
        q_msg.level = level;
        
        const char* base_file = strrchr(file, '/');
        if (!base_file) base_file = strrchr(file, '\\');
        base_file = base_file ? base_file + 1 : file;
        std::strncpy(q_msg.file, base_file, sizeof(q_msg.file) - 1);
        q_msg.file[sizeof(q_msg.file) - 1] = '\0';

        std::strncpy(q_msg.func, func, sizeof(q_msg.func) - 1);
        q_msg.func[sizeof(q_msg.func) - 1] = '\0';

        q_msg.line = line;
        
        // Allocate heap copy of formatted log message
        q_msg.message = strdup(formatted.c_str());
        
        if (xQueueSend(m_queue_handle, &q_msg, 0) != pdPASS) {
            // Queue full, free message to avoid leak
            free(q_msg.message);
        }
    }
}

void Logger::asyncWriteTask(void* pvParameters) {
    Logger* self = static_cast<Logger*>(pvParameters);
    LogQueueMessage q_msg;

    while (true) {
        if (xQueueReceive(self->m_queue_handle, &q_msg, portMAX_DELAY) == pdPASS) {
            if (q_msg.message != nullptr) {
                // Open file, write, close. In real embedded systems, keeping it open
                // or syncing periodically is a trade-off. We'll open in append mode.
                FILE* f = std::fopen(self->m_log_path.c_str(), "a");
                if (f != nullptr) {
                    std::fputs(q_msg.message, f);
                    std::fclose(f);

                    // Check file size for rotation
                    FILE* f_chk = std::fopen(self->m_log_path.c_str(), "r");
                    if (f_chk != nullptr) {
                        std::fseek(f_chk, 0, SEEK_END);
                        size_t size = std::ftell(f_chk);
                        std::fclose(f_chk);

                        if (size >= self->m_max_file_size) {
                            // Run rotation logic
                            for (int i = self->m_max_rotations - 1; i >= 0; --i) {
                                std::string old_name = self->m_log_path + "." + std::to_string(i);
                                std::string new_name = self->m_log_path + "." + std::to_string(i + 1);
                                if (i == 0) {
                                    old_name = self->m_log_path;
                                }
                                
                                std::remove(new_name.c_str()); // delete target if exists
                                std::rename(old_name.c_str(), new_name.c_str());
                            }
                        }
                    }
                }
                
                // Free heap copy
                free(q_msg.message);
            }
        }
    }
}

void Logger::dumpLogsToStream(std::function<void(const std::string&)> out_fn) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Dump rotations first (oldest to newest)
    for (int i = m_max_rotations; i > 0; --i) {
        std::string rot_path = m_log_path + "." + std::to_string(i);
        FILE* f = std::fopen(rot_path.c_str(), "r");
        if (f != nullptr) {
            char buffer[128];
            while (std::fgets(buffer, sizeof(buffer), f) != nullptr) {
                out_fn(buffer);
            }
            std::fclose(f);
        }
    }

    // Dump current log file
    FILE* f = std::fopen(m_log_path.c_str(), "r");
    if (f != nullptr) {
        char buffer[128];
        while (std::fgets(buffer, sizeof(buffer), f) != nullptr) {
            out_fn(buffer);
        }
        std::fclose(f);
    }
}

void Logger::clearLogs() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::remove(m_log_path.c_str());
    for (int i = 0; i <= m_max_rotations; ++i) {
        std::string rot_path = m_log_path + "." + std::to_string(i);
        std::remove(rot_path.c_str());
    }
}

} // namespace CRF
