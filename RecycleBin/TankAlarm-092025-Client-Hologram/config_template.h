//
// Configuration file for Tank Alarm 092025
// This file now contains only essential compile-time constants
// Most configuration is now loaded from tank_config.txt on SD card
//

#ifndef CONFIG_H
#define CONFIG_H

// Pin Configuration (hardware specific - cannot be changed at runtime)
#define TANK_LEVEL_PIN 7               // Tank level sensor input pin
#define RELAY_CONTROL_PIN 5            // Relay output control pin
#define SD_CARD_CS_PIN 4               // SD card chip select pin (works for both MKR SD PROTO and MKR ETH shields)

// Sensor Type Configuration (hardware specific)
// Sensor Types: DIGITAL_FLOAT, ANALOG_VOLTAGE, CURRENT_LOOP
#define SENSOR_TYPE DIGITAL_FLOAT      // Type of tank level sensor

// Digital Float Switch Configuration (SENSOR_TYPE = DIGITAL_FLOAT)
#define TANK_ALARM_STATE HIGH          // Sensor state that triggers alarm (HIGH or LOW)
#define SENSOR_DEBOUNCE_MS 100         // Debounce delay for sensor reading

// Analog Voltage Sensor Configuration (SENSOR_TYPE = ANALOG_VOLTAGE)
#define ANALOG_SENSOR_PIN A1           // Analog input pin for voltage sensor (A1, A2, A3, or A4)
#define VOLTAGE_MIN 0.5                // Minimum sensor voltage (V)
#define VOLTAGE_MAX 4.5                // Maximum sensor voltage (V)
#define TANK_EMPTY_VOLTAGE 0.5         // Voltage when tank is empty (V)
#define TANK_FULL_VOLTAGE 4.5          // Voltage when tank is full (V)

// Current Loop Sensor Configuration (SENSOR_TYPE = CURRENT_LOOP)
#define I2C_CURRENT_LOOP_ADDRESS 0x48  // I2C address of NCD.io module
#define CURRENT_LOOP_CHANNEL 0         // Channel number (0-3) on NCD.io module
#define CURRENT_MIN 4.0                // Minimum current (mA)
#define CURRENT_MAX 20.0               // Maximum current (mA)
#define TANK_EMPTY_CURRENT 4.0         // Current when tank is empty (mA)
#define TANK_FULL_CURRENT 20.0         // Current when tank is full (mA)

// System Buffer Sizes and Hardware Limits (memory allocation at compile time)
#define MAX_CALIBRATION_POINTS 10
#define CALIBRATION_FILE_NAME "calibration.txt"

// Network Configuration (timeouts and hardware limits)
#define WAKE_CHECK_DURATION_MS 5000    // How long to stay awake for each check

// Logging Configuration (system limits)
#define ENABLE_SERIAL_DEBUG true       // Enable/disable serial output for debugging
#define LOG_FILE_NAME "tanklog.txt"    // SD card log file name

// SD Card Configuration Files
#define SD_CONFIG_FILE "tank_config.txt"      // SD card configuration file

// No fallback defaults - SD card configuration is required

#endif // CONFIG_H