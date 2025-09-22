//
// Configuration file for Tank Alarm 092025
// Copy this file and rename to config.h, then update with your specific values
//

#ifndef CONFIG_H
#define CONFIG_H

// Hologram.io Configuration
#define HOLOGRAM_DEVICE_KEY "your_device_key_here"  // Replace with your actual device key
#define SERVER_DEVICE_KEY "server_device_key_here"  // Server's Hologram device ID for remote commands
#define HOLOGRAM_APN "hologram"                     // Hologram.io APN (usually stays "hologram")

// Phone Numbers for Notifications (include country code, e.g., +1 for US)
#define ALARM_PHONE_PRIMARY "+12223334444"     // Primary emergency contact
#define ALARM_PHONE_SECONDARY "+15556667777"   // Secondary emergency contact  
#define DAILY_REPORT_PHONE "+18889990000"      // Daily report recipient

// Timing Configuration (in hours)
#define SLEEP_INTERVAL_HOURS 1          // How often to check tank level (default: 1 hour)
#define DAILY_REPORT_HOURS 24           // How often to send daily reports (default: 24 hours)

// Daily Report Time (in HH:MM format)
#define DAILY_REPORT_TIME "05:00"       // Time to send daily report (default: 5:00 AM)

// Sensor Configuration
#define SENSOR_TYPE 0                   // 0=DIGITAL_FLOAT, 1=ANALOG_VOLTAGE, 2=CURRENT_LOOP

// Tank Sensor Pin Configuration
#define TANK_LEVEL_PIN 7               // Digital input for float switch (pins 5,6,7 available on MKR RELAY)
#define ANALOG_SENSOR_PIN A1           // Analog input pin (A1, A2, A3, A4 available on MKR RELAY shield)
#define RELAY_PIN 5                    // Relay output pin (change as needed)

// I2C Current Loop Sensor Configuration (for CURRENT_LOOP sensor type)
#define I2C_CURRENT_LOOP_ADDRESS 0x48  // I2C address for current loop ADC
#define CURRENT_LOOP_CHANNEL 0         // ADC channel for current loop input

// See tank configuration section above for inches-based alarm settings

// Logging Configuration
#define LOG_FILE_NAME "tanklog.txt"    // SD card log file name
#define ENABLE_SERIAL_DEBUG true       // Enable serial output for debugging

// Network Configuration
#define CONNECTION_TIMEOUT_MS 30000    // Network connection timeout (30 seconds)
#define SMS_RETRY_ATTEMPTS 3           // Number of retry attempts for SMS sending

// Power Management
#define ENABLE_LOW_POWER_MODE true     // Enable low power sleep modes
#define WAKE_CHECK_DURATION_MS 5000    // How long to stay awake for each check
#define SHORT_SLEEP_MINUTES 10         // Short sleep duration for active monitoring (minutes)
#define NORMAL_SLEEP_HOURS 1           // Normal sleep duration between readings (hours)
#define ENABLE_WAKE_ON_PING true       // Enable wake-on-ping functionality
#define DEEP_SLEEP_MODE false          // Use deep sleep mode for maximum power savings

// Tank Configuration for Inches/Feet Measurements
#define TANK_NUMBER 1                  // Tank number identifier (1-99)
#define SITE_LOCATION_NAME "Your Site Name"  // Site location name for reports
#define INCHES_PER_UNIT 1.0            // Inches per sensor unit (calibration factor)
#define TANK_HEIGHT_INCHES 120         // Total tank height in inches

// Alarm Thresholds in Inches/Feet (for analog/current loop sensors)
#define HIGH_ALARM_INCHES 100          // High alarm threshold in inches
#define LOW_ALARM_INCHES 12            // Low alarm threshold in inches

// Digital Float Switch Alarm Configuration (for DIGITAL_FLOAT sensors)
#define DIGITAL_HIGH_ALARM true        // Enable high alarm for digital float
#define DIGITAL_LOW_ALARM false        // Enable low alarm for digital float

// Large Decrease Detection
#define ENABLE_LARGE_DECREASE_DETECTION true  // Enable detection of large decreases
#define LARGE_DECREASE_THRESHOLD_INCHES 24    // Threshold for large decrease in inches
#define LARGE_DECREASE_WAIT_HOURS 2           // Hours to wait before logging large decrease

// SD Card Configuration Files
#define SD_CONFIG_FILE "tank_config.txt"      // SD card configuration file
#define SD_HOURLY_LOG_FILE "hourly_log.txt"   // Hourly data log file
#define SD_DAILY_LOG_FILE "daily_log.txt"     // Daily report log file
#define SD_ALARM_LOG_FILE "alarm_log.txt"     // Alarm event log file
#define SD_DECREASE_LOG_FILE "decrease_log.txt" // Large decrease log file
#define SD_REPORT_LOG_FILE "report_log.txt"     // Daily report transmission log file

#endif // CONFIG_H