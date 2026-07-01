#include "CrashRecoveryFramework.hpp"
#include <cstdio>
#include <cstring>
#include <sstream>
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "cJSON.h"

namespace CRF {

// Embed the beautiful, modern dark mode HTML dashboard using C++ raw string literal
static const char* DASHBOARD_HTML = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>FaultShield - Recovery & Diagnostic Dashboard</title>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #0b0f19;
            --card-bg: rgba(17, 24, 39, 0.7);
            --card-border: rgba(255, 255, 255, 0.08);
            --primary: #4facfe;
            --primary-glow: #00f2fe;
            --text-color: #f3f4f6;
            --text-muted: #9ca3af;
            --danger: #ef4444;
            --danger-glow: #f87171;
            --success: #10b981;
            --warning: #f59e0b;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: 'Inter', sans-serif;
            background-color: var(--bg-color);
            color: var(--text-color);
            min-height: 100vh;
            padding: 2rem 1rem;
            background-image: 
                radial-gradient(at 10% 20%, rgba(79, 172, 254, 0.08) 0px, transparent 50%),
                radial-gradient(at 90% 80%, rgba(0, 242, 254, 0.05) 0px, transparent 50%);
        }
        .container { max-width: 1200px; margin: 0 auto; display: flex; flex-direction: column; gap: 2rem; }
        
        /* Header section */
        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid var(--card-border);
            padding-bottom: 1.5rem;
        }
        .logo {
            display: flex;
            align-items: center;
            gap: 0.75rem;
            font-size: 1.5rem;
            font-weight: 700;
            background: linear-gradient(135deg, var(--primary), var(--primary-glow));
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        .badge {
            background-color: rgba(16, 185, 129, 0.15);
            color: var(--success);
            padding: 0.25rem 0.75rem;
            border-radius: 9999px;
            font-size: 0.85rem;
            font-weight: 600;
            border: 1px solid rgba(16, 185, 129, 0.3);
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }
        .badge.safe-mode {
            background-color: rgba(239, 68, 68, 0.15);
            color: var(--danger);
            border-color: rgba(239, 68, 68, 0.3);
            animation: pulse 2s infinite;
        }
        @keyframes pulse {
            0% { opacity: 0.6; }
            50% { opacity: 1; }
            100% { opacity: 0.6; }
        }

        /* Grid stats layout */
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
            gap: 1.5rem;
        }
        .card {
            background: var(--card-bg);
            backdrop-filter: blur(12px);
            border: 1px solid var(--card-border);
            border-radius: 16px;
            padding: 1.5rem;
            box-shadow: 0 10px 30px rgba(0,0,0,0.25);
            transition: transform 0.3s ease, border-color 0.3s ease;
        }
        .card:hover {
            transform: translateY(-4px);
            border-color: rgba(79, 172, 254, 0.3);
        }
        .card-title {
            font-size: 0.85rem;
            text-transform: uppercase;
            letter-spacing: 0.05em;
            color: var(--text-muted);
            margin-bottom: 0.5rem;
        }
        .card-value {
            font-size: 1.75rem;
            font-weight: 700;
        }
        .card-desc {
            font-size: 0.75rem;
            color: var(--text-muted);
            margin-top: 0.25rem;
        }

        /* Main Workspace layout */
        .workspace {
            display: grid;
            grid-template-columns: 2fr 1fr;
            gap: 1.5rem;
        }
        @media(max-width: 900px) {
            .workspace { grid-template-columns: 1fr; }
        }
        
        .section-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 1.25rem;
        }
        .section-title {
            font-size: 1.15rem;
            font-weight: 600;
        }
        .progress-bar-container {
            width: 100%;
            background-color: rgba(255,255,255,0.05);
            height: 10px;
            border-radius: 9999px;
            overflow: hidden;
            margin-top: 0.5rem;
        }
        .progress-bar {
            height: 100%;
            background: linear-gradient(90deg, var(--primary), var(--primary-glow));
            border-radius: 9999px;
            transition: width 0.5s ease;
        }

        /* Table elements */
        table {
            width: 100%;
            border-collapse: collapse;
            margin-top: 0.5rem;
            font-size: 0.9rem;
        }
        th, td {
            text-align: left;
            padding: 0.75rem;
            border-bottom: 1px solid rgba(255,255,255,0.05);
        }
        th {
            color: var(--text-muted);
            font-weight: 500;
            text-transform: uppercase;
            font-size: 0.75rem;
            letter-spacing: 0.05em;
        }
        
        /* Logs Viewer */
        .logs-window {
            background-color: rgba(0,0,0,0.4);
            border: 1px solid var(--card-border);
            border-radius: 12px;
            padding: 1rem;
            font-family: monospace;
            font-size: 0.825rem;
            height: 250px;
            overflow-y: auto;
            white-space: pre-wrap;
            margin-bottom: 1rem;
        }
        .log-btn-group {
            display: flex;
            gap: 1rem;
        }
        .btn {
            background: linear-gradient(135deg, var(--primary), var(--primary-glow));
            border: none;
            color: white;
            padding: 0.5rem 1.25rem;
            border-radius: 8px;
            font-weight: 600;
            font-size: 0.85rem;
            cursor: pointer;
            transition: all 0.3s ease;
            box-shadow: 0 4px 12px rgba(79, 172, 254, 0.2);
        }
        .btn:hover {
            opacity: 0.9;
            transform: scale(1.02);
        }
        .btn.btn-danger {
            background: linear-gradient(135deg, var(--danger), var(--danger-glow));
            box-shadow: 0 4px 12px rgba(239, 68, 68, 0.2);
        }
        .btn.btn-secondary {
            background: rgba(255,255,255,0.08);
            border: 1px solid var(--card-border);
            color: var(--text-color);
            box-shadow: none;
        }
        .btn.btn-secondary:hover {
            background: rgba(255,255,255,0.15);
        }

        /* Danger injection center */
        .danger-grid {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 1rem;
        }
        .danger-desc {
            font-size: 0.8rem;
            color: var(--text-muted);
            margin-bottom: 1.25rem;
        }
        .pulse-red {
            width: 8px;
            height: 8px;
            background-color: var(--danger);
            border-radius: 50%;
            display: inline-block;
            box-shadow: 0 0 8px var(--danger);
        }
        .pulse-green {
            width: 8px;
            height: 8px;
            background-color: var(--success);
            border-radius: 50%;
            display: inline-block;
            box-shadow: 0 0 8px var(--success);
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <div class="logo">🛡️ FaultShield</div>
            <div id="status-badge" class="badge"><span class="pulse-green"></span> SYSTEM ACTIVE</div>
        </header>

        <!-- Stats row -->
        <div class="stats-grid">
            <div class="card">
                <div class="card-title">Uptime</div>
                <div id="stat-uptime" class="card-value">0s</div>
                <div class="card-desc">Continuous operation</div>
            </div>
            <div class="card">
                <div class="card-title">RAM Utilization</div>
                <div id="stat-heap" class="card-value">0 KB</div>
                <div class="progress-bar-container">
                    <div id="heap-bar" class="progress-bar" style="width: 0%"></div>
                </div>
                <div id="stat-heap-desc" class="card-desc">0 KB free</div>
            </div>
            <div class="card">
                <div class="card-title">Core Temp</div>
                <div id="stat-temp" class="card-value">0.0 °C</div>
                <div class="card-desc">Internal ESP32-C6 sensor</div>
            </div>
            <div class="card">
                <div class="card-title">Crash/Recovery Stats</div>
                <div id="stat-crashes" class="card-value">0 / 0</div>
                <div class="card-desc">Reboots vs software crashes</div>
            </div>
        </div>

        <!-- Main workspace -->
        <div class="workspace">
            <!-- Left pane: Tasks and System Logs -->
            <div style="display: flex; flex-direction: column; gap: 1.5rem;">
                <!-- Tasks watch list -->
                <div class="card">
                    <div class="section-header">
                        <div class="section-title">FreeRTOS Task Watchdog</div>
                    </div>
                    <table>
                        <thead>
                            <tr>
                                <th>Task Name</th>
                                <th>Timeout Threshold</th>
                                <th>Elapsed</th>
                                <th>State</th>
                            </tr>
                        </thead>
                        <tbody id="watchdog-table-body">
                            <tr>
                                <td colspan="4" style="text-align: center; color: var(--text-muted);">No monitored tasks active.</td>
                            </tr>
                        </tbody>
                    </table>
                </div>

                <!-- Logger Section -->
                <div class="card">
                    <div class="section-header">
                        <div class="section-title">System Diagnostic Logs</div>
                        <div class="log-btn-group">
                            <button onclick="fetchLogs()" class="btn btn-secondary">Refresh</button>
                            <button onclick="clearLogs()" class="btn btn-secondary">Clear</button>
                        </div>
                    </div>
                    <div id="logs-container" class="logs-window">Loading logs...</div>
                </div>
            </div>

            <!-- Right pane: Fault Injection & Checkpointing -->
            <div style="display: flex; flex-direction: column; gap: 1.5rem;">
                <!-- Fault Injection Center -->
                <div class="card" style="border-color: rgba(239, 68, 68, 0.2);">
                    <div class="section-header">
                        <div class="section-title" style="color: var(--danger-glow);">Fault Injection Control</div>
                    </div>
                    <div class="danger-desc">Simulate production failures to validate recovery policies. Triggering causes reboot or safe-mode boot.</div>
                    <div class="danger-grid">
                        <button onclick="injectFault(1)" class="btn btn-danger">Null Pointer</button>
                        <button onclick="injectFault(2)" class="btn btn-danger">Divide by Zero</button>
                        <button onclick="injectFault(3)" class="btn btn-danger">Assert Fail</button>
                        <button onclick="injectFault(4)" class="btn btn-danger">WDT Hang</button>
                        <button onclick="injectFault(5)" class="btn btn-danger">Stack Overflow</button>
                        <button onclick="injectFault(6)" class="btn btn-danger">Mem Corrupt</button>
                    </div>
                </div>

                <!-- Checkpoints list -->
                <div class="card">
                    <div class="section-header">
                        <div class="section-title">System Recovery Reports</div>
                    </div>
                    <div id="crash-history-container" style="display: flex; flex-direction: column; gap: 1rem; font-size: 0.85rem;">
                        <div style="color: var(--text-muted); text-align: center;">No crash history saved.</div>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <script>
        function updateDashboard() {
            fetch('/api/metrics')
                .then(res => res.json())
                .then(data => {
                    document.getElementById('stat-uptime').innerText = data.uptime_s + 's';
                    
                    const freeKB = Math.round(data.free_heap / 1024);
                    const totalKB = 320; // ESP32-C6 SRAM
                    const usedKB = Math.max(0, totalKB - freeKB);
                    const pct = Math.round((usedKB / totalKB) * 100);
                    
                    document.getElementById('stat-heap').innerText = freeKB + ' KB';
                    document.getElementById('heap-bar').style.width = pct + '%';
                    document.getElementById('stat-heap-desc').innerText = pct + '% used (' + usedKB + ' KB / ' + totalKB + ' KB)';
                    
                    document.getElementById('stat-temp').innerText = data.temperature_c.toFixed(1) + ' °C';
                    document.getElementById('stat-crashes').innerText = data.recovery_count + ' / ' + data.crash_count;

                    const badge = document.getElementById('status-badge');
                    if (data.in_safe_mode) {
                        badge.className = 'badge safe-mode';
                        badge.innerHTML = '<span class="pulse-red"></span> SAFE MODE ACTIVE';
                    } else {
                        badge.className = 'badge';
                        badge.innerHTML = '<span class="pulse-green"></span> SYSTEM ACTIVE';
                    }

                    // Render watchdog tasks
                    const tbody = document.getElementById('watchdog-table-body');
                    if (data.watchdog_tasks && data.watchdog_tasks.length > 0) {
                        tbody.innerHTML = '';
                        data.watchdog_tasks.forEach(task => {
                            tbody.innerHTML += `
                                <tr>
                                    <td><code>${task.name}</code></td>
                                    <td>${task.timeout_ms} ms</td>
                                    <td>${task.elapsed_ms} ms</td>
                                    <td><span style="color: ${task.active ? 'var(--success)' : 'var(--danger)'}">${task.active ? 'Checked in' : 'HUNG'}</span></td>
                                </tr>
                            `;
                        });
                    } else {
                        tbody.innerHTML = '<tr><td colspan="4" style="text-align: center; color: var(--text-muted);">No monitored tasks active.</td></tr>';
                    }
                })
                .catch(err => console.error("Error fetching metrics:", err));
        }

        function fetchLogs() {
            fetch('/api/logs')
                .then(res => res.text())
                .then(text => {
                    const el = document.getElementById('logs-container');
                    el.innerText = text ? text : "Log file is empty.";
                    el.scrollTop = el.scrollHeight; // Scroll to bottom
                });
        }

        function clearLogs() {
            fetch('/api/logs?clear=1')
                .then(() => fetchLogs());
        }

        function fetchCrashes() {
            fetch('/api/crashes')
                .then(res => res.json())
                .then(data => {
                    const container = document.getElementById('crash-history-container');
                    if (data && data.length > 0) {
                        container.innerHTML = '';
                        data.forEach(crash => {
                            container.innerHTML += `
                                <div style="background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.05); padding: 0.75rem; border-radius: 8px;">
                                    <div style="display: flex; justify-content: space-between; font-weight: 600; margin-bottom: 0.25rem;">
                                        <span style="color: var(--danger-glow)">Crash Signature</span>
                                        <span>Task: ${crash.task_name}</span>
                                    </div>
                                    <div style="font-size: 0.75rem; color: var(--text-muted);">UUID: ${crash.uuid}</div>
                                    ${crash.assert_line ? `<div style="font-size: 0.8rem; margin-top: 0.25rem; font-family: monospace;">Assert Failed: ${crash.assert_expression}<br>File: ${crash.assert_file}:${crash.assert_line}</div>` : `<div style="font-size: 0.8rem; margin-top: 0.25rem; font-family: monospace;">Fault Addr: 0x${crash.fault_address.toString(16)}</div>`}
                                </div>
                            `;
                        });
                    } else {
                        container.innerHTML = '<div style="color: var(--text-muted); text-align: center;">No crash history saved.</div>';
                    }
                });
        }

        function injectFault(id) {
            if (confirm("WARNING: Injecting this fault will intentionally crash or stall the micro-controller to test recovery rules. Are you sure?")) {
                fetch('/api/trigger_fault?id=' + id, { method: 'POST' })
                    .then(() => {
                        alert("Fault injected! The system is restarting. Refresh in a few seconds.");
                    });
            }
        }

        // Loop execution
        setInterval(updateDashboard, 2000);
        updateDashboard();
        fetchLogs();
        fetchCrashes();
    </script>
</body>
</html>
)rawhtml";

Dashboard& Dashboard::getInstance() {
    static Dashboard instance;
    return instance;
}

esp_err_t Dashboard::indexHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, DASHBOARD_HTML, HTTPD_RESP_USE_STRLEN);
}

esp_err_t Dashboard::metricsApiHandler(httpd_req_t* req) {
    SystemMetrics metrics = ResourceMonitor::getInstance().getMetrics();

    // Create cJSON response
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "uptime_s", metrics.uptime_s);
    cJSON_AddNumberToObject(root, "free_heap", metrics.free_heap);
    cJSON_AddNumberToObject(root, "min_free_heap", metrics.min_free_heap);
    cJSON_AddNumberToObject(root, "cpu_usage_pct", metrics.cpu_usage_pct);
    cJSON_AddNumberToObject(root, "temperature_c", metrics.temperature_c);
    cJSON_AddNumberToObject(root, "flash_used", metrics.flash_used);
    cJSON_AddNumberToObject(root, "flash_total", metrics.flash_total);
    cJSON_AddNumberToObject(root, "crash_count", metrics.crash_count);
    cJSON_AddNumberToObject(root, "recovery_count", metrics.recovery_count);
    cJSON_AddBoolToObject(root, "in_safe_mode", metrics.in_safe_mode);

    // Watchdog tasks status
    cJSON* wdt_arr = cJSON_CreateArray();
    WatchdogManager::getInstance().getWatchdogStatus([&wdt_arr](const std::string& name, uint32_t threshold, uint32_t elapsed, bool active) {
        cJSON* task = cJSON_CreateObject();
        cJSON_AddStringToObject(task, "name", name.c_str());
        cJSON_AddNumberToObject(task, "timeout_ms", threshold);
        cJSON_AddNumberToObject(task, "elapsed_ms", elapsed);
        cJSON_AddBoolToObject(task, "active", active);
        cJSON_AddItemToArray(wdt_arr, task);
    });
    cJSON_AddItemToObject(root, "watchdog_tasks", wdt_arr);

    char* rendered = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t res = httpd_resp_sendstr(req, rendered);

    cJSON_free(rendered);
    cJSON_Delete(root);
    return res;
}

esp_err_t Dashboard::crashesApiHandler(httpd_req_t* req) {
    auto history = CrashManager::getInstance().getCrashHistory();

    cJSON* root = cJSON_CreateArray();
    for (const auto& report : history) {
        cJSON* r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "uuid", report.uuid);
        cJSON_AddNumberToObject(r, "timestamp_us", report.timestamp_us);
        cJSON_AddNumberToObject(r, "reset_reason", report.reset_reason);
        cJSON_AddStringToObject(r, "task_name", report.task_name);
        cJSON_AddNumberToObject(r, "fault_address", report.fault_address);
        cJSON_AddStringToObject(r, "assert_file", report.assert_file);
        cJSON_AddNumberToObject(r, "assert_line", report.assert_line);
        cJSON_AddStringToObject(r, "assert_expression", report.assert_expression);
        cJSON_AddItemToArray(root, r);
    }

    char* rendered = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t res = httpd_resp_sendstr(req, rendered);

    cJSON_free(rendered);
    cJSON_Delete(root);
    return res;
}

esp_err_t Dashboard::logsApiHandler(httpd_req_t* req) {
    // Check if query string includes "clear=1" to clear logs
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[32];
        if (httpd_query_key_value(query, "clear", val, sizeof(val)) == ESP_OK) {
            if (std::strcmp(val, "1") == 0) {
                Logger::getInstance().clearLogs();
                httpd_resp_sendstr(req, "Logs cleared.");
                return ESP_OK;
            }
        }
    }

    // Set chunked header response
    httpd_resp_set_type(req, "text/plain");
    
    // Dump logs directly from file via chunked response stream
    struct ChunkSender {
        httpd_req_t* request;
        static void send(const std::string& line, ChunkSender* self) {
            httpd_resp_send_chunk(self->request, line.c_str(), line.length());
        }
    } sender;
    
    sender.request = req;
    Logger::getInstance().dumpLogsToStream([&sender](const std::string& line) {
        ChunkSender::send(line, &sender);
    });

    // Send empty chunk to terminate stream
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t Dashboard::triggerFaultApiHandler(httpd_req_t* req) {
    char query[64];
    int fault_id = 0;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[32];
        if (httpd_query_key_value(query, "id", val, sizeof(val)) == ESP_OK) {
            fault_id = std::atoi(val);
        }
    }

    httpd_resp_sendstr(req, "Fault triggered. Restarting...");
    
    // Brief delay to allow HTTP response transmission
    vTaskDelay(pdMS_TO_TICKS(500));

    switch (fault_id) {
        case 1: CrashManager::getInstance().injectNullPointerCrash(); break;
        case 2: CrashManager::getInstance().injectDivideByZero(); break;
        case 3: CrashManager::getInstance().injectAssertFailure(); break;
        case 4: CrashManager::getInstance().injectWatchdogTimeout(); break;
        case 5: CrashManager::getInstance().injectStackOverflow(32); break;
        case 6: CrashManager::getInstance().injectMemoryCorruption(); break;
        default: break;
    }

    return ESP_OK;
}

esp_err_t Dashboard::start(uint16_t port) {
    m_port = port;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = 8;
    config.ctrl_port = 32768; // Unique control port for ESP32-C6

    CRF_LOG_INFO("Starting Dashboard HTTP Server on port %u...", port);
    esp_err_t err = httpd_start(&m_server_handle, &config);
    if (err == ESP_OK) {
        // Register URI endpoints
        httpd_uri_t index_uri = { "/", HTTP_GET, indexHandler, nullptr };
        httpd_register_uri_handler(m_server_handle, &index_uri);

        httpd_uri_t metrics_uri = { "/api/metrics", HTTP_GET, metricsApiHandler, nullptr };
        httpd_register_uri_handler(m_server_handle, &metrics_uri);

        httpd_uri_t crashes_uri = { "/api/crashes", HTTP_GET, crashesApiHandler, nullptr };
        httpd_register_uri_handler(m_server_handle, &crashes_uri);

        httpd_uri_t logs_uri = { "/api/logs", HTTP_GET, logsApiHandler, nullptr };
        httpd_register_uri_handler(m_server_handle, &logs_uri);

        httpd_uri_t trigger_uri = { "/api/trigger_fault", HTTP_POST, triggerFaultApiHandler, nullptr };
        httpd_register_uri_handler(m_server_handle, &trigger_uri);
    }
    return err;
}

void Dashboard::stop() {
    if (m_server_handle) {
        httpd_stop(m_server_handle);
        m_server_handle = nullptr;
        CRF_LOG_INFO("Dashboard HTTP Server stopped.");
    }
}

} // namespace CRF
