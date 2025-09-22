//
// Essential Hardware Configuration for Tank Alarm 092025
// 
// This file contains only the essential hardware-specific settings that must be
// compiled into the firmware. All operational settings should be configured
// in tank_config.txt on the SD card for maximum field flexibility.
//

#ifndef CONFIG_H
#define CONFIG_H

// === ESSENTIAL HARDWARE SETTINGS (CANNOT BE CHANGED AT RUNTIME) ===

// Sensor Configuration - Hardware dependent
#define SENSOR_TYPE 0                   // 0=DIGITAL_FLOAT, 1=ANALOG_VOLTAGE, 2=CURRENT_LOOP

// Pin Configuration - Hardware dependent  
#define TANK_LEVEL_PIN 7               // Digital input for float switch (pins 5,6,7 available on MKR RELAY)
#define ANALOG_SENSOR_PIN A1           // Analog input pin (A1, A2, A3, A4 available on MKR RELAY shield)
#define RELAY_PIN 5                    // Relay output pin (change as needed)

// I2C Configuration - Hardware dependent
#define I2C_CURRENT_LOOP_ADDRESS 0x48  // I2C address for current loop ADC
#define CURRENT_LOOP_CHANNEL 0         // ADC channel for current loop input

// Debug Configuration - Compile-time only
#define ENABLE_SERIAL_DEBUG true       // Enable serial output for debugging

// SD Card File Names - System level settings
#define SD_CONFIG_FILE "tank_config.txt"      // SD card configuration file
#define SD_HOURLY_LOG_FILE "hourly_log.txt"   // Hourly data log file
#define SD_DAILY_LOG_FILE "daily_log.txt"     // Daily report log file
#define SD_ALARM_LOG_FILE "alarm_log.txt"     // Alarm event log file
#define SD_DECREASE_LOG_FILE "decrease_log.txt" // Large decrease log file
#define SD_REPORT_LOG_FILE "report_log.txt"     // Daily report transmission log file

// === FALLBACK DEFAULTS (USED ONLY IF SD CARD CONFIG IS MISSING) ===
// All operational settings should be configured in tank_config.txt
// These provide minimal defaults for emergency operation only

// Minimal Hologram Configuration
#define HOLOGRAM_DEVICE_KEY "your_device_key_here"  // Replace with your actual device key
#define SERVER_DEVICE_KEY "server_device_key_here"  // Server's Hologram device ID for remote commands
#define HOLOGRAM_APN "hologram"                     // Hologram.io APN (usually stays "hologram")

// Emergency Contact (configure proper numbers in tank_config.txt)
#define ALARM_PHONE_PRIMARY "+12223334444"     // Emergency contact - UPDATE IN tank_config.txt
#define ALARM_PHONE_SECONDARY "+15556667777"   // Emergency contact - UPDATE IN tank_config.txt
#define DAILY_REPORT_PHONE "+18889990000"      // Report recipient - UPDATE IN tank_config.txt

// Basic operational defaults
#define SLEEP_INTERVAL_HOURS 1          // Basic sleep interval
#define DAILY_REPORT_HOURS 24           // Basic report interval  
#define DAILY_REPORT_TIME "05:00"       // Basic report time
#define CONNECTION_TIMEOUT_MS 30000     // Basic network timeout
#define SMS_RETRY_ATTEMPTS 3            // Basic retry attempts
#define TANK_NUMBER 1                   // Basic tank identifier
#define SITE_LOCATION_NAME "Tank Site"  // Basic site name
#define TANK_HEIGHT_INCHES 120          // Basic tank height
#define INCHES_PER_UNIT 1.0             // Basic calibration
#define HIGH_ALARM_INCHES 100           // Basic high alarm
#define LOW_ALARM_INCHES 12             // Basic low alarm
#define DIGITAL_HIGH_ALARM true         // Basic digital alarm settings
#define DIGITAL_LOW_ALARM false
#define LARGE_DECREASE_THRESHOLD_INCHES 24  // Basic decrease detection
#define LARGE_DECREASE_WAIT_HOURS 2
#define ENABLE_LOW_POWER_MODE true      // Basic power management
#define WAKE_CHECK_DURATION_MS 5000
#define SHORT_SLEEP_MINUTES 10
#define NORMAL_SLEEP_HOURS 1
#define ENABLE_WAKE_ON_PING true
#define DEEP_SLEEP_MODE false

#endif // CONFIG_H