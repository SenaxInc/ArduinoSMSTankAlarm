# Critical Bug Fixes Applied - TankAlarm 112025

## Date: November 11, 2025

This document summarizes the critical bugs that have been fixed in the 112025 server and client code.

---

## ✅ Bug #1: HTTP Response Status Code (Server)

**File:** `TankAlarm-112025-Server-BluesOpta.ino`

**Issue:** The `respondStatus()` function was sending incomplete HTTP response headers for non-200 status codes, causing malformed responses.

**Original Code:**
```cpp
client.println(status == 200 ? F(" OK") : "");
```

**Fixed Code:**
```cpp
if (status == 200) {
  client.println(F("OK"));
} else if (status == 400) {
  client.println(F("Bad Request"));
} else if (status == 404) {
  client.println(F("Not Found"));
} else if (status == 500) {
  client.println(F("Internal Server Error"));
} else {
  client.println(F("Error"));
}
```

**Impact:** HTTP clients will now receive proper status line responses for all error conditions.

---

## ✅ Bug #6: Current Loop Parameter Type (Client)

**File:** `TankAlarm-112025-Client-BluesOpta.ino`

**Issue:** Function `readCurrentLoopMilliamps()` used `uint8_t` parameter but compared it with negative value (-1), which would never be true.

**Original Code:**
```cpp
static float readCurrentLoopMilliamps(uint8_t channel) {
  if (channel < 0) {  // This would never be true!
    return -1.0f;
  }
```

**Fixed Code:**
```cpp
static float readCurrentLoopMilliamps(int16_t channel) {
  if (channel < 0) {
    return -1.0f;
  }
  Wire.write((uint8_t)channel);  // Cast when passing to Wire
```

**Impact:** The function now properly handles disabled channels (value -1) and prevents attempting to read from invalid channels.

---

## ✅ Bug #5: Division by Zero Protection (Client)

**File:** `TankAlarm-112025-Client-BluesOpta.ino`

**Issue:** Multiple locations performed division or calculations using `cfg.heightInches` without validating it wasn't zero or very small, risking division by zero or NaN results.

**Locations Fixed:**
1. SENSOR_ANALOG case in `readTankSensor()`
2. SENSOR_CURRENT_LOOP case in `readTankSensor()`
3. Percentage calculation in `sendTelemetry()`
4. Percentage calculation in `sendDailyReport()`

**Added Validation:**
```cpp
if (cfg.heightInches < 0.1f) {
  return 0.0f;
}
```

**Improved Threshold:**
Changed percentage calculation check from `> 0.01f` to `> 0.1f` for better safety margin.

**Impact:** Prevents NaN values and division by zero errors when tank height is misconfigured.

---

## ✅ Bug #2: Buffer Overflow Validation (Server)

**File:** `TankAlarm-112025-Server-BluesOpta.ino`

**Issue:** `cacheClientConfigFromBuffer()` used `memcpy` without pre-validating buffer length, potentially allowing buffer overflow.

**Original Code:**
```cpp
static void cacheClientConfigFromBuffer(const char *clientUid, const char *buffer) {
  if (!clientUid || !buffer) {
    return;
  }
  // ... directly proceeded to strlen and memcpy
```

**Fixed Code:**
```cpp
static void cacheClientConfigFromBuffer(const char *clientUid, const char *buffer) {
  if (!clientUid || !buffer) {
    return;
  }

  // Validate buffer length before processing
  size_t bufferLen = strlen(buffer);
  if (bufferLen == 0 || bufferLen >= sizeof(((ClientConfigSnapshot*)0)->payload)) {
    Serial.println(F("Config buffer invalid size"));
    return;
  }
  // ... rest of function uses validated bufferLen
```

**Impact:** Prevents buffer overflow attacks or crashes from oversized configuration payloads.

---

## ✅ Bug #4: Pin Comparison Safety (Client)

**File:** `TankAlarm-112025-Client-BluesOpta.ino`

**Issue:** Pin and channel comparisons used implicit type conversions that could cause issues with sentinel values (-1).

**Original Code:**
```cpp
int pin = cfg.primaryPin >= 0 ? cfg.primaryPin : (2 + idx);
int channel = cfg.primaryPin >= 0 ? cfg.primaryPin : 0;
int channel = cfg.currentLoopChannel >= 0 ? cfg.currentLoopChannel : 0;
```

**Fixed Code:**
```cpp
// Digital pin with explicit bounds
int pin = (cfg.primaryPin >= 0 && cfg.primaryPin < 255) ? cfg.primaryPin : (2 + idx);

// Analog channel with hardware-specific bounds (A0602 has 0-7)
int channel = (cfg.primaryPin >= 0 && cfg.primaryPin < 8) ? cfg.primaryPin : 0;

// Current loop channel with bounds
int16_t channel = (cfg.currentLoopChannel >= 0 && cfg.currentLoopChannel < 8) ? cfg.currentLoopChannel : 0;
```

**Impact:** Prevents using out-of-range pin/channel numbers that could cause hardware issues or undefined behavior.

---

## Summary

All 5 critical bugs have been successfully fixed:

1. ✅ **Server:** Fixed malformed HTTP responses
2. ✅ **Client:** Fixed current loop type mismatch
3. ✅ **Client:** Added division by zero protection
4. ✅ **Server:** Added buffer overflow validation
5. ✅ **Client:** Improved pin/channel bounds checking

## Testing Recommendations

Before deployment, test the following scenarios:

1. **HTTP Error Responses:** Send invalid requests and verify proper 400/404 responses
2. **Current Loop Sensors:** Test with disabled channels (currentLoopChannel = -1)
3. **Zero Height Configuration:** Try configuring tank with heightInches = 0
4. **Large Config Payloads:** Send oversized configuration JSON to server
5. **Invalid Pin Numbers:** Configure sensors with out-of-range pins

## Next Steps

Consider addressing the moderate and minor issues documented in `CODE_REVIEW_112025.md`, particularly:
- Add authentication to web interface (Security concern)
- Implement alarm hysteresis (prevents false alarms)
- Add watchdog timer (reliability)
- Implement rate limiting for alerts (prevents spam)
