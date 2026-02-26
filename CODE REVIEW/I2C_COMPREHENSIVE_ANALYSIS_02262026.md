# I2C Comprehensive Analysis — TankAlarm Client (Arduino Opta)

**Date:** February 26, 2026  
**Sketch:** `TankAlarm-112025-Client-BluesOpta.ino` (6,486 lines)  
**Hardware:** Arduino Opta Lite (STM32H747XI, Mbed OS), Blues Notecard, Opta Ext A0602  
**Scope:** All I2C-related code, error handling, risks, and recommendations

---

## Table of Contents

1. [I2C Bus Architecture](#1-i2c-bus-architecture)
2. [All I2C Code Locations](#2-all-i2c-code-locations)
3. [Current Error Handling Analysis](#3-current-error-handling-analysis)
4. [Identified Problems and Risks](#4-identified-problems-and-risks)
5. [Bus Contention Analysis](#5-bus-contention-analysis)
6. [Recovery Mechanisms](#6-recovery-mechanisms)
7. [Watchdog / I2C Interaction Risks](#7-watchdog--i2c-interaction-risks)
8. [Timing and Clock Speed Issues](#8-timing-and-clock-speed-issues)
9. [Recommended Fixes](#9-recommended-fixes)
10. [I2C Utility Comparison](#10-i2c-utility-comparison)
11. [Existing Documentation References](#11-existing-documentation-references)

---

## 1. I2C Bus Architecture

### Devices on the Shared I2C Bus (Wire)

| Device | I2C Address | Role | Protocol |
|--------|-------------|------|----------|
| Blues Notecard | `0x17` (23) | Cellular modem, GPS | JSON-over-I2C chunked protocol |
| Opta Ext A0602 | `0x64` (100) | 4-20mA current loop ADC | Register read (channel select + 2-byte raw) |
| Opta Ext A0602 (expansion bus) | `0x0B` (11) | Analog expansion (OptaBlue) | OptaBlue binary protocol |

All devices share the **same SDA/SCL lines**. The Opta is always I2C **master**.

### I2C Address Definitions

| Constant | Value | Defined In | Line |
|----------|-------|------------|------|
| `NOTECARD_I2C_ADDRESS` | `0x17` | `TankAlarm_Common.h` | L32 |
| `CURRENT_LOOP_I2C_ADDRESS` | `0x64` | Client `.ino` | L188 |

---

## 2. All I2C Code Locations

### 2.1 Client Sketch — Direct Wire Calls

| Location | Code | Purpose |
|----------|------|---------|
| `.ino` L27 | `#include <Wire.h>` | Wire library include |
| `.ino` L857 | `Wire.begin();` | I2C bus initialization in `setup()` |
| `.ino` L2263 | `notecard.begin(NOTECARD_I2C_ADDRESS);` | Notecard I2C attach in `initializeNotecard()` |
| `.ino` L2978 | `Wire.beginTransmission(i2cAddr);` | Start current loop read in `readCurrentLoopMilliamps()` |
| `.ino` L2979 | `Wire.write((uint8_t)channel);` | Select ADC channel |
| `.ino` L2980 | `Wire.endTransmission(false)` | Repeated start (no STOP) |
| `.ino` L2984 | `Wire.requestFrom(i2cAddr, (uint8_t)2)` | Request 2 bytes from ADC |
| `.ino` L2988 | `Wire.read()` (×2) | Read raw 16-bit ADC value |

### 2.2 Client Sketch — Notecard I2C (via note-arduino library)

All Notecard communication uses `notecard.requestAndResponse()` / `notecard.sendRequest()` which internally use Wire for I2C transport. Key call sites:

| Location | Notecard Call | Purpose |
|----------|---------------|---------|
| `.ino` L2263 | `notecard.begin(NOTECARD_I2C_ADDRESS)` | Attach to Notecard |
| `.ino` L2270–2282 | `notecard.requestAndResponse(req)` | `hub.get` — retrieve Device UID |
| `.ino` L2291–2313 | `notecard.requestAndResponse(req)` | `card.wireless` — health check |
| `.ino` L2329–2353 | `notecard.requestAndResponse(req)` | `card.time` — time sync |
| `.ino` L2536–2544 | `notecard.requestAndResponse(req)` | `note.get` — poll config updates |
| `.ino` L5289–5305 | `notecard.requestAndResponse(req)` | `note.add` — publish telemetry |
| Many others | `notecard.sendRequest(req)` | Fire-and-forget commands |

### 2.3 Common Library (TankAlarm_Notecard.h)

| Location | Code | Purpose |
|----------|------|---------|
| `TankAlarm_Notecard.h` L42 | `notecard.newRequest("card.time")` | Time sync helper |
| `TankAlarm_Notecard.h` L47 | `notecard.requestAndResponse(req)` | Time sync response |

**No direct Wire calls in common library** — it only uses the Notecard abstraction.

### 2.4 I2C Utility Sketch

| Location | Code | Purpose |
|----------|------|---------|
| I2C Utility L74 | `Wire.begin()` | Bus init |
| I2C Utility L196–199 | `Wire.beginTransmission()` / `Wire.endTransmission()` | ACK probe |
| I2C Utility L208–209 | `Wire.beginTransmission()` / `Wire.endTransmission()` | Bus scan |
| I2C Utility L433–465 | Notecard `card.io` with `i2c: -1` | Reset I2C address |

### 2.5 Arduino_Opta_Blueprint Library (OptaController.cpp)

| Location | Code | Purpose |
|----------|------|---------|
| OptaController.cpp L326 | `Wire.begin()` | Re-initializes Wire as I2C master |
| OptaController.cpp L327 | `Wire.setClock(400000)` | Sets bus to 400 kHz |
| OptaController.cpp L1032 | `Wire.beginTransmission(add)` | Send to expansion |
| OptaController.cpp L1047 | `Wire.write(tx_buffer[i])` | Write bytes |
| OptaController.cpp L1050 | `Wire.endTransmission()` | Complete transmission |
| OptaController.cpp L1063 | `Wire.requestFrom(add, r)` | Request response |

**Critical finding:** The OptaBlue library calls `Wire.begin()` and `Wire.setClock(400000)` inside `Controller::begin()`. The client sketch does NOT currently use `OptaController.begin()` — it communicates with the A0602 via raw I2C at address `0x64` instead of through OptaBlue. However, if OptaBlue is ever integrated, it will clobber the Wire state.

---

## 3. Current Error Handling Analysis

### 3.1 readCurrentLoopMilliamps() — Lines 2970–2991

```cpp
static float readCurrentLoopMilliamps(int16_t channel) {
  if (channel < 0) {
    return -1.0f;
  }
  uint8_t i2cAddr = gConfig.currentLoopI2cAddress 
    ? gConfig.currentLoopI2cAddress : CURRENT_LOOP_I2C_ADDRESS;

  Wire.beginTransmission(i2cAddr);
  Wire.write((uint8_t)channel);
  if (Wire.endTransmission(false) != 0) {
    return -1.0f;  // ← NACK or bus error, no retry, no logging
  }

  if (Wire.requestFrom(i2cAddr, (uint8_t)2) != 2) {
    return -1.0f;  // ← Short read, no retry, no logging
  }

  uint16_t raw = ((uint16_t)Wire.read() << 8) | Wire.read();
  return 4.0f + (raw / 65535.0f) * 16.0f;
}
```

**Error handling assessment:**
- ✅ Checks `endTransmission()` return value (NACK detection)
- ✅ Checks `requestFrom()` byte count
- ❌ **No retry logic** — single failure = -1.0f returned immediately
- ❌ **No logging** — silent failure, no Serial output on I2C error
- ❌ **No error counting** — no metric tracked for current loop I2C failures
- ❌ **No bus recovery** — if the bus is stuck, no attempt to reset it
- ❌ **No delay between endTransmission and requestFrom** — the A0602 may need processing time

### 3.2 Caller of readCurrentLoopMilliamps() — readTankSensor() at Line 3192

```cpp
case SENSOR_CURRENT_LOOP: {
  int16_t channel = ...;
  float milliamps = readCurrentLoopMilliamps(channel);
  if (milliamps < 0.0f) {
    return gMonitorState[idx].currentInches; // keep previous on failure
  }
  // ... convert mA to inches ...
}
```

**Error handling assessment:**
- ✅ Detects failed read (negative return)
- ✅ Returns last known good value (graceful degradation)
- ❌ **No failure counter at this level** — the read just silently uses stale data
- ❌ After initial error, never retries the I2C read in the same sample cycle

### 3.3 Notecard Communication Error Handling

The Notecard error handling is substantially better:

- ✅ `gNotecardFailureCount` tracks consecutive failures (L654)
- ✅ After 5 failures (`NOTECARD_FAILURE_THRESHOLD`), marks Notecard unavailable (L656)
- ✅ Periodic health checks attempt recovery every 5 minutes (L1043–1047)
- ✅ `checkNotecardHealth()` sends `card.wireless` to verify Notecard is alive (L2291)
- ✅ On recovery, flushes buffered notes (L2321)
- ✅ `publishNote()` buffers notes to flash on failure for later retry (L5306)
- ❌ **No I2C bus-level recovery** — if the I2C bus is hung, `checkNotecardHealth()` will also fail because it uses the same hung bus
- ❌ **No Wire.end()/Wire.begin() reset** — the note-arduino library internally attempts this, but the client never does its own bus reset
- ❌ `notecard.begin()` is only called once in `setup()` — never re-called during recovery

---

## 4. Identified Problems and Risks

### P1: No I2C Bus Recovery Mechanism (CRITICAL)

**Risk level: HIGH**

If the I2C bus locks up (SDA stuck low, clock stretching timeout, or electromagnetic interference), there is **no code path** that can recover it. The only recovery is a hardware watchdog reset of the entire MCU.

The STM32H747 I2C peripheral can enter a state where SDA is held low by a slave device that got out of sync (e.g., the A0602 or Notecard received a partial transaction and is waiting to complete it). The standard recovery is:
1. `Wire.end()` — deinitialize the I2C peripheral  
2. Toggle SCL manually 9–16 times as GPIO to clock out the stuck slave  
3. `Wire.begin()` — reinitialize  

**This is never done anywhere in the client code.**

### P2: No Retry Logic for Current Loop I2C Reads (HIGH)

**Risk level: HIGH**

`readCurrentLoopMilliamps()` makes exactly one attempt. A single NACK or short read returns -1.0f. In an industrial environment with electrical noise, occasional I2C glitches are expected. A simple 2–3 retry loop would dramatically improve reliability.

### P3: Silent Current Loop I2C Failures (MEDIUM)

**Risk level: MEDIUM**

When `readCurrentLoopMilliamps()` fails, there is zero logging. The operator has no way to know how often I2C reads to the A0602 are failing. This should at minimum log to Serial and increment a diagnostic counter.

### P4: No Current Loop I2C Error Counter (MEDIUM)

**Risk level: MEDIUM**

There is `gNotecardCommErrorCount` for Notecard errors, but no equivalent counter for current loop I2C failures. Health telemetry should include this.

### P5: Shared I2C Bus Without Mutex/Coordination (MEDIUM)

**Risk level: MEDIUM (currently mitigated by single-threaded execution)**

Both the Notecard (at `0x17`) and the A0602 expansion (at `0x64`) share the same Wire bus. Currently, the code is single-threaded (no RTOS tasks doing I2C), so there is no actual contention. However:
- If any future code adds an RTOS task that does I2C
- If any interrupt handler touches Wire
- If OptaBlue is ever integrated (it does its own Wire operations in `update()`)

...there would be immediate bus corruption. There is no I2C mutex or arbitration.

### P6: Wire.begin() Called Only Once, Never Re-initialized (MEDIUM)

**Risk level: MEDIUM**

`Wire.begin()` is called at line 857 in `setup()`. If the I2C bus enters an error state later, there is no code path to call `Wire.end()` + `Wire.begin()` to reset the peripheral. The `reinitializeHardware()` function (L2574) resets pin modes and Notecard hub config, but does **not** reinitialize Wire.

### P7: No Wire.setClock() After Wire.begin() (LOW-MEDIUM)

**Risk level: LOW-MEDIUM**

The code calls `Wire.begin()` at L857 but **never** calls `Wire.setClock()`. On Mbed OS, `Wire.begin()` defaults to **100 kHz** (Standard Mode). The existing `OPTA_I2C_COMMUNICATION.md` documents that this was deliberately changed — all `Wire.setClock(400000)` and `card.wire` calls were removed after debugging showed 100 kHz was the only reliable speed.

However, the A0602 expansion module supports 400 kHz, and running at 100 kHz means each I2C transaction takes 4x longer, increasing the window for watchdog issues during long Notecard transactions.

### P8: Missing Wire.available() Check Before Wire.read() (LOW)

**Risk level: LOW**

At line 2988:
```cpp
uint16_t raw = ((uint16_t)Wire.read() << 8) | Wire.read();
```

This reads 2 bytes without checking `Wire.available()`. While the preceding check `Wire.requestFrom(...) != 2` should guarantee 2 bytes are available, defensive coding would add an `available()` check. On Mbed OS, `Wire.read()` returns -1 if no bytes are available, which would corrupt the raw value silently.

### P9: endTransmission(false) — Repeated Start Risk (LOW)

**Risk level: LOW**

At line 2980, `Wire.endTransmission(false)` sends a repeated start instead of a stop. This is correct protocol for a read-after-write to the same device. However, if another device on the bus (like the Notecard) is in the middle of clock stretching, the repeated start may fail. This is a theoretical concern — in practice, the Notecard only clock-stretches during its own transactions.

### P10: No Notecard I2C Reinitialization After Extended Failure (MEDIUM)

**Risk level: MEDIUM**

When `gNotecardAvailable` is set to false after 5 consecutive failures, the recovery path in `checkNotecardHealth()` (L2291) just tries `card.wireless` again. If the underlying I2C bus is broken, this will fail forever. The recovery should include:
1. `Wire.end()` + `Wire.begin()` to reset the I2C peripheral
2. `notecard.begin(NOTECARD_I2C_ADDRESS)` to re-create the NoteI2c singleton
3. Short delay to allow the Notecard to stabilize

---

## 5. Bus Contention Analysis

### Current Architecture (Safe)

```
loop() → sampleTanks() → readTankSensor() → readCurrentLoopMilliamps()
                                                  ↓ (Wire @ 0x64)
       → publishNote() → notecard.requestAndResponse()
                                                  ↓ (Wire @ 0x17)
```

These are called **sequentially** in `loop()`, never in parallel. No bus contention exists today.

### Risk Scenario 1: Notecard Clock Stretching During Current Loop Read

The Notecard uses clock stretching extensively (up to 250ms per chunk). If the sketch sends a Notecard request via `sendRequest()` (fire-and-forget) and then immediately tries to read the current loop, the Notecard may still be stretching SCL from the previous transaction. This is unlikely because `sendRequest()` blocks until the I2C write completes, but the Notecard's internal processing could cause it to pull SCL low on the *next* bus transaction.

### Risk Scenario 2: OptaBlue Integration

If `OptaController.begin()` is ever added (as documented in the Blueprint library), it will:
1. Call `Wire.begin()` — reinitializing the peripheral (safe if done before `notecard.begin()`)
2. Call `Wire.setClock(400000)` — changing the clock speed
3. Use `_send()` internally during `OptaController.update()` — which does `Wire.beginTransmission()` / `Wire.endTransmission()` / `Wire.requestFrom()` to expansion addresses

If `OptaController.update()` is ever called in `loop()` alongside Notecard communication, bus corruption is possible unless carefully ordered.

### Risk Scenario 3: Notecard I2C Master Mode

The Notecard has its own I2C master capability (for scanning sensors on its own I2C bus). If `card.io` mode `"i2c-master-enable"` is active, the Notecard could attempt to act as a master on the bus while the Opta is also trying to master. The client code does **not** disable this mode. However, the Notecard's I2C master operates on a different physical I2C bus (on the Notecard side), so this is not a contention risk on the Opta's Wire bus.

---

## 6. Recovery Mechanisms

### What Exists

| Mechanism | Present? | Location | Notes |
|-----------|----------|----------|-------|
| Notecard failure counter | ✅ | L654 | `gNotecardFailureCount` incremented on each failure |
| Notecard offline mode | ✅ | L655 | `gNotecardAvailable` set false after 5 failures |
| Notecard health check | ✅ | L1043–1047 | `checkNotecardHealth()` every 5 min when offline |
| Notecard recovery detection | ✅ | L2315–2322 | Re-enables Notecard, flushes buffered notes |
| Note buffering for retry | ✅ | L5305–5307 | Failed notes saved to flash |
| Hardware watchdog | ✅ | L862–883 | 30-second watchdog, kicked in loop() |
| Current loop I2C error detection | ✅ | L2980+2984 | Returns -1.0f on NACK or short read |
| Stale reading fallback | ✅ | L3203 | Returns last good value on sensor failure |
| Sensor failure detection | ✅ | L3003+ | `consecutiveFailures` counter, `sensorFailed` flag, `stuckReadingCount` |

### What Is Missing

| Mechanism | Present? | Impact |
|-----------|----------|--------|
| I2C bus reset (Wire.end/begin) | ❌ | Bus lockup = permanent until watchdog reset |
| SDA stuck low recovery (SCL toggle) | ❌ | Classic I2C failure mode unhandled |
| Current loop I2C retry | ❌ | Single glitch = lost reading |
| Current loop I2C error counter | ❌ | No diagnostic visibility |
| Current loop I2C error logging | ❌ | Silent failures |
| Notecard I2C reinit after recovery | ❌ | `notecard.begin()` not re-called |
| Wire reinitialization in `reinitializeHardware()` | ❌ | Config changes don't reset I2C |
| I2C mutex/guard | ❌ | Not needed today, but no protection for future changes |

---

## 7. Watchdog / I2C Interaction Risks

### Watchdog Configuration

- Timeout: **30 seconds** (`WATCHDOG_TIMEOUT_SECONDS`, `TankAlarm_Common.h` L72)
- Kicked at: start of every `loop()` iteration (L1034), and within sleep chunks (L6417)

### Risk: Notecard Transaction Duration

A single `notecard.requestAndResponse()` can take significant time:
- I2C write: transmit JSON request in ~250-byte chunks → multiple I2C transactions
- Wait for Notecard to process (up to several seconds for cellular operations)
- I2C read: receive JSON response in chunks → multiple I2C transactions
- Total: typically 100ms–5s, but can exceed 10s for `hub.sync` or first connect

With a 30-second watchdog, a single Notecard transaction should not trigger a reset. However, if `sampleTanks()` reads from multiple tanks (each with current loop I2C), followed by several Notecard publish calls, followed by config polling, the total loop iteration could approach the watchdog timeout.

### Risk: I2C Bus Hang + Watchdog

If the I2C bus locks up (SDA stuck low), Wire.write/read will block until the internal timeout (typically 25ms on Mbed OS). The watchdog kick at the top of loop() will still execute on subsequent iterations, but the I2C-dependent code will always fail fast (-1.0f for current loop, null for Notecard). The watchdog will NOT trigger a reset in this case — the system will continue running but with permanently broken I2C.

**This is actually worse than a watchdog reset** — the system stays "alive" but cannot read sensors or communicate. A deliberate reset after N consecutive I2C failures would be better.

---

## 8. Timing and Clock Speed Issues

### Current I2C Speed: 100 kHz (Standard Mode)

The client sketch calls `Wire.begin()` without `Wire.setClock()`, defaulting to 100 kHz on Mbed OS. This was a deliberate choice documented in `OPTA_I2C_COMMUNICATION.md` after extensive debugging showed `{io}` failures at 400 kHz.

### A0602 Current Loop Read Timing at 100 kHz

Each `readCurrentLoopMilliamps()` call performs:
1. `beginTransmission(0x64)` → START + address byte + W bit = 9 clocks
2. `write(channel)` → 1 data byte = 9 clocks
3. `endTransmission(false)` → repeated start = ~2 clocks
4. `requestFrom(0x64, 2)` → START + address byte + R bit + 2 data bytes = 27 clocks
5. Plus overhead (ACK/NACK, start/stop conditions) = ~10 clocks

Total: ~57 clock cycles at 100 kHz = **~570 μs per channel read**

This is fast enough to not cause issues, even for 8 channels: 8 × 570μs = 4.56ms.

### Notecard Transaction Timing

Notecard JSON-over-I2C uses chunked transfers. A typical `note.add` with a ~200 byte body:
- Write: ~200 bytes in chunks of 250 = 1 chunk = ~20ms at 100 kHz
- Wait for processing: ~50-200ms
- Read response: ~100 bytes = 1 chunk = ~10ms at 100 kHz
- Total: **~80-230ms per note.add**

At 400 kHz, this would be ~40-210ms (the I2C transfer portion is small relative to the processing wait).

### Speed Mismatch Risk

If the Notecard has cached a 400 kHz speed setting from an old `card.wire` command on a previous firmware version, and the Opta is running at 100 kHz, clock stretching behavior may be affected. However, I2C is clock-synchronous from the master side — the slave always follows the master's clock. The risk is minimal.

---

## 9. Recommended Fixes

### Fix 1: Add I2C Bus Recovery Function (CRITICAL)

Add a function that can reset the I2C bus when it appears to be hung:

```cpp
/**
 * Attempt to recover a hung I2C bus.
 * Toggles SCL as GPIO to clock out any slave holding SDA low,
 * then reinitializes Wire.
 */
static void recoverI2CBus() {
  Serial.println(F("I2C bus recovery: toggling SCL..."));
  
  // Deinitialize Wire to release pins
  Wire.end();
  
  // Toggle SCL manually to unstick any slave
  // On Arduino Opta: SCL = PB_8, SDA = PB_9 (Wire/I2C1)
  // Use the Arduino pin numbers for the Opta's I2C pins
  #if defined(ARDUINO_OPTA)
    // Opta I2C1 pins (check your board's variant.h)
    const int SCL_PIN = PIN_WIRE_SCL;  // PB_8
    const int SDA_PIN = PIN_WIRE_SDA;  // PB_9
  #else
    const int SCL_PIN = SCL;
    const int SDA_PIN = SDA;
  #endif
  
  pinMode(SDA_PIN, INPUT);
  pinMode(SCL_PIN, OUTPUT);
  
  for (int i = 0; i < 16; i++) {
    digitalWrite(SCL_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(SCL_PIN, HIGH);
    delayMicroseconds(5);
  }
  
  // Generate STOP condition: SDA goes low then high while SCL is high
  pinMode(SDA_PIN, OUTPUT);
  digitalWrite(SDA_PIN, LOW);
  delayMicroseconds(5);
  digitalWrite(SCL_PIN, HIGH);
  delayMicroseconds(5);
  digitalWrite(SDA_PIN, HIGH);
  delayMicroseconds(5);
  
  // Reinitialize Wire
  Wire.begin();
  // No Wire.setClock() — stay at 100 kHz default
  
  Serial.println(F("I2C bus recovery complete"));
}
```

### Fix 2: Add Retry Logic to readCurrentLoopMilliamps() (HIGH)

```cpp
static float readCurrentLoopMilliamps(int16_t channel) {
  if (channel < 0) {
    return -1.0f;
  }
  
  uint8_t i2cAddr = gConfig.currentLoopI2cAddress 
    ? gConfig.currentLoopI2cAddress : CURRENT_LOOP_I2C_ADDRESS;

  const uint8_t MAX_RETRIES = 3;
  for (uint8_t attempt = 0; attempt < MAX_RETRIES; attempt++) {
    if (attempt > 0) {
      delay(2);  // Brief delay between retries
    }
    
    Wire.beginTransmission(i2cAddr);
    Wire.write((uint8_t)channel);
    uint8_t err = Wire.endTransmission(false);
    if (err != 0) {
      if (attempt == MAX_RETRIES - 1) {
        Serial.print(F("I2C NACK from 0x"));
        Serial.print(i2cAddr, HEX);
        Serial.print(F(" err="));
        Serial.println(err);
      }
      continue;
    }

    if (Wire.requestFrom(i2cAddr, (uint8_t)2) != 2) {
      if (attempt == MAX_RETRIES - 1) {
        Serial.print(F("I2C short read from 0x"));
        Serial.println(i2cAddr, HEX);
      }
      continue;
    }

    uint16_t raw = ((uint16_t)Wire.read() << 8) | Wire.read();
    return 4.0f + (raw / 65535.0f) * 16.0f;
  }
  
  return -1.0f;  // All retries exhausted
}
```

### Fix 3: Add Current Loop I2C Error Counter (MEDIUM)

```cpp
// Add to global state (near line 631):
static uint32_t gCurrentLoopI2cErrors = 0;

// In readCurrentLoopMilliamps(), after all retries fail:
gCurrentLoopI2cErrors++;

// Include in health telemetry (sendHealthTelemetry):
JAddNumberToObject(body, "i2c_cl_err", gCurrentLoopI2cErrors);
```

### Fix 4: Add I2C Bus Recovery to Notecard Recovery Path (HIGH)

In `checkNotecardHealth()`, after the failure threshold is reached, attempt bus recovery:

```cpp
static bool checkNotecardHealth() {
  J *req = notecard.newRequest("card.wireless");
  if (!req) {
    gNotecardFailureCount++;
    if (gNotecardFailureCount >= NOTECARD_FAILURE_THRESHOLD && gNotecardAvailable) {
      gNotecardAvailable = false;
      Serial.println(F("Notecard unavailable - entering offline mode"));
    }
    // NEW: After threshold+5 failures, try I2C bus recovery + Notecard reinit
    if (gNotecardFailureCount >= NOTECARD_FAILURE_THRESHOLD + 5) {
      Serial.println(F("Attempting I2C bus recovery..."));
      recoverI2CBus();
      notecard.begin(NOTECARD_I2C_ADDRESS);
      gNotecardFailureCount = NOTECARD_FAILURE_THRESHOLD;  // Reset to threshold
    }
    return false;
  }
  // ... rest of function unchanged ...
}
```

### Fix 5: Add Wire Reinitialization to reinitializeHardware() (LOW)

```cpp
static void reinitializeHardware() {
  // Reinitialize I2C bus in case of configuration changes
  Wire.end();
  delay(10);
  Wire.begin();
  
  // Reinitialize all tank sensors with new configuration
  for (uint8_t i = 0; i < gConfig.monitorCount; ++i) {
    // ... existing code ...
  }
  
  // Re-attach Notecard after Wire reinit
  notecard.begin(NOTECARD_I2C_ADDRESS);
  
  configureNotecardHubMode();
  Serial.println(F("Hardware reinitialized after config update"));
}
```

### Fix 6: Deliberate Reset After Prolonged I2C Failure (MEDIUM)

If both the Notecard AND current loop I2C are failing for an extended period, the system should force a watchdog reset rather than running indefinitely in a broken state:

```cpp
// In loop(), after the watchdog kick:
static uint32_t consecutiveTotalI2cFailures = 0;

// After sampleTanks() check if ALL I2C operations are failing
bool allI2cFailed = !gNotecardAvailable;
for (uint8_t i = 0; i < gConfig.monitorCount; i++) {
  if (gConfig.monitors[i].sensorInterface == SENSOR_CURRENT_LOOP &&
      gMonitorState[i].consecutiveFailures >= SENSOR_FAILURE_THRESHOLD) {
    allI2cFailed = true;
  }
}

if (allI2cFailed) {
  consecutiveTotalI2cFailures++;
  if (consecutiveTotalI2cFailures > 60) {  // ~60 loop iterations ≈ 5-30 minutes
    Serial.println(F("FATAL: Prolonged I2C failure. Forcing reset."));
    // Stop kicking watchdog → triggers hardware reset
    while(true) { delay(100); }
  }
} else {
  consecutiveTotalI2cFailures = 0;
}
```

### Fix 7: Log I2C Failures with Error Codes (LOW)

The `Wire.endTransmission()` return values have specific meanings:
- 0: success
- 1: data too long for transmit buffer
- 2: received NACK on transmit of address
- 3: received NACK on transmit of data
- 4: other error
- 5: timeout (not standard but used by some implementations)

Log the specific error code to aid field debugging.

---

## 10. I2C Utility Comparison

The I2C Utility sketch (`TankAlarm-112025-I2C_Utility.ino`, 578 lines) provides these I2C diagnostic capabilities that the client lacks:

| Feature | I2C Utility | Client |
|---------|-------------|--------|
| I2C bus scan | ✅ `scanI2CBus()` | ❌ |
| ACK probe | ✅ `i2cAck()` | ❌ |
| Auto-detect Notecard address | ✅ `autoAttachNotecard()` | ❌ |
| Reset Notecard I2C address | ✅ `resetI2CAddressToDefault()` | ❌ |
| Notecard diagnostics | ✅ `runDiagnostics()` | ❌ |
| Card restore (factory reset) | ✅ `runCardRestore()` | ❌ |

The utility is a separate sketch meant for manual debugging. None of these capabilities are available at runtime in the client.

---

## 11. Existing Documentation References

| Document | Key I2C Content |
|----------|-----------------|
| `CODE REVIEW/OPTA_I2C_COMMUNICATION.md` (900 lines) | Comprehensive I2C reference, debugging history, all attempted fixes for `{io}` errors, finding that `card.wire` is deprecated |
| `CODE REVIEW/CODE_REVIEW_112025.md` L159–161 | Original finding: "Fixed I2C Address for Current Loop" at 0x64 may conflict, recommend configurable |
| `CODE REVIEW/COMMUNICATION_ARCHITECTURE_VERDICT_02192026.md` L554–566 | Documents Wire library and I2C at 0x17, 400 kHz |
| `Tutorials/Tutorials-112025/WIRING_DIAGRAM.md` L119–133 | I2C frequency 400 kHz reference, `Wire.setClock` usage |
| `TankAlarm-112025-Common/README.md` L55 | Documents `NOTECARD_I2C_FREQUENCY` (now removed from code) |

### Key Historical Finding from OPTA_I2C_COMMUNICATION.md

The document records 7 debugging attempts for `{io}` errors on the client Notecard. All attempts at changing I2C speed (100/400 kHz), init order, retry loops, and removing/adding `Wire.setClock()` failed. The ultimate resolution was to:
1. Remove all `Wire.setClock()` calls (stay at 100 kHz)  
2. Remove the deprecated `card.wire` API call  
3. Accept that the Notecard communicates at 100 kHz  

The document's hypothesis list includes hardware issues (loose M.2 connector), Notecard firmware mismatch, OptaBlue library conflicts, and persistent Notecard state — suggesting the root cause may have been physical rather than code-level.

---

## Summary of Priority Fixes

| # | Fix | Priority | Effort | Impact |
|---|-----|----------|--------|--------|
| 1 | I2C bus recovery function (`recoverI2CBus()`) | CRITICAL | Medium | Recovers from bus lockups without full MCU reset |
| 2 | Retry logic in `readCurrentLoopMilliamps()` | HIGH | Low | Tolerates single-glitch I2C failures |
| 4 | I2C recovery in Notecard failure path | HIGH | Low | Recovers Notecard after extended failures |
| 6 | Deliberate reset after prolonged I2C failure | MEDIUM | Low | Prevents zombie state (alive but blind) |
| 3 | Current loop I2C error counter | MEDIUM | Low | Diagnostic visibility |
| 10 | Notecard reinit (`notecard.begin()`) after recovery | MEDIUM | Low | Properly reconnects to Notecard |
| 5 | Wire reinit in `reinitializeHardware()` | LOW | Low | Handles I2C state after config changes |
| 7 | Log I2C error codes | LOW | Low | Field debugging aid |
| 8 | `Wire.available()` check before `Wire.read()` | LOW | Trivial | Defensive coding |
