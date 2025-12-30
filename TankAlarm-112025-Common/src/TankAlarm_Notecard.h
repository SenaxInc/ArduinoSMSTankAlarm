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

/**
 * Configure Notecard I2C speed
 * 
 * @param notecard Reference to Notecard instance
 * @param speed I2C speed in Hz (default 400000)
 */
static inline void tankalarm_setNotecardI2CSpeed(Notecard &notecard, uint32_t speed = 400000UL) {
  J *req = notecard.newRequest("card.wire");
  if (req) {
    JAddIntToObject(req, "speed", (int)speed);
    notecard.sendRequest(req);
  }
}

/**
 * Get device UUID from Notecard
 * 
 * @param notecard Reference to Notecard instance
 * @param buffer Buffer to store UUID
 * @param bufferSize Size of buffer
 * @return true if UUID retrieved successfully
 */
static inline bool tankalarm_getNotecardUUID(Notecard &notecard, char *buffer, size_t bufferSize) {
  if (!buffer || bufferSize == 0) return false;
  
  buffer[0] = '\0';  // Initialize empty
  
  J *req = notecard.newRequest("card.uuid");
  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    return false;
  }
  
  const char *uid = JGetString(rsp, "uuid");
  if (uid && strlen(uid) > 0) {
    strlcpy(buffer, uid, bufferSize);
    notecard.deleteResponse(rsp);
    return true;
  }
  
  notecard.deleteResponse(rsp);
  return false;
}

/**
 * Configure hub.set with product UID and mode
 * 
 * @param notecard Reference to Notecard instance
 * @param productUid Product UID string
 * @param mode Hub mode ("continuous", "periodic", "minimum")
 * @param inbound Inbound sync interval in minutes (0 = default)
 * @param outbound Outbound sync interval in minutes (0 = default)
 * @return true if configured successfully
 */
static inline bool tankalarm_configureHub(
    Notecard &notecard,
    const char *productUid,
    const char *mode = "continuous",
    int inbound = 0,
    int outbound = 0
) {
  J *req = notecard.newRequest("hub.set");
  if (!req) return false;
  
  JAddStringToObject(req, "product", productUid);
  JAddStringToObject(req, "mode", mode);
  
  if (inbound > 0) {
    JAddIntToObject(req, "inbound", inbound);
  }
  if (outbound > 0) {
    JAddIntToObject(req, "outbound", outbound);
  }
  
  return notecard.sendRequest(req);
}

/**
 * Get Notecard status information
 * 
 * @param notecard Reference to Notecard instance
 * @param connected Output: true if connected to Notehub
 * @param cellBars Output: Signal strength (0-4 bars)
 * @return true if status retrieved successfully
 */
static inline bool tankalarm_getNotecardStatus(
    Notecard &notecard,
    bool &connected,
    int &cellBars
) {
  connected = false;
  cellBars = 0;
  
  J *req = notecard.newRequest("card.wireless");
  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    return false;
  }
  
  J *net = JGetObject(rsp, "net");
  if (net) {
    cellBars = JGetInt(net, "bars");
  }
  
  notecard.deleteResponse(rsp);
  
  // Check hub status for connectivity
  req = notecard.newRequest("hub.status");
  rsp = notecard.requestAndResponse(req);
  if (rsp) {
    const char *status = JGetString(rsp, "status");
    connected = (status && strstr(status, "connected") != nullptr);
    notecard.deleteResponse(rsp);
  }
  
  return true;
}

#endif // TANKALARM_NOTECARD_H
