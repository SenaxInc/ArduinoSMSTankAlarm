/**
 * TankAlarm_Utils.h
 * 
 * Utility functions for TankAlarm 112025 components.
 * 
 * Copyright (c) 2025 Senax Inc. All rights reserved.
 */

#ifndef TANKALARM_UTILS_H
#define TANKALARM_UTILS_H

#include <Arduino.h>
#include <math.h>
#include <string.h>

// ============================================================================
// Sensor Unit Conversion
// ============================================================================

// Unit conversion constants for 4-20mA sensors
// constexpr gives type-safety, scoping, and debugger visibility vs bare #define
static constexpr float METERS_TO_INCHES      = 39.3701f;  // 1 meter = 39.3701 inches
static constexpr float CENTIMETERS_TO_INCHES = 0.393701f; // 1 centimeter = 0.393701 inches
static constexpr float FEET_TO_INCHES        = 12.0f;     // 1 foot = 12 inches

// Pressure-to-height conversion factors (for water at standard conditions)
static constexpr float PSI_TO_INCHES_WATER   = 27.68f;    // 1 PSI = 27.68 inches of water column
static constexpr float BAR_TO_INCHES_WATER   = 401.5f;    // 1 bar = 401.5 inches of water
static constexpr float KPA_TO_INCHES_WATER   = 4.015f;    // 1 kPa = 4.015 inches of water
static constexpr float MBAR_TO_INCHES_WATER  = 0.4015f;   // 1 mbar = 0.4015 inches of water

enum class PressureUnit : uint8_t {
  PSI,
  BAR,
  KPA,
  MBAR,
  IN_H2O
};

enum class DistanceUnit : uint8_t {
  INCH,
  METER,
  CENTIMETER,
  FOOT
};

// Conversion helpers (defined in header to avoid Arduino preprocessor prototype issues)
inline float getPressureConversionFactor(PressureUnit unit) {
  switch (unit) {
    case PressureUnit::BAR:    return BAR_TO_INCHES_WATER;
    case PressureUnit::KPA:    return KPA_TO_INCHES_WATER;
    case PressureUnit::MBAR:   return MBAR_TO_INCHES_WATER;
    case PressureUnit::IN_H2O: return 1.0f;
    case PressureUnit::PSI:
    default:                   return PSI_TO_INCHES_WATER;
  }
}

inline float getDistanceConversionFactor(DistanceUnit unit) {
  switch (unit) {
    case DistanceUnit::METER:      return METERS_TO_INCHES;
    case DistanceUnit::CENTIMETER: return CENTIMETERS_TO_INCHES;
    case DistanceUnit::FOOT:       return FEET_TO_INCHES;
    case DistanceUnit::INCH:
    default:                       return 1.0f;
  }
}

// ============================================================================
// strlcpy - Safe String Copy
// Provided for non-Mbed platforms that don't have it
// ============================================================================
#if !defined(ARDUINO_ARCH_MBED) && !defined(strlcpy)
/**
 * Copy string with size limit (BSD-style safe strcpy)
 * @param dst Destination buffer
 * @param src Source string
 * @param size Size of destination buffer
 * @return Length of src (may exceed size if truncated)
 */
static inline size_t strlcpy(char *dst, const char *src, size_t size) {
  if (!dst || !src || size == 0) {
    return 0;
  }
  size_t len = strlen(src);
  size_t copyLen = (len >= size) ? (size - 1) : len;
  memcpy(dst, src, copyLen);
  dst[copyLen] = '\0';
  return len;
}
#endif

// ============================================================================
// Numeric Utilities
// ============================================================================

/**
 * Round float to specified decimal places
 * @param val Value to round
 * @param decimals Number of decimal places
 * @return Rounded value
 */
static inline float tankalarm_roundTo(float val, int decimals) {
  float multiplier = pow(10, decimals);
  return round(val * multiplier) / multiplier;
}

// ============================================================================
// Time/Scheduling Utilities
// ============================================================================

/**
 * Compute next aligned epoch for scheduled tasks
 * Used to schedule daily reports, summaries, etc. at specific times
 * 
 * @param epoch Current Unix epoch timestamp
 * @param baseHour Hour of day to align to (0-23)
 * @param intervalSeconds Interval between occurrences
 * @return Next aligned epoch timestamp, or 0.0 on error
 */
static inline double tankalarm_computeNextAlignedEpoch(double epoch, uint8_t baseHour, uint32_t intervalSeconds) {
  if (epoch <= 0.0 || intervalSeconds == 0) {
    return 0.0;
  }
  // Start of day + base hour
  double aligned = floor(epoch / 86400.0) * 86400.0 + (double)baseHour * 3600.0;
  // Find next occurrence after current epoch
  while (aligned <= epoch) {
    aligned += (double)intervalSeconds;
  }
  return aligned;
}

#endif // TANKALARM_UTILS_H
