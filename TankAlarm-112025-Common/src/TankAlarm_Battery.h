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
 * Predefined battery types (chemistry only).
 * Nominal pack voltage (12V/24V) is carried separately in BatteryConfig.nominalVoltage
 * so that thresholds can be scaled at runtime instead of duplicating enum values.
 *
 * Legacy values (LEAD_ACID_12V=0, LIFEPO4_12V=1, LIPO=2, CUSTOM=3) are preserved
 * for back-compat with config files written by older firmware.
 */
enum BatteryType : uint8_t {
  // Legacy values (do not renumber)
  BATTERY_TYPE_LEAD_ACID_12V = 0,  // Legacy: generic 12V lead-acid (= AGM)
  BATTERY_TYPE_LIFEPO4_12V   = 1,  // Legacy: generic 12V LiFePO4
  BATTERY_TYPE_LIPO          = 2,  // LiPo battery (Notecard default, single cell)
  BATTERY_TYPE_CUSTOM        = 3,  // Custom thresholds (no auto-init)

  // New chemistry-specific values (decoupled from pack voltage)
  BATTERY_TYPE_NONE          = 4,  // No battery (solar-direct or grid-only)
  BATTERY_TYPE_AGM           = 5,  // Sealed AGM lead-acid
  BATTERY_TYPE_FLOODED       = 6,  // Flooded (wet) lead-acid (supports equalize)
  BATTERY_TYPE_GEL           = 7,  // Gel lead-acid (no equalize)
  BATTERY_TYPE_SLA           = 8,  // Generic sealed lead-acid (treated as AGM)
  BATTERY_TYPE_LIFEPO4       = 9,  // LiFePO4 (any pack voltage; uses nominalVoltage)
  BATTERY_TYPE_LI_ION        = 10  // Li-ion (NMC/LCO chemistry)
};

/**
 * Returns true if the battery type is any lead-acid chemistry.
 */
inline bool batteryIsLeadAcid(BatteryType t) {
  return t == BATTERY_TYPE_LEAD_ACID_12V || t == BATTERY_TYPE_AGM ||
         t == BATTERY_TYPE_FLOODED || t == BATTERY_TYPE_GEL || t == BATTERY_TYPE_SLA;
}

/**
 * Returns true if the battery type supports equalization charging.
 * Only flooded lead-acid benefits from periodic equalization.
 */
inline bool batterySupportsEqualize(BatteryType t) {
  return t == BATTERY_TYPE_FLOODED;
}

/**
 * Human-readable label for a BatteryType (for UI/reports).
 */
inline const char* batteryTypeLabel(BatteryType t) {
  switch (t) {
    case BATTERY_TYPE_NONE:          return "None";
    case BATTERY_TYPE_AGM:           return "AGM";
    case BATTERY_TYPE_FLOODED:       return "Flooded";
    case BATTERY_TYPE_GEL:           return "Gel";
    case BATTERY_TYPE_SLA:           return "SLA";
    case BATTERY_TYPE_LIFEPO4:       return "LiFePO4";
    case BATTERY_TYPE_LI_ION:        return "Li-ion";
    case BATTERY_TYPE_LIPO:          return "LiPo";
    case BATTERY_TYPE_LEAD_ACID_12V: return "Lead-Acid 12V";
    case BATTERY_TYPE_LIFEPO4_12V:   return "LiFePO4 12V";
    case BATTERY_TYPE_CUSTOM:        return "Custom";
    default:                         return "Unknown";
  }
}

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
  char mode[16];              // Voltage mode state (usb/high/normal/low/dead)
                              // BugFix 02282026: Was const char* pointing into freed Notecard
                              // JSON response memory — now a fixed buffer copied via strlcpy.
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
  BatteryType batteryType;    // Battery chemistry for automatic thresholds
  uint8_t nominalVoltage;     // Nominal pack voltage: 12 or 24 (0 = legacy/auto)
  
  // Voltage thresholds (set automatically from batteryType+nominalVoltage, or custom)
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
 * @param type Battery chemistry to configure for
 * @param nominalVoltage Nominal pack voltage (12 or 24). Pass 0 for legacy/auto
 *                      (legacy types LEAD_ACID_12V/LIFEPO4_12V imply 12V).
 *
 * Threshold scaling: for non-LiPo chemistries, the 12V baseline thresholds are
 * multiplied by (nominalVoltage/12). A 24V AGM bank therefore inherits 25.4V/23.6V
 * from the 12.7V/11.8V 12V values.
 */
inline void initBatteryConfig(BatteryConfig* config, BatteryType type, uint8_t nominalVoltage = 0) {
  if (!config) return;

  // Resolve nominal voltage. Legacy *_12V enums force 12V; explicit 0 also defaults to 12V.
  if (nominalVoltage == 0) {
    nominalVoltage = 12;
  }
  // LiPo is a single-cell chemistry (~3.7V); pack voltage is meaningless here.
  if (type == BATTERY_TYPE_LIPO) {
    nominalVoltage = 0;  // sentinel: don't scale
  }

  config->enabled = (type != BATTERY_TYPE_NONE);  // None = disabled by default
  config->batteryType = type;
  config->nominalVoltage = (type == BATTERY_TYPE_LIPO) ? 0 : nominalVoltage;
  config->calibrationOffset = BATTERY_DEFAULT_CALIBRATION;
  config->pollIntervalSec = BATTERY_DEFAULT_POLL_INTERVAL_SEC;
  config->trendAnalysisHours = BATTERY_DEFAULT_TREND_HOURS;
  config->alertOnLow = true;
  config->alertOnCritical = true;
  config->alertOnDeclining = true;
  config->alertOnRecovery = false;  // Usually not needed
  config->declineAlertThreshold = BATTERY_DEFAULT_DECLINE_THRESHOLD;
  config->includeInDailyReport = true;

  // Scale factor for threshold math: 24V pack -> 2.0x the 12V baseline.
  const float scale = (nominalVoltage > 0) ? (float)nominalVoltage / 12.0f : 1.0f;

  // Set thresholds based on chemistry, scaled by pack voltage where applicable.
  switch (type) {
    case BATTERY_TYPE_NONE:
      // No battery: thresholds are meaningless but populate with safe sentinels.
      config->highVoltage = 0.0f;
      config->normalVoltage = 0.0f;
      config->lowVoltage = 0.0f;
      config->criticalVoltage = 0.0f;
      config->alertOnLow = false;
      config->alertOnCritical = false;
      config->alertOnDeclining = false;
      break;

    case BATTERY_TYPE_LEAD_ACID_12V:  // legacy
    case BATTERY_TYPE_AGM:
    case BATTERY_TYPE_GEL:
    case BATTERY_TYPE_SLA:
      config->highVoltage     = 14.8f * scale;  // typical absorption ceiling
      config->normalVoltage   = LEAD_ACID_12V_NORMAL   * scale;
      config->lowVoltage      = LEAD_ACID_12V_LOW      * scale;
      config->criticalVoltage = LEAD_ACID_12V_CRITICAL * scale;
      break;

    case BATTERY_TYPE_FLOODED:
      // Flooded tolerates higher equalization voltage (~15.5V on a 12V bank).
      config->highVoltage     = 15.5f * scale;
      config->normalVoltage   = LEAD_ACID_12V_NORMAL   * scale;
      config->lowVoltage      = LEAD_ACID_12V_LOW      * scale;
      config->criticalVoltage = LEAD_ACID_12V_CRITICAL * scale;
      break;

    case BATTERY_TYPE_LIFEPO4_12V:  // legacy
    case BATTERY_TYPE_LIFEPO4:
      config->highVoltage     = 14.8f * scale;
      config->normalVoltage   = LIFEPO4_12V_NORMAL     * scale;
      config->lowVoltage      = LIFEPO4_12V_LOW        * scale;
      config->criticalVoltage = LIFEPO4_12V_CRITICAL   * scale;
      break;

    case BATTERY_TYPE_LI_ION:
      // Li-ion (NMC) 3S nominal ~11.1V, 4.2V/cell max -> 12.6V full at 3S.
      // For a 24V (7S) pack, scale accordingly.
      config->highVoltage     = 12.6f * scale;
      config->normalVoltage   = 11.4f * scale;
      config->lowVoltage      = 10.5f * scale;
      config->criticalVoltage =  9.9f * scale;
      break;

    case BATTERY_TYPE_LIPO:
      // Single cell (Notecard default).
      config->highVoltage = 4.6f;
      config->normalVoltage = 3.5f;
      config->lowVoltage = 3.2f;
      config->criticalVoltage = 3.0f;
      break;

    case BATTERY_TYPE_CUSTOM:
    default:
      // Safe defaults at the requested nominal voltage.
      config->highVoltage     = 15.0f * scale;
      config->normalVoltage   = 12.0f * scale;
      config->lowVoltage      = 11.5f * scale;
      config->criticalVoltage = 11.0f * scale;
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
