# Future Improvement 3.2.4 — I2C Recovery Event Logging via Notecard

**Priority:** Medium  
**Effort:** 2–3 hours  
**Risk:** Very Low — adds a diagnostic note, no impact on recovery logic  
**Prerequisite:** None  
**Date:** 2026-02-26  

---

## Problem Statement

When `recoverI2CBus()` fires, the event is logged to Serial and the `gI2cBusRecoveryCount` counter is incremented. However:

1. **Serial logs are volatile** — they're lost if nobody is watching the serial console
2. **The counter resets daily** — after the daily report, `gI2cBusRecoveryCount` resets to 0, so only the count within the current 24-hour window is known
3. **No event-level granularity** — the daily report says "3 recoveries happened today" but not *when* they happened or *why* (Notecard failure, sensor-only failure, or dual failure)
4. **No fleet visibility** — operators can't see recovery events in the Notehub dashboard without parsing daily reports

Publishing a lightweight diagnostic note at the time of each recovery would provide real-time visibility into I2C bus health across the entire fleet.

---

## Current Recovery Triggers

`recoverI2CBus()` is called from 4 code paths:

| Trigger | Location | Condition | Severity |
|---------|----------|-----------|----------|
| Notecard sustained failure | `checkNotecardHealth()` ~2451 | `gNotecardFailureCount == I2C_NOTECARD_RECOVERY_THRESHOLD` (10) | High |
| Notecard response failure | `checkNotecardHealth()` ~2467 | Same threshold, response-null branch | High |
| Sensor-only failure | loop() ~1175 | `consecutiveSensorOnlyFailLoops >= threshold × backoff` | Medium |
| Dual failure (prolonged) | loop() ~1133 | `consecutiveTotalI2cFailLoops >= I2C_DUAL_FAIL_RECOVERY_LOOPS` (30) | Critical |

Each trigger represents a different failure mode and should be identified in the diagnostic note.

---

## Implementation

### Diagnostic Note File

Use a separate notefile from alarms to avoid cluttering the alarm queue:

```cpp
// In defines section:
#define DIAG_FILE DIAG_OUTBOX_FILE  // "diag.qo" — or define new notefile
```

Or reuse the health notefile since this is diagnostic data:

```cpp
#define I2C_DIAG_FILE HEALTH_OUTBOX_FILE  // "health.qo"
```

**Recommendation:** Use a new `"diag.qo"` notefile. This keeps diagnostics separate from periodic health telemetry and allows different Notehub routing rules (e.g., diagnostic events go to an engineering dashboard, health data goes to fleet monitoring).

### Add Notefile Name to Common Header

```cpp
// In TankAlarm_Common.h:
#define DIAG_OUTBOX_FILE    "diag.qo"
```

### Modified `recoverI2CBus()` with Logging

```cpp
// New enum for recovery trigger type
enum I2CRecoveryTrigger {
  I2C_RECOVERY_NOTECARD_FAILURE = 0,
  I2C_RECOVERY_SENSOR_ONLY = 1,
  I2C_RECOVERY_DUAL_FAILURE = 2,
  I2C_RECOVERY_MANUAL = 3  // For future I2C Utility use
};

static void recoverI2CBus(I2CRecoveryTrigger trigger = I2C_RECOVERY_MANUAL) {
  // ... existing recovery logic (DFU guard, watchdog kick, SCL toggle) ...
  
  gI2cBusRecoveryCount++;
  
  // Publish diagnostic event if Notecard is available
  // (Don't publish for Notecard-failure triggers — the Notecard is down!)
  if (gNotecardAvailable && trigger != I2C_RECOVERY_NOTECARD_FAILURE) {
    JsonDocument doc;
    doc["c"] = gDeviceUID;
    doc["s"] = gConfig.siteName;
    doc["ev"] = "i2c-recovery";
    doc["trigger"] = (uint8_t)trigger;
    doc["count"] = gI2cBusRecoveryCount;
    doc["i2c_errs"] = gCurrentLoopI2cErrors;
    doc["t"] = currentEpoch();
    publishNote(DIAG_FILE, doc, false);  // false = don't force immediate sync
  }
  
  Serial.print(F("I2C bus recovery complete (count="));
  Serial.print(gI2cBusRecoveryCount);
  Serial.println(F(")"));
}
```

### Update Call Sites with Trigger Type

```cpp
// In checkNotecardHealth():
recoverI2CBus(I2C_RECOVERY_NOTECARD_FAILURE);  // Can't send note — Notecard is down

// In loop() sensor-only path:
recoverI2CBus(I2C_RECOVERY_SENSOR_ONLY);       // Notecard OK, can send note

// In loop() dual-failure path:
recoverI2CBus(I2C_RECOVERY_DUAL_FAILURE);       // Both down, can't send note
```

### Trigger Type Interpretation

| Value | Name | Description | Note Published? |
|-------|------|-------------|----------------|
| 0 | `NOTECARD_FAILURE` | Notecard not responding after threshold | No (Notecard unavailable) |
| 1 | `SENSOR_ONLY` | All current-loop sensors failing, Notecard OK | Yes |
| 2 | `DUAL_FAILURE` | Both Notecard and sensors failing | No (Notecard unavailable) |
| 3 | `MANUAL` | Triggered by I2C Utility or future API | Depends |

**Key insight:** Only `SENSOR_ONLY` recoveries will produce a diagnostic note because the Notecard must be available to publish. For Notecard-failure and dual-failure recoveries, the event is captured in the daily report's `gI2cBusRecoveryCount` counter instead.

---

## Diagnostic Note Payload

```json
{
  "c": "dev:864475XXXXXXXXX",
  "s": "Farm Site Alpha",
  "ev": "i2c-recovery",
  "trigger": 1,
  "count": 3,
  "i2c_errs": 27,
  "t": 1772121600
}
```

| Field | Type | Description |
|-------|------|-------------|
| `c` | string | Device UID |
| `s` | string | Site name |
| `ev` | string | Event type identifier |
| `trigger` | uint8 | Recovery trigger enum value |
| `count` | uint32 | Cumulative recovery count (since last daily reset) |
| `i2c_errs` | uint32 | Current I2C error count at time of recovery |
| `t` | double | Epoch timestamp |

**Payload size:** ~120 bytes. At most a few per day — negligible bandwidth impact.

---

## Notehub Routing Example

Create a Notehub route to forward diagnostic events to a monitoring dashboard:

```json
{
  "type": "http",
  "url": "https://monitoring.example.com/api/i2c-events",
  "filter": {
    "notefile": "diag.qo",
    "body.ev": "i2c-recovery"
  },
  "transform": {
    "device": "{{body.c}}",
    "site": "{{body.s}}",
    "trigger": "{{body.trigger}}",
    "count": "{{body.count}}",
    "timestamp": "{{body.t}}"
  }
}
```

---

## Testing Plan

| Test | Procedure | Pass Criteria |
|------|-----------|---------------|
| Sensor-only recovery note | Disconnect A0602, wait for recovery trigger | `diag.qo` note appears in Notehub with trigger=1 |
| Notecard failure — no note | Disconnect Notecard, wait for recovery | No `diag.qo` note (expected — Notecard is down) |
| Recovery count in note | Trigger multiple recoveries | `count` field increments correctly |
| Error count in note | Generate I2C errors, then trigger recovery | `i2c_errs` field matches `gCurrentLoopI2cErrors` |
| Payload size | Check note size in Notehub | < 200 bytes |

---

## Files Affected

| File | Change |
|------|--------|
| Client .ino | Modify `recoverI2CBus()` signature, add note publishing, update call sites (~20 lines) |
| `TankAlarm_Common.h` | +1 `#define` (`DIAG_OUTBOX_FILE`) |

---

## Future Extension

This diagnostic note system can be extended to log other events:
- Configuration changes applied via Notecard
- DFU firmware update start/complete
- Power state transitions
- Watchdog reset recovery (if detected)

All would use the same `diag.qo` notefile with different `ev` values, creating a unified device event log in Notehub.
