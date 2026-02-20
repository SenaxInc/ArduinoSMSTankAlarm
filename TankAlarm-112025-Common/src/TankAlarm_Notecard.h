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
  JAddStringToObject(req, "mode", "auto");
  
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
// Notecard Configuration
// ============================================================================

#endif // TANKALARM_NOTECARD_H
