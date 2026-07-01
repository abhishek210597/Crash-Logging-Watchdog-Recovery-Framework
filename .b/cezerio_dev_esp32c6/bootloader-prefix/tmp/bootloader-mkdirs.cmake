# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/DELL/.platformio/packages/framework-espidf/components/bootloader/subproject")
  file(MAKE_DIRECTORY "C:/Users/DELL/.platformio/packages/framework-espidf/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "C:/Users/DELL/Desktop/rutvik/PROJECT_CV/RTOS_Based_Sensor_Data_Logger_with_CLI/Crash_Logging_Watchdoc_Recovery_Framework/.b/cezerio_dev_esp32c6/bootloader"
  "C:/Users/DELL/Desktop/rutvik/PROJECT_CV/RTOS_Based_Sensor_Data_Logger_with_CLI/Crash_Logging_Watchdoc_Recovery_Framework/.b/cezerio_dev_esp32c6/bootloader-prefix"
  "C:/Users/DELL/Desktop/rutvik/PROJECT_CV/RTOS_Based_Sensor_Data_Logger_with_CLI/Crash_Logging_Watchdoc_Recovery_Framework/.b/cezerio_dev_esp32c6/bootloader-prefix/tmp"
  "C:/Users/DELL/Desktop/rutvik/PROJECT_CV/RTOS_Based_Sensor_Data_Logger_with_CLI/Crash_Logging_Watchdoc_Recovery_Framework/.b/cezerio_dev_esp32c6/bootloader-prefix/src/bootloader-stamp"
  "C:/Users/DELL/Desktop/rutvik/PROJECT_CV/RTOS_Based_Sensor_Data_Logger_with_CLI/Crash_Logging_Watchdoc_Recovery_Framework/.b/cezerio_dev_esp32c6/bootloader-prefix/src"
  "C:/Users/DELL/Desktop/rutvik/PROJECT_CV/RTOS_Based_Sensor_Data_Logger_with_CLI/Crash_Logging_Watchdoc_Recovery_Framework/.b/cezerio_dev_esp32c6/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/DELL/Desktop/rutvik/PROJECT_CV/RTOS_Based_Sensor_Data_Logger_with_CLI/Crash_Logging_Watchdoc_Recovery_Framework/.b/cezerio_dev_esp32c6/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/DELL/Desktop/rutvik/PROJECT_CV/RTOS_Based_Sensor_Data_Logger_with_CLI/Crash_Logging_Watchdoc_Recovery_Framework/.b/cezerio_dev_esp32c6/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
