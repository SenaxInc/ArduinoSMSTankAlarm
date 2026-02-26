/**
 * TankAlarm_Notecard.h
 * 
 * Notecard helper functions for TankAlarm 112025 components.
 * Provides common patterns for time sync, UUID retrieval, etc.
 * 
 * Copyright (c) 2025 Senax Inc. All rights reserved.
 */

#ifndef TANKALARM_NOTECARD_H
#define TANKALARM_NOTECARD_H

#include <Arduino.h>
#include <Notecard.h>

// ============================================================================
// Time Synchronization
// ============================================================================

/**
 * Ensure time is synchronized from Notecard
 * Re-syncs every 6 hours or if never synced
 * 
 * @param notecard Reference to Notecard instance
 * @param lastSyncedEpoch Reference to stored epoch (updated on sync)
 * @param lastSyncMillis Reference to stored millis (updated on sync)
 * @param forceSync If true, sync regardless of elapsed time
 */
static inline void tankalarm_ensureTimeSync(
    Notecard &notecard,
    double &lastSyncedEpoch,
    unsigned long &lastSyncMillis,
    bool forceSync = false
) {
  // Check if sync needed (every 6 hours or never synced)
  const unsigned long SYNC_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
  
  if (!forceSync && lastSyncedEpoch > 0.0 && (millis() - lastSyncMillis) < SYNC_INTERVAL_MS) {
    return;  // No sync needed
  }
  
  J *req = notecard.newRequest("card.time");
  if (!req) {
    return;
  }
  
  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    return;
  }
  
  // Check for error response (time not yet available from cellular)
  const char *err = JGetString(rsp, "err");
  if (err && strlen(err) > 0) {
    notecard.deleteResponse(rsp);
    return;
  }
  
  double time = JGetNumber(rsp, "time");
  if (time > 0) {
    lastSyncedEpoch = time;
    lastSyncMillis = millis();
  }
  notecard.deleteResponse(rsp);
}

/**
 * Get current Unix epoch timestamp based on last sync
 * 
 * @param lastSyncedEpoch Stored epoch from last sync
 * @param lastSyncMillis Stored millis from last sync
 * @return Current epoch, or 0.0 if never synced
 */
static inline double tankalarm_currentEpoch(double lastSyncedEpoch, unsigned long lastSyncMillis) {
  if (lastSyncedEpoch <= 0.0) {
    return 0.0;
  }
  unsigned long delta = millis() - lastSyncMillis;
  return lastSyncedEpoch + (double)delta / 1000.0;
}

// ============================================================================
// Notecard I2C Binding
// ============================================================================

/**
 * Re-establish the Notecard's I2C binding after bus recovery or Wire reinit.
 *
 * AUDIT RESULT (2026-02-26): The Blues note-arduino library's
 * `Notecard::begin(uint32_t)` calls `make_note_i2c(Wire)`, which uses a
 * singleton guard (`if (!note_i2c)`) — so the NoteI2c_Arduino object is
 * only allocated ONCE, regardless of how many times `begin()` is called.
 * Subsequent calls re-register the I2C function-pointer callbacks
 * (NoteSetFnI2CDefault) and re-set the address/MTU, but do NOT leak memory.
 *
 * Safe to call:
 *   - After recoverI2CBus() (which calls Wire.end() + Wire.begin())
 *   - After reinitializeHardware() (which calls Wire.end() + Wire.begin())
 *   - When transitioning back to online mode after an outage
 *
 * NOT needed for:
 *   - Periodic health checks when the bus hasn't been reset
 *   - Normal Notecard request/response cycles
 *
 * @param notecard  Reference to the Notecard instance
 * @param i2cAddr   I2C address (default NOTECARD_I2C_ADDRESS from TankAlarm_Common.h)
 */
static inline void tankalarm_ensureNotecardBinding(
    Notecard &notecard,
    uint32_t i2cAddr = NOTECARD_I2C_ADDRESS
) {
  notecard.begin(i2cAddr);
}

// ============================================================================
// Notecard Configuration
// ============================================================================

#endif // TANKALARM_NOTECARD_H
