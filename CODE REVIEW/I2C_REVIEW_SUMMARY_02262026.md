# I2C Code Review & Implementation Summary — 02/26/2026

## Overview
A comprehensive review of the I2C communication code across the TankAlarm project was conducted to ensure compatibility with the Arduino Opta (Mbed OS) and the Blues Notecard. The review focused on bus stability, error recovery, and hardware-specific quirks of the Opta platform.

## What Was Reviewed
1. **`TankAlarm-112025-Common/src/TankAlarm_I2C.h`**: The core shared I2C logic, including bus recovery, scanning, and current loop reading.
2. **`TankAlarm-112025-Client-BluesOpta.ino`**: The main client sketch, specifically how it initializes the I2C bus, binds the Notecard, and handles I2C errors.
3. **`TankAlarm-112025-Server-BluesOpta.ino` & `TankAlarm-112025-Viewer-BluesOpta.ino`**: Verified consistent I2C initialization patterns.
4. **`Arduino_Opta_Blueprint/src/OptaController.cpp`**: Reviewed the official Opta expansion library to understand the expected I2C communication patterns for the A0602 module.

## What Was Changed (Implemented Fixes)

The following changes were made to `TankAlarm_I2C.h` to improve stability:

### 1. Removed Repeated Start for A0602 Current Loop Reads
* **Previous Code:** Used `Wire.endTransmission(false)` to send a repeated start before reading from the A0602 expansion module.
* **Issue:** While technically correct I2C protocol, some Mbed OS implementations and specific expansion modules (like the A0602) can struggle with repeated starts or require processing time between the write (channel selection) and the read.
* **Fix:** Changed to `Wire.endTransmission()` (which sends a proper STOP condition) and added a `delay(1)` before `Wire.requestFrom()`. This matches the exact pattern used in the official `OptaController.cpp` library and gives the A0602 ADC time to process the channel change.

### 2. Safer I2C Bus Recovery STOP Condition
* **Previous Code:** Called `pinMode(I2C_SDA_PIN, OUTPUT)` before `digitalWrite(I2C_SDA_PIN, LOW)` during the manual STOP condition generation.
* **Issue:** If the pin's output register was previously HIGH, switching to OUTPUT first could cause a brief HIGH glitch on SDA before it was driven LOW.
* **Fix:** Swapped the order to `digitalWrite(I2C_SDA_PIN, LOW)` followed by `pinMode(I2C_SDA_PIN, OUTPUT)`. On Arduino, writing LOW to an INPUT pin disables the pull-up, and then switching to OUTPUT safely drives it LOW without glitches.

### 3. Corrected Struct Initialization
* **Previous Code:** `I2CScanResult result = {0, count, 0, false};`
* **Issue:** The `I2CScanResult` struct has 5 fields, but was initialized with 4 values. This caused the `unexpectedCount` field to be initialized with `false` (0) and the `allFound` boolean to be implicitly initialized to `0` (false).
* **Fix:** Updated the initialization to explicitly provide all 5 fields: `I2CScanResult result = {0, count, 0, 0, false};`.

## What is Good (Verified Opta/Blues Compatibility)

* **I2C Clock Speed:** The code correctly avoids calling `Wire.setClock(400000)`. As documented in previous analyses, the Mbed OS implementation on the Opta communicates most reliably with the Blues Notecard at the default 100 kHz.
* **Notecard Initialization:** The code correctly uses the `tankalarm_ensureNotecardBinding(notecard)` wrapper to handle `notecard.begin()`. This is safe to call multiple times and properly re-binds the Notecard after an I2C bus recovery.
* **Wire.begin() Placement:** `Wire.begin()` is correctly called once in `setup()` before any Notecard or sensor communication, and is properly re-invoked inside the `tankalarm_recoverI2CBus()` function after manually toggling the pins.
* **Bus Recovery Logic:** The manual SCL toggling (clocking out 16 bits) is the correct and robust way to recover an I2C bus where a slave device is stuck holding SDA low.

## What Might Need Improvement (Future Considerations)

1. **OptaBlue Library Integration Risk:**
   * Currently, the client sketch communicates with the A0602 via raw I2C calls. If the official `OptaBlue` library (`OptaController.begin()`) is ever integrated, it will clobber the Wire state because it internally calls `Wire.begin()` and `Wire.setClock(400000)`. If this happens, careful coordination will be needed to ensure the Notecard and the OptaBlue library don't interfere with each other's bus settings.

2. **I2C Mutex / Bus Contention:**
   * The current implementation assumes that I2C transactions (Notecard and A0602) happen sequentially in the main loop. If interrupts or RTOS threads are ever introduced that also use the I2C bus, a mutex will be required to prevent bus contention.

3. **Wire.available() Check:**
   * In `tankalarm_readCurrentLoopMilliamps()`, there is a check `if (Wire.available() < 2)`. While good for safety, if `Wire.requestFrom()` returns 2, `Wire.available()` is practically guaranteed to be 2. The current logic is safe, but if short reads become a frequent issue, investigating the root cause (e.g., A0602 firmware bugs) rather than just draining the buffer might be necessary.