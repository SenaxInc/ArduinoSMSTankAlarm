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
