/**
 * TankAlarm_Diagnostics.h
 * 
 * Shared diagnostics utilities for TankAlarm 112025 components.
 * Provides platform-agnostic heap measurement and health telemetry helpers.
 * Consumed by Client, Server, Viewer, and I2C Utility sketches.
 * 
 * Copyright (c) 2025 Senax Inc. All rights reserved.
 */

#ifndef TANKALARM_DIAGNOSTICS_H
#define TANKALARM_DIAGNOSTICS_H

#include <Arduino.h>
#include "TankAlarm_Platform.h"

// ============================================================================
// Free Heap Measurement
// ============================================================================

/**
 * Get current free heap bytes for field diagnostics.
 *
 * On Mbed/Opta: uses mbed_stats_heap_get(). Requires MBED_HEAP_STATS_ENABLED
 * in mbed_app.json for non-zero values; otherwise returns 0.
 * On AVR/STM32 without Mbed: returns 0 (no portable API).
 *
 * @return Free heap in bytes, or 0 if unavailable.
 */
static inline uint32_t tankalarm_freeRam() {
#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  mbed_stats_heap_t heapStats;
  mbed_stats_heap_get(&heapStats);
  return (heapStats.reserved_size > heapStats.current_size)
           ? (uint32_t)(heapStats.reserved_size - heapStats.current_size)
           : 0U;
#else
  return 0U;
#endif
}

// ============================================================================
// Heap Free Reporting (Serial)
// ============================================================================

/**
 * Print heap stats to Serial.  Safe to call on any platform.
 */
static inline void tankalarm_printHeapStats() {
#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  Serial.print(F("Heap free: "));
  Serial.print(tankalarm_freeRam());
  Serial.println(F("B"));
#else
  Serial.println(F("Heap stats: not available on this platform"));
#endif
}

// ============================================================================
// Health Telemetry Struct (optional, feature-flagged)
// ============================================================================

/**
 * Lightweight health snapshot for periodic telemetry.
 * Populated by tankalarm_collectHealthSnapshot() and included in
 * health notes when TANKALARM_HEALTH_TELEMETRY_ENABLED is defined.
 *
 * All fields are zero-initialized; callers cherry-pick what applies.
 */
struct TankAlarmHealthSnapshot {
  uint32_t heapFreeBytes;          // Current free heap
  uint32_t heapMinFreeBytes;       // Low-watermark (tracked externally)
  uint32_t uptimeSeconds;          // millis()/1000
  uint32_t watchdogResetCount;     // Incremented if watchdog-triggered boot detected
  uint32_t notecardCommErrors;     // Cumulative Notecard I²C/serial errors
  uint32_t storageWriteErrors;     // Cumulative flash write failures
  bool     storageAvailable;       // LittleFS mounted successfully
};

/**
 * Collect a point-in-time health snapshot (heap + uptime).
 * Notecard comm and storage counters must be maintained by the caller
 * and merged into the snapshot after this call.
 */
static inline TankAlarmHealthSnapshot tankalarm_collectHealthSnapshot() {
  TankAlarmHealthSnapshot snap = {};
  snap.heapFreeBytes = tankalarm_freeRam();
  snap.uptimeSeconds = (uint32_t)(millis() / 1000UL);
  return snap;
}

#endif // TANKALARM_DIAGNOSTICS_H
