# CMakeLists.txt to register CrashRecoveryFramework as a first-class ESP-IDF component
idf_component_register(SRCS "CrashDetection.cpp"
                            "StackTrace.cpp"
                            "Logger.cpp"
                            "Watchdog.cpp"
                            "RecoveryEngine.cpp"
                            "ResourceMonitor.cpp"
                            "StateCheckpoint.cpp"
                            "CrashDatabase.cpp"
                            "Notification.cpp"
                            "Dashboard.cpp"
                            "CLI.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES cjson esp_http_server nvs_flash console esp_wifi esp_event esp_timer app_update spiffs esp_driver_tsens esp_http_client)

target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=__assert_func")
