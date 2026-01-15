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
// Default Configuration Values
// ============================================================================
#define BATTERY_DEFAULT_POLL_INTERVAL_SEC     300    // 5 minutes
#define BATTERY_DEFAULT_TREND_HOURS           168    // 7 days
#define BATTERY_DEFAULT_CALIBRATION           0.35f  // Schottky diode drop
#define BATTERY_DEFAULT_DECLINE_THRESHOLD     0.5f   // 0.5V weekly decline = alert

// Minimum interval between same battery alerts (1 hour)
#define BATTERY_ALARM_MIN_INTERVAL_MS         3600000UL

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
 * Get the Notecard voltage mode string for configuring thresholds.
 * Format: "usb:V1;high:V2;normal:V3;low:V4;dead:0"
 * 
 * @param config Battery configuration
 * @param buffer Output buffer for mode string
 * @param bufferSize Size of output buffer
 * @return true if successful
 */
inline bool getBatteryVoltageMode(const BatteryConfig* config, char* buffer, size_t bufferSize) {
  if (!config || !buffer || bufferSize < 64) return false;
  
  // Create custom voltage thresholds string
  // Format matches card.voltage "mode" parameter
  snprintf(buffer, bufferSize, 
           "usb:%.1f;high:%.1f;normal:%.1f;low:%.1f;dead:0",
           config->highVoltage + 0.5f,  // USB detection above high
           config->highVoltage,
           config->normalVoltage,
           config->criticalVoltage);
  
  return true;
}

/**
 * Get human-readable battery state description.
 * 
 * @param voltage Current voltage
 * @param config Battery configuration
 * @return State description string
 */
inline const char* getBatteryStateDescription(float voltage, const BatteryConfig* config) {
  if (!config) return "unknown";
  
  if (voltage >= config->highVoltage) return "charging/high";
  if (voltage >= config->normalVoltage) return "good";
  if (voltage >= config->lowVoltage) return "low";
  if (voltage >= config->criticalVoltage) return "critical";
  return "dead";
}

/**
 * Get estimated State of Charge (SOC) percentage for lead-acid battery.
 * Uses lookup table interpolation for 12V lead-acid at rest.
 * Note: This is approximate - actual SOC depends on load, temperature, age.
 * 
 * @param voltage Battery voltage (resting, no load)
 * @return Estimated SOC percentage (0-100)
 */
inline uint8_t estimateLeadAcidSOC(float voltage) {
  // Lookup table: voltage -> SOC% for 12V lead-acid at 25°C
  if (voltage >= 12.70f) return 100;
  if (voltage >= 12.50f) return 90;
  if (voltage >= 12.42f) return 80;
  if (voltage >= 12.32f) return 70;
  if (voltage >= 12.20f) return 60;
  if (voltage >= 12.06f) return 50;
  if (voltage >= 11.90f) return 40;
  if (voltage >= 11.75f) return 30;
  if (voltage >= 11.58f) return 20;
  if (voltage >= 11.31f) return 10;
  return 0;  // Below 11.31V = discharged
}

/**
 * Get estimated State of Charge (SOC) percentage for LiFePO4 battery.
 * 
 * @param voltage Battery voltage (4S configuration)
 * @return Estimated SOC percentage (0-100)
 */
inline uint8_t estimateLiFePO4SOC(float voltage) {
  // LiFePO4 has very flat discharge curve, SOC estimation less accurate
  if (voltage >= 14.40f) return 100;
  if (voltage >= 13.60f) return 90;
  if (voltage >= 13.40f) return 70;
  if (voltage >= 13.30f) return 50;
  if (voltage >= 13.20f) return 30;
  if (voltage >= 13.00f) return 20;
  if (voltage >= 12.00f) return 10;
  return 0;
}

#endif // TANKALARM_BATTERY_H
