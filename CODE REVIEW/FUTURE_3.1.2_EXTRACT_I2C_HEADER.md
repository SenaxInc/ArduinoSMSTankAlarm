# Future Improvement 3.1.2 — Extract I2C Operations into `TankAlarm_I2C.h`

**Priority:** High  
**Effort:** 8–16 hours (extraction + compilation testing across 4 sketches)  
**Risk:** Medium — structural change touching core I2C paths  
**Prerequisite:** Audit of 3.1.1 (notecard.begin idempotency) recommended first  
**Date:** 2026-02-26  

---

## Problem Statement

All I2C recovery and diagnostic infrastructure currently lives inside the Client sketch as `static` functions. This means:

1. **The I2C Utility sketch** has its own incomplete copy of bus recovery logic
2. **Server and Viewer sketches** have no I2C recovery at all (addressed by 3.1.3 after this)
3. **Configuration defines** are split between `TankAlarm_Config.h` (thresholds) and the Client sketch (function implementations)
4. **Testing is harder** — I2C logic can't be unit tested independently of the 6,700+ line Client sketch

Extracting I2C operations into a shared header follows the modularization plan in `MODULARIZATION_DESIGN_NOTE.md` (Phase 1 scope overlap) and creates a reusable foundation for all 4 sketches.

---

## Functions to Extract

| Function | Current Location | Lines | Dependencies |
|----------|-----------------|------:|-------------|
| `recoverI2CBus()` | Client .ino ~3147 | ~60 | `Wire`, `gDfuInProgress`, watchdog, `gI2cBusRecoveryCount` |
| `readCurrentLoopMilliamps()` | Client .ino ~3207 | ~80 | `Wire`, `gConfig.currentLoopI2cAddress`, `gCurrentLoopI2cErrors` |
| Startup I2C bus scan | Client .ino ~864 | ~50 | `Wire`, expected addresses |
| I2C error counters | Client .ino ~660 | 2 | `gCurrentLoopI2cErrors`, `gI2cBusRecoveryCount` |

### Also Extract (Configuration)

These `#define`s are already in `TankAlarm_Config.h` and need no changes:
- `I2C_NOTECARD_RECOVERY_THRESHOLD`
- `I2C_DUAL_FAIL_RECOVERY_LOOPS`
- `I2C_DUAL_FAIL_RESET_LOOPS`
- `I2C_SENSOR_ONLY_RECOVERY_THRESHOLD`
- `I2C_CURRENT_LOOP_MAX_RETRIES`
- `I2C_STARTUP_SCAN_RETRIES`
- `I2C_STARTUP_SCAN_RETRY_DELAY_MS`
- `I2C_SENSOR_RECOVERY_MAX_BACKOFF`
- `I2C_ERROR_ALERT_THRESHOLD`

---

## Proposed File Structure

### `TankAlarm-112025-Common/src/TankAlarm_I2C.h`

```cpp
#ifndef TANKALARM_I2C_H
#define TANKALARM_I2C_H

#include <Wire.h>
#include <Arduino.h>
#include "TankAlarm_Config.h"

// ============================================================================
// I2C Error Tracking
// ============================================================================

// Global counters — extern declarations, defined in the sketch
extern uint32_t gCurrentLoopI2cErrors;
extern uint32_t gI2cBusRecoveryCount;

// ============================================================================
// I2C Bus Recovery
// ============================================================================

/**
 * Attempt to recover a hung I2C bus by toggling SCL as GPIO.
 * Handles the classic failure mode where a slave is stuck driving SDA low.
 *
 * @param dfuInProgress  Set true to skip recovery during firmware updates
 * @param kickWatchdog   Optional function pointer to kick hardware watchdog
 *
 * Implementation notes:
 * - Calls Wire.end() to release pins
 * - Toggles SCL 16 times to clock out stuck slaves
 * - Generates STOP condition (SDA low→high while SCL high)
 * - Calls Wire.begin() to reinitialize
 * - Increments gI2cBusRecoveryCount
 */
static void recoverI2CBus(bool dfuInProgress, void (*kickWatchdog)() = nullptr) {
  if (dfuInProgress) {
    Serial.println(F("I2C recovery skipped - DFU in progress"));
    return;
  }

  if (kickWatchdog) {
    kickWatchdog();
  }

  Serial.println(F("I2C bus recovery: toggling SCL..."));
  Wire.end();

#if defined(ARDUINO_OPTA)
  const int I2C_SCL_PIN = PIN_WIRE_SCL;
  const int I2C_SDA_PIN = PIN_WIRE_SDA;
#else
  const int I2C_SCL_PIN = SCL;
  const int I2C_SDA_PIN = SDA;
#endif

  pinMode(I2C_SDA_PIN, INPUT);
  pinMode(I2C_SCL_PIN, OUTPUT);

  for (int i = 0; i < 16; i++) {
    digitalWrite(I2C_SCL_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(I2C_SCL_PIN, HIGH);
    delayMicroseconds(5);
  }

  pinMode(I2C_SDA_PIN, OUTPUT);
  digitalWrite(I2C_SDA_PIN, LOW);
  delayMicroseconds(5);
  digitalWrite(I2C_SCL_PIN, HIGH);
  delayMicroseconds(5);
  digitalWrite(I2C_SDA_PIN, HIGH);
  delayMicroseconds(5);

  Wire.begin();

  gI2cBusRecoveryCount++;
  Serial.print(F("I2C bus recovery complete (count="));
  Serial.print(gI2cBusRecoveryCount);
  Serial.println(F(")"));
}

// ============================================================================
// I2C Bus Scan
// ============================================================================

struct I2CScanResult {
  uint8_t foundCount;       // Number of expected devices found
  uint8_t expectedCount;    // Total expected devices
  uint8_t retryCount;       // Number of retry attempts used
  bool allFound;            // True if all expected devices responded
};

/**
 * Scan the I2C bus for expected devices with configurable retry.
 * Also reports any unexpected devices found on the bus.
 *
 * @param expectedAddrs  Array of expected I2C addresses
 * @param expectedNames  Array of human-readable device names
 * @param count          Number of expected devices
 * @return I2CScanResult with scan outcome
 */
static I2CScanResult scanI2CBus(
    const uint8_t *expectedAddrs,
    const char * const *expectedNames,
    uint8_t count
) {
  I2CScanResult result = {0, count, 0, false};
  // ... implementation moved from Client setup()
  return result;
}

// ============================================================================
// Current Loop Reading (A0602 Expansion)
// ============================================================================

/**
 * Read a 4-20mA current loop value from the A0602 expansion module.
 *
 * @param channel   A0602 channel number (0-7)
 * @param i2cAddr   I2C address of the A0602 (default CURRENT_LOOP_I2C_ADDRESS)
 * @return Current in milliamps, or -1.0f on failure
 */
static float readCurrentLoopMilliamps(int16_t channel, uint8_t i2cAddr) {
  // ... implementation moved from Client
}

#endif // TANKALARM_I2C_H
```

---

## Migration Steps

### Step 1 — Create the Header

Create `TankAlarm-112025-Common/src/TankAlarm_I2C.h` with the full implementations above.

### Step 2 — Modify the Client Sketch

1. Add `#include "TankAlarm_I2C.h"` to the includes section
2. Change `gCurrentLoopI2cErrors` and `gI2cBusRecoveryCount` from `static` to non-static (so the `extern` in the header resolves)
3. Remove the `static` implementations of `recoverI2CBus()` and `readCurrentLoopMilliamps()`
4. Update call sites:
   - `recoverI2CBus()` → `recoverI2CBus(gDfuInProgress, kickWatchdogFn)`
   - `readCurrentLoopMilliamps(channel)` → `readCurrentLoopMilliamps(channel, i2cAddr)`
5. Extract the startup bus scan into a call to `scanI2CBus()`

### Step 3 — Modify the I2C Utility Sketch

1. Add `#include "TankAlarm_I2C.h"`
2. Define `gCurrentLoopI2cErrors` and `gI2cBusRecoveryCount` as globals
3. Replace its local bus recovery implementation with calls to the shared function
4. Use `readCurrentLoopMilliamps()` from the shared header

### Step 4 — Include in TankAlarm_Common.h

Add the `#include "TankAlarm_I2C.h"` to the master include header so Server/Viewer automatically get it available (for 3.1.3).

### Step 5 — Compilation Verification

Compile all 4 sketches:
```
arduino-cli compile --fqbn arduino:mbed_opta:opta TankAlarm-112025-Client-BluesOpta/
arduino-cli compile --fqbn arduino:mbed_opta:opta TankAlarm-112025-Server-BluesOpta/
arduino-cli compile --fqbn arduino:mbed_opta:opta TankAlarm-112025-Viewer-BluesOpta/
arduino-cli compile --fqbn arduino:mbed_opta:opta TankAlarm-112025-I2C_Utility/
```

---

## Design Decisions

### Q: Header-only or .h/.cpp pair?

**Recommendation: Header-only with `static` functions.**

Arduino's build system concatenates all `.ino` files and compiles `.cpp` files in `src/` separately. Using `static` functions in a header avoids linker issues where multiple translation units define the same symbol. This matches the pattern already used by `TankAlarm_Platform.h` and `TankAlarm_Diagnostics.h`.

### Q: How to handle the DFU guard?

The `gDfuInProgress` flag is Client-specific (Server/Viewer don't do DFU this way). Pass it as a parameter to `recoverI2CBus()` rather than making it an `extern` global. Sketches without DFU simply pass `false`.

### Q: How to handle the watchdog kick?

The watchdog implementation varies by sketch (Client uses `mbedWatchdog.kick()`, others may differ). Pass a function pointer so each sketch provides its own watchdog kick implementation.

### Q: Should error counters be in the header?

Declare `extern` in the header, define in each sketch. This allows each sketch to own its counters while the shared functions can increment them. The counters are used by health telemetry and daily reports, which are sketch-specific.

---

## Dependency Graph

```
TankAlarm_Config.h          (thresholds — already shared)
       ↓
TankAlarm_I2C.h             (NEW — recovery, scan, reading functions)
       ↓
TankAlarm_Common.h          (master include — adds TankAlarm_I2C.h)
       ↓
   ┌───┴───┬──────────┬─────────┐
Client   Server    Viewer   I2C Utility
```

---

## Testing Plan

| Test | Method | Pass Criteria |
|------|--------|---------------|
| Client compiles | `arduino-cli compile` | No errors or warnings |
| Server compiles | `arduino-cli compile` | No errors or warnings |
| Viewer compiles | `arduino-cli compile` | No errors or warnings |
| I2C Utility compiles | `arduino-cli compile` | No errors or warnings |
| Bus recovery works | Pull A0602 I2C, observe recovery | Serial log shows recovery, device reconnects |
| Current loop read works | Read 4-20mA channel | Same values as before extraction |
| Startup scan works | Boot with all devices | Scan log matches pre-extraction output |
| Health telemetry includes I2C counters | Check health.qo | `i2c_cl_err` and `i2c_bus_recover` fields present |

---

## Estimated Line Changes

| File | Added | Removed | Net |
|------|------:|--------:|----:|
| `TankAlarm_I2C.h` (new) | ~150 | 0 | +150 |
| Client .ino | +5 | -145 | -140 |
| I2C Utility .ino | +5 | -30 | -25 |
| `TankAlarm_Common.h` | +1 | 0 | +1 |
| **Total** | +161 | -175 | **-14** |

Net reduction in total code while improving reuse.

---

## References

- `CODE REVIEW/MODULARIZATION_DESIGN_NOTE.md` — Phase 1 (Sensor Reading) overlaps scope
- `TankAlarm-112025-Common/src/TankAlarm_Config.h` — I2C configuration defines
- `TankAlarm-112025-Common/src/TankAlarm_Diagnostics.h` — Pattern example for header-only shared code
