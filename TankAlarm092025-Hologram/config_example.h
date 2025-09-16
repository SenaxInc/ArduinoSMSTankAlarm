//
// Example Configuration file for Tank Alarm 092025
// This file shows example values - copy to config.h and update with your actual values
//

#ifndef CONFIG_H
#define CONFIG_H

// Hologram.io Configuration
// Get your device key from the Hologram.io dashboard after creating a device
#define HOLOGRAM_DEVICE_KEY "abc123def456ghi789"  // Example - replace with your actual key
#define HOLOGRAM_APN "hologram"                   // Hologram.io APN (keep as "hologram")

// Phone Numbers for Notifications
// IMPORTANT: Include country code (e.g., +1 for US, +44 for UK)
#define ALARM_PHONE_PRIMARY "+15551234567"     // Primary emergency contact
#define ALARM_PHONE_SECONDARY "+15559876543"   // Secondary emergency contact  
#define DAILY_REPORT_PHONE "+15555551234"      // Daily report recipient

// Timing Configuration (in hours)
#define SLEEP_INTERVAL_HOURS 1          // Check tank every 1 hour
#define DAILY_REPORT_HOURS 24           // Send daily report every 24 hours

// Pin Configuration (use defaults unless you've modified hardware)
#define TANK_LEVEL_PIN 7               // Tank level sensor input pin
#define RELAY_CONTROL_PIN 5            // Relay output control pin
#define SD_CARD_CS_PIN 4               // SD card chip select pin

// Tank Level Logic
#define TANK_ALARM_STATE HIGH          // Sensor state that triggers alarm
#define SENSOR_DEBOUNCE_MS 100         // Debounce delay for sensor reading

// Logging Configuration
#define LOG_FILE_NAME "tanklog.txt"    // SD card log file name
#define ENABLE_SERIAL_DEBUG true       // Enable serial output for debugging

// Network Configuration
#define CONNECTION_TIMEOUT_MS 30000    // Network connection timeout (30 seconds)
#define SMS_RETRY_ATTEMPTS 3           // Number of retry attempts for SMS sending

// Power Management
#define ENABLE_LOW_POWER_MODE true     // Enable low power sleep modes
#define WAKE_CHECK_DURATION_MS 5000    // How long to stay awake for each check

// Debug Options (for development/testing)
#define DEBUG_WAIT_SERIAL false        // Wait for serial connection at startup
// #define ENABLE_TEST_MODE true       // Uncomment to enable test mode features

#endif // CONFIG_H