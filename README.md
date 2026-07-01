# Crash Logging, Watchdog & Recovery Framework for ESP32-C6

A C++20 post-mortem crash diagnostics, supervisor watchdog, and fail-safe recovery framework for ESP32-C6 microcontrollers built on **PlatformIO** using the native **ESP-IDF** framework.

This project implements a complete, self-healing system recovery suite designed for high-reliability embedded nodes. It ensures system resilience against firmware crashes, memory corruption, and frozen tasks through automated recovery mechanisms, glassmorphic diagnostic web portals, real-time Slack/Discord webhooks, and an interactive UART console.

---

## 🌟 Key Features

*   **💾 Post-Mortem Crash Signatures:** Captures cpu registers, program counter (PC), reset reason, stack pointer (SP), and crash timestamp in non-volatile `RTC_NOINIT_ATTR` memory, surviving soft resets.
*   **🔍 RISC-V Stack Trace Generator:** A custom assembly-level frame pointer walker that unwinds the RISC-V stack to record call-stack program counter (PC) lists for pinpointing assertion failures and panics.
*   **📂 Async Structured Logger:** Thread-safe logger utilizing FreeRTOS queues. Implements log level filtering, SPIFFS file storage, log rotation (preventing flash exhaustion), and regex-based credential masking.
*   **🐕 FreeRTOS Watchdog Manager:** Software watchdog thread supervising custom-registered tasks. If a task fails to feed its heartbeat within a target interval, the watchdog collects telemetry and triggers a controlled reset.
*   **🛡️ Boot Loop Protection & Safe Mode:** Monitors reset frequencies using NVS boot counters. If 3 consecutive crashes occur within a 20-second window, the system falls back to a minimal "Safe Mode" to prevent infinite loops.
*   **🖥️ Glassmorphic Web Dashboard:** Embedded Web Server presenting real-time hardware metrics (heap, task CPU allocation, internal chip temperature) and diagnostic logs in a sleek dark-mode user interface.
*   **🐚 UART Console CLI:** Rich console registered using `esp_console` exposing interactive commands (`status`, `logs`, `crashinfo`, `inject_fault`) to easily diagnose runtime behavior.
*   **🔔 Discord/Slack Webhooks:** Event-driven notification agent dispatching JSON payloads containing exact crash signatures and watchdog alerts over Wi-Fi.

---

## 📁 Repository Structure

```text
Crash_Logging_Watchdog_Recovery_Framework/
├── components/
│   ├── cjson/                          # Standard cJSON JSON parser component
│   └── CrashRecoveryFramework/         # Unified Framework Component
│       ├── include/
│       │   └── CrashRecoveryFramework.hpp  # Declarations for all modules
│       ├── CLI.cpp                     # esp_console UART shell commands
│       ├── CrashDatabase.cpp           # PERSISTENT SPIFFS database archiving
│       ├── CrashDetection.cpp          # System panic override & __assert_func wrap
│       ├── Dashboard.cpp               # Glassmorphic HTTP server endpoints
│       ├── Logger.cpp                  # Asynchronous SPIFFS logger tasks
│       ├── Notification.cpp            # Discord webhook dispatcher
│       ├── RecoveryEngine.cpp          # Boot loops counter and Safe Mode 
│       ├── ResourceMonitor.cpp         # Task CPU, memory & temp diagnostics
│       ├── StateCheckpoint.cpp        # Checkpointing state to NVS
│       └── Watchdog.cpp                # Supervisor watchdog thread
├── include/
│   └── sdkconfig.h                     # Merged ESP-IDF configuration definitions
├── src/
│   └── main.cpp                        # Orchestrates Wi-Fi AP, CLI, sensor tasks & faults
├── platformio.ini                      # PlatformIO board configuration
└── CMakeLists.txt                      # Project CMake script
```

---

## 🚀 Getting Started

### Prerequisites
*   [PlatformIO Core (CLI)](https://docs.platformio.org/page/core/index.html) or PlatformIO IDE inside VS Code.
*   An **ESP32-C6** development board connected via USB.

### Configuration
Update Wi-Fi AP settings or your notification webhook URL inside `src/main.cpp`:
```cpp
// Notification URL for Slack/Discord webhook alerts
CRF::NotificationSystem::getInstance().init("https://discord.com/api/webhooks/your-id");
```

### Compiling and Flashing
To build and flash the firmware to your ESP32-C6:
```bash
# Compile project
pio run

# Flash to device
pio run --target upload

# Open UART serial console monitor
pio device monitor
```

---

## 🐚 CLI Command Console

Connect to your ESP32-C6 over serial monitor (`115200` baud) to interact with the console shell:

*   `status`: Displays current system status, task list, CPU usage, free heap, and internal temperature.
*   `crashinfo`: Reads the last recorded post-mortem crash signature and stack trace.
*   `logs`: Lists or prints the active crash log files from SPIFFS flash.
*   `inject_fault <id>`: Proactively triggers test faults:
    *   `1` — Null Pointer Dereference (CPU Exception)
    *   `2` — Division by Zero (Arithmetic Fault)
    *   `3` — Stack Overflow (Stack canary corruption)
    *   `4` — Assertion Failure (`assert(2 + 2 == 5)`)
    *   `5` — Memory Corruption (Overwriting heap allocation header)
    *   `6` — Watchdog Timeout (Blocking loop in a registered task)

---

## 🛠️ Testing Verification Flow

1.  **Boot & AP Init:** On startup, the board initializes SPIFFS and NVS, starts a Wi-Fi Access Point named `FaultShield_Recovery` (Password: `recovery123`), and launches the web dashboard.
2.  **Diagnostics Web Interface:** Connect your phone/PC to the AP and open `http://192.168.4.1/` to view the dark-mode glassmorphic telemetry dashboard.
3.  **Assertion/Panic Test:** Run `inject_fault 4` in the CLI. The system will write the crash signature to RTC memory, send a notification over Wi-Fi, print the call stack backtrace to serial, and restart.
4.  **Crash Post-Mortem:** After boot, run `crashinfo` in the console or visit the dashboard to view the preserved crash record.
5.  **Safe Mode Trigger:** Run `inject_fault 4` three times in quick succession (under 20 seconds). The recovery engine will identify a boot loop and start the device in **Safe Mode** with minimal services to allow recovery.

---

## ⚖️ License

Distributed under the Apache 3.0 License. See `LICENSE` for details.
