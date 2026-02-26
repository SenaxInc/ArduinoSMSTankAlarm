# Future Improvement 3.3.4 — Flash-Backed I2C Error Log

**Priority:** Low  
**Effort:** 6–10 hours  
**Risk:** Medium — flash write endurance and filesystem considerations  
**Prerequisite:** None  
**Date:** 2026-02-26  

---

## Problem Statement

Critical I2C events (bus recovery, prolonged failures, watchdog resets) are currently only tracked in:
1. **Volatile RAM counters** (`gCurrentLoopI2cErrors`, `gI2cBusRecoveryCount`) — lost on reset
2. **Serial output** — lost if not captured
3. **Daily report JSON** — includes the 24-hour counter values at report time, but counters are then reset (implemented in 1.8.6)

This creates a blind spot: if a device experiences a watchdog reset, the I2C error history that led to the reset is completely lost. The device reboots with clean counters and no record of what happened.

For field units deployed in remote locations (tank farms, agricultural sites), post-mortem analysis of failure events is critical for:
- Diagnosing intermittent I2C bus issues
- Identifying environmental factors (temperature, vibration, time of day)
- Correlating I2C failures with power state transitions
- Building evidence for hardware replacement decisions

---

## Current Data Flow

```
I2C Error Occurs
    ↓
gCurrentLoopI2cErrors++        ← RAM counter (lost on reset)
Serial.println("I2C NACK...")  ← Serial output (lost if unwatched)
    ↓
[Every 24 hours]
sendDailyReport()
  → doc["i2c_cl_err"] = gCurrentLoopI2cErrors  ← Sent to Notehub
  → gCurrentLoopI2cErrors = 0                   ← Counter reset
    ↓
[If I2C_ERROR_ALERT_THRESHOLD exceeded]
Alarm note published           ← Sent to Notehub (if Notecard available)
    ↓
[On watchdog reset or power loss]
ALL DATA LOST                  ← RAM cleared, Serial gone
```

---

## Proposed Architecture

### Ring Buffer in Flash

A fixed-size ring buffer stored in the Opta's onboard flash (QSPI or LittleFS) that records I2C events with timestamps:

```
Flash File: /i2c_events.log
┌──────────────────────────────────────────────┐
│ Header: magic (4B) | version (1B) | ptr (2B) │
├──────────────────────────────────────────────┤
│ Entry 0: timestamp(4B) | type(1B) | data(3B) │
│ Entry 1: timestamp(4B) | type(1B) | data(3B) │
│ Entry 2: ...                                  │
│ ...                                           │
│ Entry 63: (newest, if ptr wraps)              │
└──────────────────────────────────────────────┘
```

### Entry Format (8 bytes each)

```cpp
struct I2CEventEntry {
  uint32_t epoch;        // Unix timestamp (4 bytes)
  uint8_t  eventType;    // Event type enum (1 byte)
  uint8_t  data1;        // Event-specific data byte 1
  uint8_t  data2;        // Event-specific data byte 2  
  uint8_t  data3;        // Event-specific data byte 3
};
// sizeof(I2CEventEntry) = 8 bytes
```

### Event Types

```cpp
enum I2CEventType : uint8_t {
  I2C_EVT_BUS_RECOVERY      = 0x01,  // data: trigger type, recovery count (low byte)
  I2C_EVT_NOTECARD_FAILURE   = 0x02,  // data: failure count, last error code
  I2C_EVT_SENSOR_ALL_FAIL    = 0x03,  // data: failed channel count, backoff multiplier
  I2C_EVT_DUAL_FAILURE       = 0x04,  // data: consecutive fail loops (high/low bytes)
  I2C_EVT_ERROR_THRESHOLD    = 0x05,  // data: error count (16-bit, daily)
  I2C_EVT_WATCHDOG_RESET     = 0x06,  // data: none (detected on boot)
  I2C_EVT_ADDRESS_CONFLICT   = 0x07,  // data: conflicting address
  I2C_EVT_SCAN_MISSING       = 0x08,  // data: missing device address
};
```

### Storage Budget

| Parameter | Value |
|-----------|-------|
| Entry size | 8 bytes |
| Ring buffer capacity | 64 entries |
| Header size | 8 bytes |
| **Total file size** | **520 bytes** |

At 64 entries, assuming 1 event per hour during degraded operation, the log holds ~2.7 days of history. Under normal operation (0 events/day), it's effectively infinite.

---

## Implementation

### File: `TankAlarm_I2CLog.h`

```cpp
#ifndef TANKALARM_I2C_LOG_H
#define TANKALARM_I2C_LOG_H

#include <Arduino.h>
#include "TankAlarm_Platform.h"  // For filesystem macros

#define I2C_LOG_FILE "/i2c_events.log"
#define I2C_LOG_MAGIC 0x49324345  // "I2CE"
#define I2C_LOG_VERSION 1
#define I2C_LOG_MAX_ENTRIES 64

struct I2CEventEntry {
  uint32_t epoch;
  uint8_t  eventType;
  uint8_t  data1;
  uint8_t  data2;
  uint8_t  data3;
};

struct I2CLogHeader {
  uint32_t magic;
  uint8_t  version;
  uint16_t writePtr;    // Next entry to write (ring pointer)
  uint8_t  reserved;    // Padding for alignment
};

// ---- API ----

/**
 * Initialize the I2C event log. Creates the file if it doesn't exist.
 * Should be called once in setup() after filesystem initialization.
 */
static bool i2cLogInit() {
  if (!isStorageAvailable()) return false;
  
  File f = FILESYSTEM.open(I2C_LOG_FILE, "r");
  if (f) {
    I2CLogHeader hdr;
    if (f.read((uint8_t*)&hdr, sizeof(hdr)) == sizeof(hdr) &&
        hdr.magic == I2C_LOG_MAGIC && hdr.version == I2C_LOG_VERSION) {
      f.close();
      return true;  // Valid log file exists
    }
    f.close();
  }
  
  // Create new log file
  f = FILESYSTEM.open(I2C_LOG_FILE, "w");
  if (!f) return false;
  
  I2CLogHeader hdr = { I2C_LOG_MAGIC, I2C_LOG_VERSION, 0, 0 };
  f.write((uint8_t*)&hdr, sizeof(hdr));
  
  // Zero-fill entry slots
  I2CEventEntry empty = {0, 0, 0, 0, 0};
  for (uint16_t i = 0; i < I2C_LOG_MAX_ENTRIES; i++) {
    f.write((uint8_t*)&empty, sizeof(empty));
  }
  
  f.close();
  return true;
}

/**
 * Write an I2C event to the ring buffer log.
 */
static bool i2cLogEvent(uint32_t epoch, uint8_t eventType,
                         uint8_t d1 = 0, uint8_t d2 = 0, uint8_t d3 = 0) {
  if (!isStorageAvailable()) return false;
  
  File f = FILESYSTEM.open(I2C_LOG_FILE, "r+");
  if (!f) return false;
  
  // Read header
  I2CLogHeader hdr;
  f.read((uint8_t*)&hdr, sizeof(hdr));
  if (hdr.magic != I2C_LOG_MAGIC) {
    f.close();
    return false;
  }
  
  // Write entry at current pointer position
  uint32_t offset = sizeof(I2CLogHeader) + (hdr.writePtr * sizeof(I2CEventEntry));
  f.seek(offset);
  
  I2CEventEntry entry = { epoch, eventType, d1, d2, d3 };
  f.write((uint8_t*)&entry, sizeof(entry));
  
  // Update write pointer (wrap around)
  hdr.writePtr = (hdr.writePtr + 1) % I2C_LOG_MAX_ENTRIES;
  f.seek(0);
  f.write((uint8_t*)&hdr, sizeof(hdr));
  
  f.close();
  return true;
}

/**
 * Read all valid entries from the log (oldest first).
 * Callback receives each entry for processing.
 */
static uint16_t i2cLogRead(void (*callback)(const I2CEventEntry& entry)) {
  if (!isStorageAvailable()) return 0;
  
  File f = FILESYSTEM.open(I2C_LOG_FILE, "r");
  if (!f) return 0;
  
  I2CLogHeader hdr;
  f.read((uint8_t*)&hdr, sizeof(hdr));
  if (hdr.magic != I2C_LOG_MAGIC) {
    f.close();
    return 0;
  }
  
  uint16_t count = 0;
  // Read from oldest (writePtr) to newest (writePtr - 1)
  for (uint16_t i = 0; i < I2C_LOG_MAX_ENTRIES; i++) {
    uint16_t idx = (hdr.writePtr + i) % I2C_LOG_MAX_ENTRIES;
    uint32_t offset = sizeof(I2CLogHeader) + (idx * sizeof(I2CEventEntry));
    f.seek(offset);
    
    I2CEventEntry entry;
    f.read((uint8_t*)&entry, sizeof(entry));
    
    if (entry.epoch > 0 && entry.eventType > 0) {
      callback(entry);
      count++;
    }
  }
  
  f.close();
  return count;
}

#endif // TANKALARM_I2C_LOG_H
```

### Integration Points

```cpp
// In recoverI2CBus():
gI2cBusRecoveryCount++;
i2cLogEvent(currentEpoch(), I2C_EVT_BUS_RECOVERY, 
            (uint8_t)trigger, (uint8_t)(gI2cBusRecoveryCount & 0xFF));

// In checkNotecardHealth() when failure threshold reached:
i2cLogEvent(currentEpoch(), I2C_EVT_NOTECARD_FAILURE,
            (uint8_t)gNotecardFailureCount);

// In sensor-only failure path:
i2cLogEvent(currentEpoch(), I2C_EVT_SENSOR_ALL_FAIL,
            failedCount, sensorRecoveryBackoff);

// In sendDailyReport() when error alert fires:
i2cLogEvent(currentEpoch(), I2C_EVT_ERROR_THRESHOLD,
            (uint8_t)(gCurrentLoopI2cErrors >> 8),
            (uint8_t)(gCurrentLoopI2cErrors & 0xFF));

// On boot, detect watchdog reset:
#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) {
    i2cLogEvent(currentEpoch(), I2C_EVT_WATCHDOG_RESET);
    __HAL_RCC_CLEAR_RESET_FLAGS();
  }
#endif
```

---

## Flash Write Endurance

### QSPI Flash Specifications

The Arduino Opta uses a QSPI NOR flash (typically IS25LP128F or similar):
- **Write endurance:** 100,000 erase/program cycles per sector
- **Sector size:** 4KB (smallest erasable unit)

### Write Frequency Analysis

| Scenario | Events/Day | Writes/Day |
|----------|-----------|-----------|
| Normal operation (no errors) | 0 | 0 |
| Occasional I2C glitch | 1–5 | 1–5 |
| Degraded bus (sensor failures) | 10–50 | 10–50 |
| Severe failure (constant recovery) | 100+ | 100+ |

At worst case (100 writes/day), the flash endures:
```
100,000 cycles / 100 writes/day = 1,000 days = 2.7 years
```

However, LittleFS uses wear leveling, which distributes writes across multiple sectors. With a 16MB flash chip and wear leveling, effective endurance is much higher.

**Mitigation:** Rate-limit log writes to at most 1 per minute:

```cpp
static bool i2cLogEvent(...) {
  static unsigned long lastLogWrite = 0;
  if (millis() - lastLogWrite < 60000UL) return false;  // Rate limit
  lastLogWrite = millis();
  // ... proceed with write ...
}
```

---

## Retrieval Methods

### Via Serial Console

```cpp
// Serial command to dump I2C event log (accessible via I2C Utility sketch)
void dumpI2CLog() {
  Serial.println(F("=== I2C Event Log ==="));
  i2cLogRead([](const I2CEventEntry& e) {
    Serial.print(e.epoch);
    Serial.print(F(" type=0x"));
    Serial.print(e.eventType, HEX);
    Serial.print(F(" d1="));
    Serial.print(e.data1);
    Serial.print(F(" d2="));
    Serial.print(e.data2);
    Serial.print(F(" d3="));
    Serial.println(e.data3);
  });
  Serial.println(F("=== End Log ==="));
}
```

### Via Notecard (Daily Upload)

Optionally include the last N log entries in the daily report:

```cpp
// In sendDailyReport():
JsonArray logEntries = doc["i2c_log"].to<JsonArray>();
uint8_t logCount = 0;
i2cLogRead([&](const I2CEventEntry& e) {
  if (logCount < 10 && e.epoch > lastDailyReportEpoch) {  // Only new entries
    JsonObject entry = logEntries.add<JsonObject>();
    entry["t"] = e.epoch;
    entry["typ"] = e.eventType;
    entry["d"] = (uint32_t)(e.data1 << 16 | e.data2 << 8 | e.data3);
    logCount++;
  }
});
```

### Via Server Web UI (if applicable)

The Server could expose a `/api/i2c-log` endpoint that reads the log file and returns it as JSON.

---

## Testing Plan

| Test | Procedure | Pass Criteria |
|------|-----------|---------------|
| Log initialization | Boot, check file created | `/i2c_events.log` exists, 520 bytes |
| Event logging | Trigger bus recovery | Entry appears with correct type/data |
| Ring buffer wrap | Write 65+ entries | Oldest entry overwritten, newest preserved |
| Read order | Dump log after wrap | Entries appear oldest → newest |
| Watchdog reset detection | Trigger watchdog timeout | `I2C_EVT_WATCHDOG_RESET` logged on next boot |
| Write rate limit | Trigger 10 events in 1 second | Only 1 entry written |
| Flash corruption recovery | Corrupt log file header | Log re-initialized on next boot |
| Serial dump | Call dumpI2CLog() | All entries printed correctly |

---

## Files Affected

| File | Change |
|------|--------|
| `TankAlarm_I2CLog.h` (new) | ~120 lines — ring buffer in flash |
| Client .ino | +10 lines — init call, event logging calls |
| Optionally: I2C Utility .ino | +20 lines — dump command |
| Optionally: Client daily report | +15 lines — log entries in daily report |

---

## Alternatives Considered

### Option A: Log to Notecard environment variable
Store a compact event summary in the Notecard's `env.set` variable. Limited to ~256 bytes but survives device resets.

### Option B: Log to dedicated Notehub notefile
Write each event as an individual note. Requires Notecard to be available — doesn't help if the Notecard is the failing device.

### Option C: Log to external EEPROM
Use I²C EEPROM (AT24C256). Ironic: using I2C to log I2C errors. But the EEPROM is on a separate write path and may survive bus issues that affect the Notecard/A0602.

**Recommendation:** Flash-based ring buffer (Option described above) is the best balance of reliability, simplicity, and storage capacity.
