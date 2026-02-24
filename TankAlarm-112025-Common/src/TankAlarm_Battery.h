/**
 * TankAlarm_Battery.h
 * 
 * Battery Voltage Monitoring via Blues Notecard
 * 
 * Uses the Notecard's card.voltage API to monitor battery health when
 * the Notecard is wired directly to the battery (3.8V - 17V VIN range).
 * 
 * Features:
 * - Real-time voltage monitoring
 * - Configurable thresholds for 12V lead-acid, LiFePO4, or custom batteries
 * - Trend analysis (daily, weekly, monthly voltage changes)
 * - Low voltage alerts
 * - Integration with daily reports
 * 
 * Hardware Requirements:
 * - Blues Notecard wired directly to battery (not through 5V regulator)
 * - Optional: Schottky diode for reverse polarity protection
 * 
 * Note: The Notecard's card.voltage calibration offset (default 0.35V) 
 * accounts for forward voltage drop of protection diodes. Adjust if using
 * different diode or no diode.
 * 
 * Copyright (c) 2025-2026 Senax Inc. All rights reserved.
 */

#ifndef TANKALARM_BATTERY_H
#define TANKALARM_BATTERY_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Battery Type Definitions
// ============================================================================

/**
 * Predefined battery types with appropriate voltage thresholds.
 * These map to the Notecard's card.voltage "mode" settings.
 */
enum BatteryType : uint8_t {
  BATTERY_TYPE_LEAD_ACID_12V = 0,  // 12V lead-acid (AGM, flooded, gel)
  BATTERY_TYPE_LIFEPO4_12V   = 1,  // 12V LiFePO4 (4S configuration)
  BATTERY_TYPE_LIPO          = 2,  // LiPo battery (Notecard default)
  BATTERY_TYPE_CUSTOM        = 3   // Custom thresholds
};

// ============================================================================
// 12V Lead-Acid Battery Thresholds (AGM/Flooded/Gel)
// State of Charge (SOC) reference at 25°C, no load
// ============================================================================
#define LEAD_ACID_12V_FULL       12.70f   // 100% SOC
#define LEAD_ACID_12V_HIGH       12.40f   // ~75% SOC
#define LEAD_ACID_12V_NORMAL     12.20f   // ~50% SOC
#define LEAD_ACID_12V_LOW        12.00f   // ~25% SOC (warning threshold)
#define LEAD_ACID_12V_CRITICAL   11.80f   // ~10% SOC (critical - immediate action)
#define LEAD_ACID_12V_DEAD       10.50f   // Battery damage if discharged further

// ============================================================================
// 12V LiFePO4 Battery Thresholds (4S configuration)
// LiFePO4 has flatter discharge curve, narrower voltage window
// ============================================================================
#define LIFEPO4_12V_FULL         14.60f   // 100% SOC (4 x 3.65V)
#define LIFEPO4_12V_HIGH         13.60f   // ~80% SOC
#define LIFEPO4_12V_NORMAL       13.20f   // ~50% SOC
#define LIFEPO4_12V_LOW          12.80f   // ~20% SOC (warning threshold)
#define LIFEPO4_12V_CRITICAL     12.00f   // ~5% SOC (critical - stop discharge)
#define LIFEPO4_12V_DEAD         10.00f   // BMS should disconnect before this

// ============================================================================
// Battery Alert Types
// ============================================================================
enum BatteryAlertType : uint8_t {
  BATTERY_ALERT_NONE          = 0,
  BATTERY_ALERT_LOW           = 1,   // Battery below low threshold
  BATTERY_ALERT_CRITICAL      = 2,   // Battery below critical threshold
  BATTERY_ALERT_HIGH          = 3,   // Battery overvoltage (charging issue)
  BATTERY_ALERT_DECLINING     = 4,   // Significant voltage decline trend
  BATTERY_ALERT_USB_LOST      = 5,   // Lost USB/external power (if monitored)
  BATTERY_ALERT_RECOVERED     = 6    // Battery voltage recovered to normal
};

// ============================================================================
// Battery Data Structure (data from card.voltage)
// ============================================================================
struct BatteryData {
  // Current measurements
  float voltage;              // Current battery voltage (V)
  const char* mode;           // Voltage mode state (usb/high/normal/low/dead)
  bool usbPowered;            // True if USB power connected
  uint32_t uptimeMinutes;     // Device uptime in minutes
  
  // Historical data (from trend analysis)
  float voltageMin;           // Minimum voltage in analysis period
  float voltageMax;           // Maximum voltage in analysis period
  float voltageAvg;           // Average voltage in analysis period
  uint16_t analysisHours;     // Hours of data analyzed
  
  // Trend data (voltage change rates)
  float dailyChange;          // Voltage change in last 24 hours
  float weeklyChange;         // Voltage change in last 7 days
  float monthlyChange;        // Voltage change in last 30 days
  
  // Derived status
  bool isHealthy;             // Battery within normal range
  bool isCharging;            // Voltage trending up (likely charging)
  bool isDeclining;           // Significant voltage decline detected
  
  // Data validity
  bool valid;                 // True if data was successfully read
  uint32_t lastReadMillis;    // Timestamp of last successful read
};

// ============================================================================
// Battery Configuration
// ============================================================================
struct BatteryConfig {
  bool enabled;               // true = battery monitoring enabled
  BatteryType batteryType;    // Battery type for automatic thresholds
  
  // Voltage thresholds (set automatically from batteryType, or custom)
  float highVoltage;          // High voltage warning (overcharge)
  float normalVoltage;        // Normal operating minimum
  float lowVoltage;           // Low voltage warning threshold
  float criticalVoltage;      // Critical low voltage (immediate alert)
  
  // Calibration
  float calibrationOffset;    // Diode voltage drop compensation (default: 0.35V)
  
  // Monitoring parameters
  uint16_t pollIntervalSec;   // How often to poll voltage (default: 300s = 5 min)
  uint16_t trendAnalysisHours;// Hours of data for trend analysis (default: 168 = 7 days)
  
  // Alert configuration
  bool alertOnLow;            // Send alert when voltage goes low
  bool alertOnCritical;       // Send alert when voltage is critical
  bool alertOnDeclining;      // Send alert on significant decline trend
  bool alertOnRecovery;       // Send alert when voltage recovers to normal
  
  // Trend alert threshold
  float declineAlertThreshold;// Weekly decline (V) to trigger alert (default: 0.5V)
  
  // Include in reports
  bool includeInDailyReport;  // Include battery data in daily report
};

// ============================================================================
// Voltage Divider (Vin Monitor) Configuration
// ============================================================================
// Reads actual battery voltage via analog input + external voltage divider.
//
// Wiring: Battery+ --> R1 --> Opta Analog Pin --> R2 --> GND
//   Example: R1=22kΩ, R2=47kΩ → ratio = 47/(22+47) = 0.6812
//            12V × 0.6812 = 8.17V at pin (within Opta 0-10V ADC range)
//            Max readable voltage = 10V / 0.6812 = 14.68V
//
// The divider draws continuous quiescent current: 12V / (R1+R2) ≈ 0.17mA
// which is negligible compared to MCU + Notecard draw (~100-200mA).

struct VinMonitorConfig {
  bool enabled;               // true = voltage divider hardware is connected
  uint8_t analogPin;          // Opta analog input pin index (0=A0 .. 7=A7)
  float r1Kohm;               // High-side resistor in kΩ (battery to pin)
  float r2Kohm;               // Low-side resistor in kΩ (pin to GND)
  uint16_t pollIntervalSec;   // How often to read (seconds)
  bool includeInDailyReport;  // Include Vin reading in daily report
};

// Defaults for VinMonitorConfig
#define VIN_MONITOR_DEFAULT_PIN             0       // A0
#define VIN_MONITOR_DEFAULT_R1_KOHM         22.0f   // 22kΩ high-side
#define VIN_MONITOR_DEFAULT_R2_KOHM         47.0f   // 47kΩ low-side
#define VIN_MONITOR_DEFAULT_POLL_SEC        300     // 5 minutes
#define VIN_MONITOR_ADC_RESOLUTION          12      // 12-bit ADC (matches client analogReadResolution)
#define VIN_MONITOR_ADC_MAX                 4095.0f // 2^12 - 1
#define VIN_MONITOR_ADC_REF_VOLTAGE         10.0f   // Opta analog inputs: 0-10V range

/**
 * Initialize VinMonitorConfig with defaults.
 */
inline void initVinMonitorConfig(VinMonitorConfig* config) {
  if (!config) return;
  config->enabled = false;
  config->analogPin = VIN_MONITOR_DEFAULT_PIN;
  config->r1Kohm = VIN_MONITOR_DEFAULT_R1_KOHM;
  config->r2Kohm = VIN_MONITOR_DEFAULT_R2_KOHM;
  config->pollIntervalSec = VIN_MONITOR_DEFAULT_POLL_SEC;
  config->includeInDailyReport = true;
}

/**
 * Calculate the voltage divider ratio: R2 / (R1 + R2).
 * Returns 0 if resistors are invalid (prevents division by zero).
 */
inline float vinDividerRatio(const VinMonitorConfig* config) {
  if (!config || (config->r1Kohm + config->r2Kohm) <= 0.0f) return 0.0f;
  return config->r2Kohm / (config->r1Kohm + config->r2Kohm);
}

/**
 * Calculate the maximum readable battery voltage for a given divider config.
 * Returns 0 if config is invalid.
 */
inline float vinMaxReadableVoltage(const VinMonitorConfig* config) {
  float ratio = vinDividerRatio(config);
  if (ratio <= 0.0f) return 0.0f;
  return VIN_MONITOR_ADC_REF_VOLTAGE / ratio;
}

// ============================================================================
// Default Configuration Values
// ============================================================================
#define BATTERY_DEFAULT_POLL_INTERVAL_SEC     300    // 5 minutes
#define BATTERY_DEFAULT_TREND_HOURS           168    // 7 days
#define BATTERY_DEFAULT_CALIBRATION           0.35f  // Schottky diode drop
#define BATTERY_DEFAULT_DECLINE_THRESHOLD     0.5f   // 0.5V weekly decline = alert

// Minimum interval between same battery alerts (1 hour)
#define BATTERY_ALARM_MIN_INTERVAL_MS         3600000UL

// ============================================================================
// Solar-Only (No Battery) Configuration
// ============================================================================
// For installations powered directly by a solar panel with NO battery backup.
// The device only operates when the sun is out and must handle:
//   - Startup debounce (wait for stable voltage before reading sensors)
//   - Sensor voltage gating (4-20mA sensors need minimum excitation voltage)
//   - Opportunistic daily reports (send ASAP after boot if overdue)
//   - Sunset protocol (detect declining voltage, save state before power loss)
//   - Battery failure fallback (auto-enable for solar+battery when battery fails)

struct SolarOnlyConfig {
  bool enabled;                     // true = solar-only (no battery) mode active
  float startupDebounceVoltage;     // Min Vin before system is "ready" (with Vin divider)
  uint16_t startupDebounceSec;      // Vin must stay above debounce voltage for this long
  uint16_t startupWarmupSec;        // Warmup time without Vin divider (fallback timer)
  float sensorGateVoltage;          // Min Vin to read sensors (with Vin divider)
  float sunsetVoltage;              // Vin below this starts sunset protocol (with Vin divider)
  uint16_t sunsetConfirmSec;        // Declining voltage duration before shutdown save
  uint16_t opportunisticReportHours;// Send report ASAP if this many hours since last
  bool batteryFailureFallback;      // Auto-enable solar-only behaviors if battery fails
  uint8_t batteryFailureThreshold;  // Consecutive CRITICAL readings to trigger fallback
};

// Solar-Only default values
#define SOLAR_ONLY_DEFAULT_DEBOUNCE_VOLTAGE    10.0f   // 10V minimum to start
#define SOLAR_ONLY_DEFAULT_DEBOUNCE_SEC        30      // 30s stable
#define SOLAR_ONLY_DEFAULT_WARMUP_SEC          60      // 60s warmup without Vin divider
#define SOLAR_ONLY_DEFAULT_SENSOR_GATE_VOLTAGE 11.0f   // 11V for 4-20mA excitation
#define SOLAR_ONLY_DEFAULT_SUNSET_VOLTAGE      10.0f   // Below 10V = sunset approaching
#define SOLAR_ONLY_DEFAULT_SUNSET_CONFIRM_SEC  120     // 2 minutes declining = save state
#define SOLAR_ONLY_DEFAULT_REPORT_HOURS        20      // Report if >20h since last
#define SOLAR_ONLY_DEFAULT_FAILURE_THRESHOLD   10      // 10 consecutive critical readings

// State persistence file for solar-only mode (survives power cycles)
#define SOLAR_STATE_FILE "/fs/solar_state.json"

/**
 * Initialize SolarOnlyConfig with defaults.
 */
inline void initSolarOnlyConfig(SolarOnlyConfig* config) {
  if (!config) return;
  config->enabled = false;
  config->startupDebounceVoltage = SOLAR_ONLY_DEFAULT_DEBOUNCE_VOLTAGE;
  config->startupDebounceSec = SOLAR_ONLY_DEFAULT_DEBOUNCE_SEC;
  config->startupWarmupSec = SOLAR_ONLY_DEFAULT_WARMUP_SEC;
  config->sensorGateVoltage = SOLAR_ONLY_DEFAULT_SENSOR_GATE_VOLTAGE;
  config->sunsetVoltage = SOLAR_ONLY_DEFAULT_SUNSET_VOLTAGE;
  config->sunsetConfirmSec = SOLAR_ONLY_DEFAULT_SUNSET_CONFIRM_SEC;
  config->opportunisticReportHours = SOLAR_ONLY_DEFAULT_REPORT_HOURS;
  config->batteryFailureFallback = false;
  config->batteryFailureThreshold = SOLAR_ONLY_DEFAULT_FAILURE_THRESHOLD;
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Initialize BatteryConfig with defaults for a specific battery type.
 * 
 * @param config Pointer to BatteryConfig to initialize
 * @param type Battery type to configure for
 */
inline void initBatteryConfig(BatteryConfig* config, BatteryType type) {
  if (!config) return;
  
  config->enabled = false;  // Must be explicitly enabled
  config->batteryType = type;
  config->calibrationOffset = BATTERY_DEFAULT_CALIBRATION;
  config->pollIntervalSec = BATTERY_DEFAULT_POLL_INTERVAL_SEC;
  config->trendAnalysisHours = BATTERY_DEFAULT_TREND_HOURS;
  config->alertOnLow = true;
  config->alertOnCritical = true;
  config->alertOnDeclining = true;
  config->alertOnRecovery = false;  // Usually not needed
  config->declineAlertThreshold = BATTERY_DEFAULT_DECLINE_THRESHOLD;
  config->includeInDailyReport = true;
  
  // Set thresholds based on battery type
  switch (type) {
    case BATTERY_TYPE_LEAD_ACID_12V:
      config->highVoltage = 14.8f;  // Typical equalization voltage
      config->normalVoltage = LEAD_ACID_12V_NORMAL;
      config->lowVoltage = LEAD_ACID_12V_LOW;
      config->criticalVoltage = LEAD_ACID_12V_CRITICAL;
      break;
      
    case BATTERY_TYPE_LIFEPO4_12V:
      config->highVoltage = 14.8f;  // Above max charge voltage
      config->normalVoltage = LIFEPO4_12V_NORMAL;
      config->lowVoltage = LIFEPO4_12V_LOW;
      config->criticalVoltage = LIFEPO4_12V_CRITICAL;
      break;
      
    case BATTERY_TYPE_LIPO:
      // Use Notecard defaults for LiPo
      config->highVoltage = 4.6f;
      config->normalVoltage = 3.5f;
      config->lowVoltage = 3.2f;
      config->criticalVoltage = 3.0f;
      break;
      
    case BATTERY_TYPE_CUSTOM:
    default:
      // Safe defaults
      config->highVoltage = 15.0f;
      config->normalVoltage = 12.0f;
      config->lowVoltage = 11.5f;
      config->criticalVoltage = 11.0f;
      break;
  }
}

/**
 * Get human-readable battery state description based on voltage thresholds.
 */
inline const char* getBatteryStateDescription(float voltage, const BatteryConfig* config) {
  if (!config) return "unknown";
  if (voltage >= config->highVoltage) return "charging/high";
  if (voltage >= config->normalVoltage) return "good";
  if (voltage >= config->lowVoltage) return "low";
  if (voltage >= config->criticalVoltage) return "critical";
  return "dead";
}

#endif // TANKALARM_BATTERY_H
