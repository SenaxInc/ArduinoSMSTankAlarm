/**
 * TankAlarm_Solar.h
 * 
 * SunSaver MPPT Solar Charger Monitoring via RS485 Modbus RTU
 * 
 * Hardware Requirements:
 * - Arduino Opta with RS485 (AFX00003 or built-in on Opta WiFi/RS485)
 * - Morningstar MRC-1 (MeterBus to EIA-485 Adapter) - Recommended
 *   - Powered by SunSaver via RJ-11 cable (no external power required)
 *   - Provides isolated RS-485 connection to Opta
 *   - Wiring: Opta A(-) to MRC-1 B(-), Opta B(+) to MRC-1 A(+), GND to G
 * 
 * Alternative (DIY, not recommended):
 * - Generic TTL to RS-485 module with auto-flow control (e.g., XY-017)
 *   - Warning: Requires voltage step-down from SunSaver 12V to 5V
 *   - Not isolated - risk of ground loops
 * 
 * Modbus Protocol:
 * - Protocol: Modbus RTU over RS-485
 * - Default Slave ID: 1
 * - Baud Rate: 9600 (typical)
 * - Data Format: 8N1 or 8N2
 * 
 * Copyright (c) 2025-2026 Senax Inc. All rights reserved.
 */

#ifndef TANKALARM_SOLAR_H
#define TANKALARM_SOLAR_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// SunSaver MPPT Modbus Register Addresses (Holding Registers, Function Code 03)
// Note: ArduinoModbus uses 0-based addresses, so address = register - 1
// ============================================================================

// Voltage and Current Registers (Real-time)
#define SS_REG_BATTERY_VOLTAGE      0x0012  // Register 19: Battery Voltage
#define SS_REG_ARRAY_VOLTAGE        0x0013  // Register 20: Array (Solar Panel) Voltage  
#define SS_REG_CHARGE_CURRENT       0x0010  // Register 17: Charge Current
#define SS_REG_LOAD_CURRENT         0x0011  // Register 18: Load Current

// Temperature
#define SS_REG_HEATSINK_TEMP        0x001B  // Register 28: Heatsink Temperature (°C, signed)
#define SS_REG_BATTERY_TEMP         0x001C  // Register 29: Battery Temperature (°C, signed, if RTS connected)

// Status Registers
#define SS_REG_CHARGE_STATE         0x002B  // Register 44: Charge State
#define SS_REG_FAULTS               0x002C  // Register 45: Faults (bitfield)
#define SS_REG_ALARMS               0x002E  // Register 47: Alarms (bitfield)
#define SS_REG_LOAD_STATE           0x002F  // Register 48: Load State

// Daily Statistics
#define SS_REG_BATTERY_V_MIN_DAILY  0x003D  // Register 62: Minimum Battery Voltage Today
#define SS_REG_BATTERY_V_MAX_DAILY  0x003E  // Register 63: Maximum Battery Voltage Today
#define SS_REG_AH_DAILY             0x0034  // Register 53: Amp-hours charged today
#define SS_REG_WH_DAILY             0x0038  // Register 57: Watt-hours charged today (if available)

// ============================================================================
// Scaling Factors for 12V System
// Formula: Actual = (Raw * Scale) / 32768
// ============================================================================
#define SS_SCALE_VOLTAGE_12V        100.0f    // Voltage scaling: Raw * 100 / 32768
#define SS_SCALE_CURRENT_12V        79.16f    // Current scaling: Raw * 79.16 / 32768
#define SS_SCALE_DIVISOR            32768.0f  // Common divisor

// ============================================================================
// Charge State Values
// ============================================================================
enum SolarChargeState : uint8_t {
  CHARGE_STATE_START      = 0,  // Controller starting up
  CHARGE_STATE_NIGHT_CHECK= 1,  // Checking for night
  CHARGE_STATE_DISCONNECT = 2,  // Disconnected
  CHARGE_STATE_NIGHT      = 3,  // Night mode (solar offline)
  CHARGE_STATE_FAULT      = 4,  // Fault condition
  CHARGE_STATE_BULK       = 5,  // Bulk charging (battery below 80%)
  CHARGE_STATE_ABSORPTION = 6,  // Absorption charging (battery 80-100%)
  CHARGE_STATE_FLOAT      = 7,  // Float charging (battery fully charged)
  CHARGE_STATE_EQUALIZE   = 8   // Equalization charging (if configured)
};

// ============================================================================
// Fault Bitfield Definitions (Register 45)
// ============================================================================
#define SS_FAULT_OVERCURRENT        (1 << 0)  // Bit 0: Overcurrent
#define SS_FAULT_FET_SHORT          (1 << 1)  // Bit 1: FET short
#define SS_FAULT_SOFTWARE           (1 << 2)  // Bit 2: Software fault
#define SS_FAULT_BATT_HVD           (1 << 3)  // Bit 3: Battery high voltage disconnect
#define SS_FAULT_ARRAY_HVD          (1 << 4)  // Bit 4: Array high voltage disconnect
#define SS_FAULT_DIP_SW_FAULT       (1 << 5)  // Bit 5: DIP switch changed
#define SS_FAULT_RESET_FAULT        (1 << 6)  // Bit 6: Settings reset (EEPROM corrupt)
#define SS_FAULT_RTS_DISCONN        (1 << 7)  // Bit 7: RTS (temp sensor) disconnected
#define SS_FAULT_RTS_SHORT          (1 << 8)  // Bit 8: RTS shorted
#define SS_FAULT_HEATSINK_LIMIT     (1 << 9)  // Bit 9: Heatsink temperature limit

// ============================================================================
// Alarm Bitfield Definitions (Register 47)
// ============================================================================
#define SS_ALARM_RTS_OPEN           (1 << 0)  // Bit 0: RTS open
#define SS_ALARM_RTS_SHORT          (1 << 1)  // Bit 1: RTS shorted
#define SS_ALARM_RTS_DISCONN        (1 << 2)  // Bit 2: RTS disconnected
#define SS_ALARM_HEATSINK_LIMIT     (1 << 3)  // Bit 3: Heatsink temperature limit
#define SS_ALARM_CURRENT_LIMIT      (1 << 4)  // Bit 4: Current limit reached
#define SS_ALARM_CURRENT_OFFSET     (1 << 5)  // Bit 5: Current offset error
#define SS_ALARM_BATT_SENSE         (1 << 6)  // Bit 6: Battery sense out of range
#define SS_ALARM_BATT_SENSE_DISC    (1 << 7)  // Bit 7: Battery sense disconnected
#define SS_ALARM_UNCALIBRATED       (1 << 8)  // Bit 8: Controller uncalibrated
#define SS_ALARM_RTS_MISWIRE        (1 << 9)  // Bit 9: RTS miswired
#define SS_ALARM_HVD                (1 << 10) // Bit 10: High voltage disconnect
#define SS_ALARM_LOG_TIMEOUT        (1 << 11) // Bit 11: Log timeout
#define SS_ALARM_EEPROM             (1 << 12) // Bit 12: EEPROM access error

// ============================================================================
// Battery Health Thresholds (12V AGM System)
// ============================================================================
#define BATTERY_VOLTAGE_CRITICAL    11.5f   // Critical low voltage (immediate alarm)
#define BATTERY_VOLTAGE_LOW         11.8f   // Low voltage warning
#define BATTERY_VOLTAGE_NORMAL      12.0f   // Normal minimum voltage
#define BATTERY_VOLTAGE_FLOAT       13.4f   // Float charge voltage (fully charged)
#define BATTERY_VOLTAGE_HIGH        14.8f   // High voltage warning (overcharge)

// ============================================================================
// Solar Data Structure
// ============================================================================
struct SolarData {
  // Real-time measurements
  float batteryVoltage;       // Battery voltage (V)
  float arrayVoltage;         // Solar panel voltage (V)
  float chargeCurrent;        // Charging current (A)
  float loadCurrent;          // Load current (A)
  int8_t heatsinkTemp;        // Heatsink temperature (°C)
  int8_t batteryTemp;         // Battery temperature (°C, if RTS connected)
  
  // Status
  SolarChargeState chargeState; // Current charge state
  uint16_t faults;            // Fault bitfield
  uint16_t alarms;            // Alarm bitfield
  bool loadOn;                // Load output state
  
  // Daily statistics
  float batteryVoltageMinDaily; // Minimum battery voltage today
  float batteryVoltageMaxDaily; // Maximum battery voltage today
  float ampHoursDaily;        // Amp-hours charged today
  float wattHoursDaily;       // Watt-hours charged today
  
  // Derived health indicators
  bool batteryHealthy;        // Overall battery health status
  bool solarHealthy;          // Overall solar system health status
  bool hasFault;              // Any fault condition present
  bool hasAlarm;              // Any alarm condition present
  bool isCharging;            // Currently charging (bulk, absorption, or equalize)
  bool isFullyCharged;        // Battery fully charged (float mode)
  
  // Communication status
  bool communicationOk;       // Last Modbus read successful
  uint32_t lastReadMillis;    // Timestamp of last successful read
  uint8_t consecutiveErrors;  // Count of consecutive read errors
};

// ============================================================================
// Solar Alert Types
// ============================================================================
enum SolarAlertType : uint8_t {
  SOLAR_ALERT_NONE              = 0,
  SOLAR_ALERT_BATTERY_LOW       = 1,  // Battery below low threshold
  SOLAR_ALERT_BATTERY_CRITICAL  = 2,  // Battery below critical threshold
  SOLAR_ALERT_BATTERY_HIGH      = 3,  // Battery overvoltage
  SOLAR_ALERT_FAULT             = 4,  // SunSaver fault condition
  SOLAR_ALERT_ALARM             = 5,  // SunSaver alarm condition
  SOLAR_ALERT_COMM_FAILURE      = 6,  // Modbus communication failure
  SOLAR_ALERT_HEATSINK_TEMP     = 7,  // Heatsink overtemperature
  SOLAR_ALERT_NO_CHARGE         = 8   // No charging during daylight (potential panel issue)
};

// ============================================================================
// Solar Configuration
// ============================================================================
struct SolarConfig {
  bool enabled;               // true = solar monitoring enabled
  uint8_t modbusSlaveId;      // Modbus slave ID (default: 1)
  uint16_t modbusBaudRate;    // Baud rate (default: 9600)
  uint16_t modbusTimeoutMs;   // Modbus read timeout (default: 200ms)
  uint16_t pollIntervalSec;   // Polling interval (default: 60 seconds)
  
  // Battery thresholds (customizable for different battery types)
  float batteryLowVoltage;    // Low voltage warning threshold (default: 11.8V)
  float batteryCriticalVoltage; // Critical voltage alarm threshold (default: 11.5V)
  float batteryHighVoltage;   // High voltage warning threshold (default: 14.8V)
  
  // Alert configuration
  bool alertOnLowBattery;     // Send alert on low battery (default: true)
  bool alertOnFault;          // Send alert on SunSaver fault (default: true)
  bool alertOnCommFailure;    // Send alert on communication failure (default: false)
  bool includeInDailyReport;  // Include solar data in daily report (default: true)
};

// ============================================================================
// Default Configuration Values
// ============================================================================
#define SOLAR_DEFAULT_SLAVE_ID          1
#define SOLAR_DEFAULT_BAUD_RATE         9600
#define SOLAR_DEFAULT_TIMEOUT_MS        200
#define SOLAR_DEFAULT_POLL_INTERVAL_SEC 60
#define SOLAR_COMM_FAILURE_THRESHOLD    5

// ============================================================================
// SolarManager Class Interface
// ============================================================================
#ifdef __cplusplus

class SolarManager {
public:
  SolarManager();
  
  // Initialization
  bool begin(const SolarConfig& config);
  void end();
  
  // Configuration
  void setConfig(const SolarConfig& config);
  const SolarConfig& getConfig() const { return _config; }
  
  // Polling (call periodically from main loop)
  bool poll(unsigned long nowMillis);
  
  // Data access
  const SolarData& getData() const { return _data; }
  
  // Health assessment
  SolarAlertType checkAlerts() const;
  const char* getAlertDescription(SolarAlertType alert) const;
  const char* getChargeStateDescription() const;
  const char* getFaultDescription() const;
  const char* getAlarmDescription() const;
  
  // Status helpers
  bool isEnabled() const { return _config.enabled && _initialized; }
  bool isCommunicationOk() const { return _data.communicationOk; }
  bool isBatteryHealthy() const { return _data.batteryHealthy; }
  bool isSolarHealthy() const { return _data.solarHealthy; }
  
  // Reset daily statistics (call at midnight or report time)
  void resetDailyStats();
  
private:
  SolarConfig _config;
  SolarData _data;
  bool _initialized;
  unsigned long _lastPollMillis;
  
  // Modbus communication
  bool readRegisters();
  float scaleVoltage(uint16_t raw) const;
  float scaleCurrent(uint16_t raw) const;
  
  // Health assessment
  void updateHealthStatus();
};

#endif // __cplusplus

#endif // TANKALARM_SOLAR_H
