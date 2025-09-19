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
#define DAILY_REPORT_HOURS 24           // How often to send daily report (default: 24 hours)
#define DAILY_REPORT_TIME "05:00"       // Time of day for daily report in HH:MM format (24-hour)

// Pin Configuration (adjust if using different pins)
#define TANK_LEVEL_PIN 7               // Tank level sensor input pin
#define RELAY_CONTROL_PIN 5            // Relay output control pin
#define SD_CARD_CS_PIN 4               // SD card chip select pin

// Tank Level Sensor Configuration
// Sensor Types: DIGITAL_FLOAT, ANALOG_VOLTAGE, CURRENT_LOOP
#define SENSOR_TYPE DIGITAL_FLOAT      // Type of tank level sensor

// Digital Float Switch Configuration (SENSOR_TYPE = DIGITAL_FLOAT)
#define TANK_ALARM_STATE HIGH          // Sensor state that triggers alarm (HIGH or LOW)
#define SENSOR_DEBOUNCE_MS 100         // Debounce delay for sensor reading

// Analog Voltage Sensor Configuration (SENSOR_TYPE = ANALOG_VOLTAGE)
// For Dwyer 626 series ratiometric 0.5-4.5V pressure sensors
// Can use A1-A4 pins on MKR RELAY shield with convenient screw terminals
#define ANALOG_SENSOR_PIN A1           // Analog input pin for voltage sensor (A1, A2, A3, or A4)
#define VOLTAGE_MIN 0.5                // Minimum sensor voltage (V)
#define VOLTAGE_MAX 4.5                // Maximum sensor voltage (V)
#define TANK_EMPTY_VOLTAGE 0.5         // Voltage when tank is empty (V)
#define TANK_FULL_VOLTAGE 4.5          // Voltage when tank is full (V)
#define ALARM_THRESHOLD_PERCENT 80     // Alarm when tank is X% full

// Multiple Analog Sensor Support (Optional - for multiple tank monitoring)
// Uncomment and configure additional sensors if needed
// #define ENABLE_MULTI_ANALOG_SENSORS true
// #define ANALOG_SENSOR_PIN_2 A2        // Second analog sensor
// #define ANALOG_SENSOR_PIN_3 A3        // Third analog sensor  
// #define ANALOG_SENSOR_PIN_4 A4        // Fourth analog sensor

// Current Loop Sensor Configuration (SENSOR_TYPE = CURRENT_LOOP)
// For 4-20mA sensors using NCD.io 4-channel current loop I2C module
#define I2C_CURRENT_LOOP_ADDRESS 0x48  // I2C address of NCD.io module
#define CURRENT_LOOP_CHANNEL 0         // Channel number (0-3) on NCD.io module
#define CURRENT_MIN 4.0                // Minimum current (mA)
#define CURRENT_MAX 20.0               // Maximum current (mA)
#define TANK_EMPTY_CURRENT 4.0         // Current when tank is empty (mA)
#define TANK_FULL_CURRENT 20.0         // Current when tank is full (mA)
#define ALARM_THRESHOLD_CURRENT_PERCENT 80  // Alarm when tank is X% full

// Logging Configuration
#define LOG_FILE_NAME "tanklog.txt"    // SD card log file name
#define ENABLE_SERIAL_DEBUG true       // Enable/disable serial output for debugging

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