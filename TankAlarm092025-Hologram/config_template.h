//
// Configuration file for Tank Alarm 092025
// Copy this file and rename to config.h, then update with your specific values
//

#ifndef CONFIG_H
#define CONFIG_H

// Hologram.io Configuration
#define HOLOGRAM_DEVICE_KEY "your_device_key_here"  // Replace with your actual device key
#define HOLOGRAM_APN "hologram"                     // Hologram.io APN (usually stays "hologram")

// Phone Numbers for Notifications (include country code, e.g., +1 for US)
#define ALARM_PHONE_PRIMARY "+12223334444"     // Primary emergency contact
#define ALARM_PHONE_SECONDARY "+15556667777"   // Secondary emergency contact  
#define DAILY_REPORT_PHONE "+18889990000"      // Daily report recipient

// Timing Configuration (in hours)
#define SLEEP_INTERVAL_HOURS 1          // How often to check tank level (default: 1 hour)
#define DAILY_REPORT_HOURS 24           // How often to send daily report (default: 24 hours)

// Pin Configuration (adjust if using different pins)
#define TANK_LEVEL_PIN 7               // Tank level sensor input pin
#define RELAY_CONTROL_PIN 5            // Relay output control pin
#define SD_CARD_CS_PIN 4               // SD card chip select pin

// Tank Level Logic
#define TANK_ALARM_STATE HIGH          // Sensor state that triggers alarm (HIGH or LOW)
#define SENSOR_DEBOUNCE_MS 100         // Debounce delay for sensor reading

// Logging Configuration
#define LOG_FILE_NAME "tanklog.txt"    // SD card log file name
#define ENABLE_SERIAL_DEBUG true       // Enable/disable serial output for debugging

// Network Configuration
#define CONNECTION_TIMEOUT_MS 30000    // Network connection timeout (30 seconds)
#define SMS_RETRY_ATTEMPTS 3           // Number of retry attempts for SMS sending

// Power Management
#define ENABLE_LOW_POWER_MODE true     // Enable low power sleep modes
#define WAKE_CHECK_DURATION_MS 5000    // How long to stay awake for each check

#endif // CONFIG_H