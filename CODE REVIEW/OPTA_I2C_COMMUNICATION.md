# Arduino Opta I2C Communication — Comprehensive Reference

**Date:** 2026-02-20  
**Project:** TankAlarm v1.1.3  
**Hardware:** Arduino Opta Lite (STM32H747), Blues Wireless Notecard, Arduino Pro Opta Ext A0602  
**Purpose:** Document all I2C communication fundamentals, configurations, and debugging history

---

## Table of Contents

1. [I2C Fundamentals on Arduino Opta](#1-i2c-fundamentals-on-arduino-opta)
2. [Expansion Module Communication (OptaBlue)](#2-expansion-module-communication-optablue)
3. [Blues Notecard I2C Communication](#3-blues-notecard-i2c-communication)
4. [note-arduino Library API Reference](#4-note-arduino-library-api-reference)
5. [Notecard API Reference for I2C](#5-notecard-api-reference-for-i2c)
6. [Our Code — Current Implementation](#6-our-code--current-implementation)
7. [What We've Tried — I2C Debugging History](#7-what-weve-tried--i2c-debugging-history)
8. [All Possible Code Patterns](#8-all-possible-code-patterns)
9. [Critical Findings](#9-critical-findings)
10. [Hypotheses and Next Steps](#10-hypotheses-and-next-steps)

---

## 1. I2C Fundamentals on Arduino Opta

### 1.1 Hardware Overview

The Arduino Opta is based on the **STM32H747XI** dual-core processor:
- **Cortex-M7** @ 480 MHz — runs the main Arduino sketch
- **Cortex-M4** @ 240 MHz — available for secondary tasks

The STM32H747 has multiple I2C peripherals (I2C1 through I2C4). The Arduino Wire library on Mbed OS wraps these peripherals.

### 1.2 I2C Speed Modes

| Mode | Speed | Notes |
|------|-------|-------|
| Standard Mode | 100 kHz | Default for `Wire.begin()` on Mbed OS |
| Fast Mode | 400 kHz | Must be set via `Wire.setClock(400000)` |
| Fast Mode Plus | 1 MHz | Supported by STM32H747 but rarely used |

**Important:** On Mbed OS (which the Opta uses), `Wire.begin()` initializes the I2C peripheral at **100 kHz** (Standard Mode) by default. The clock must be explicitly set with `Wire.setClock()` if a faster speed is needed.

### 1.3 Arduino Wire Library on Mbed OS

The Opta uses the **Mbed OS Wire library** implementation, NOT the standard AVR Wire library. Key differences:

```cpp
#include <Wire.h>

// Initialize I2C as master
Wire.begin();

// Set clock speed (must be called AFTER Wire.begin())
Wire.setClock(400000);  // 400 kHz Fast Mode

// Basic I2C transaction
Wire.beginTransmission(address);  // Start + address
Wire.write(data);                  // Write data
Wire.endTransmission();            // Stop

// Request data from device
Wire.requestFrom(address, numBytes);
while (Wire.available()) {
  byte b = Wire.read();
}
```

**Platform-Specific Notes for Mbed OS (Opta):**
- `Wire.begin()` must be called before any I2C communication
- Calling `Wire.begin()` multiple times may reinitialize the peripheral (on Mbed OS, this can cause issues with devices already on the bus)
- `Wire.setClock()` should be called after `Wire.begin()` and before any communication
- The Wire library on Mbed OS uses the internal `mbed::I2C` class
- I2C pins are fixed: SDA on a specific pin, SCL on a specific pin (hardware-defined)

### 1.4 I2C Bus Architecture on Opta

The Opta has a shared I2C bus connecting:
1. **Blues Notecard** at address `0x17` (23 decimal)
2. **Expansion modules** (Ext A0602) starting at address `0x0B` (11 decimal)
3. **Any other I2C peripherals** on the user-accessible bus

All devices share the same SDA/SCL lines. The Opta is ALWAYS the I2C **master**; all connected devices are **slaves**.

---

## 2. Expansion Module Communication (OptaBlue)

### 2.1 Arduino_Opta_Blueprint Library

The **Arduino_Opta_Blueprint** (OptaBlue) library version **0.2.8** manages communication with Opta expansion modules.

**Source:** https://github.com/arduino-libraries/Arduino_Opta_Blueprint

### 2.2 I2C Architecture

From the OptaBlue library documentation:

> **I2C Communication Protocol:** OptaBlue uses I2C as its communication layer. All Opta devices maintain a master-slave hierarchical relationship:
> - The first Opta (closest to the controller) is always the **I2C master**
> - All expansion modules are **I2C slaves**

### 2.3 Address Assignment

Expansion modules use an auto-addressing scheme:

| Device | I2C Address | Notes |
|--------|-------------|-------|
| First expansion | `0x0B` (11) | Auto-assigned during detection |
| Second expansion | `0x0C` (12) | Auto-assigned during detection |
| Third expansion | `0x0D` (13) | Auto-assigned during detection |
| And so on... | Incrementing | Up to the maximum supported |

**DETECT Pin Mechanism:**
- Each expansion has a DETECT_IN and DETECT_OUT pin
- The Opta controller pulls DETECT_IN on the first expansion
- When an expansion detects this signal, it activates its DETECT_OUT to chain to the next expansion
- This allows the library to discover and auto-address all connected expansions

### 2.4 OptaBlue Code Pattern

```cpp
#include <OptaBlue.h>

void setup() {
  // OptaBlue handles its own I2C initialization internally
  OptaController.begin();
}

void loop() {
  // Update expansion module state
  OptaController.update();
  
  // Read analog input from expansion
  uint16_t value = 0;
  OptaController.analogRead(expansionIndex, channel, value);
}
```

### 2.5 Key Classes

- **`OptaController`** — Singleton managing the I2C master side
- **`Module`** — Base class for expansion modules
- **`AnalogExpansion`** — Derived class for the Ext A0602 analog expansion
- **`Controller`** — Handles discovery, addressing, and communication

### 2.6 Interaction with Wire Library

**Critical:** The OptaBlue library uses `Wire` internally. If both OptaBlue and other I2C devices (like the Notecard) share the same Wire instance, there can be contention. The OptaBlue library may:
- Call `Wire.begin()` during `OptaController.begin()`
- Set its own clock speed
- Hold the bus during expansion communication

---

## 3. Blues Notecard I2C Communication

### 3.1 Notecard I2C Basics

| Parameter | Value |
|-----------|-------|
| Default I2C Address | `0x17` (23 decimal) |
| Default I2C Speed | 100 kHz (Standard Mode) |
| Maximum I2C Speed | 400 kHz (Fast Mode) |
| Communication Protocol | JSON over I2C |

The Notecard communicates via JSON requests and responses sent over the I2C bus. The note-arduino library handles the serialization/deserialization and I2C transport.

### 3.2 How Notecard I2C Works

1. The host (Opta) sends a JSON request as bytes over I2C to address `0x17`
2. The Notecard processes the request
3. The host polls the Notecard for a response
4. The response is returned as JSON bytes over I2C

The Notecard uses a chunked transfer protocol — data is sent/received in segments (default max ~250 bytes per chunk).

### 3.3 Notecard I2C Address Change

The Notecard's I2C address can be changed from the default `0x17` using:

```json
{
  "req": "card.io",
  "i2c": 24
}
```

This changes the address to `0x18` (24 decimal). Pass `-1` to reset to default.

**Note from Blues docs:** The alternate I2C address is **NOT reset** by `card.restore` with `"delete": true`. This is intentional so the device remains accessible after a factory reset.

### 3.4 I2C Master Mode Control

The Notecard itself can act as an I2C master (for scanning sensors like BME280, OPT3001, ENS210 on its own I2C bus). This can be disabled:

```json
{
  "req": "card.io",
  "mode": "i2c-master-disable"
}
```

Re-enable with:
```json
{
  "req": "card.io",
  "mode": "i2c-master-enable"
}
```

---

## 4. note-arduino Library API Reference

### 4.1 Initialization

```cpp
#include <Notecard.h>

Notecard notecard;

void setup() {
  // Initialize Notecard over I2C
  notecard.begin(NOTE_I2C_ADDR_DEFAULT);  // 0x17
  
  // Or with full parameters:
  notecard.begin(
    0x17,    // i2cAddress
    0,       // i2cMax (0 = default, ~250 bytes)
    Wire     // wirePort (defaults to Wire)
  );
}
```

**Internal behavior of `notecard.begin()` (from Notecard.cpp source):**
1. Creates an internal `NoteI2c` singleton if one doesn't already exist
2. The `NoteI2c` singleton holds a reference to the Wire port
3. Sets up the internal I2C transport functions (reset, transmit, receive)
4. Does **NOT** call `Wire.begin()` — the caller is responsible for that

**Critical:** If `notecard.begin()` is called without a prior `Wire.begin()`, the `NoteI2c` singleton will be created but I2C communication will fail with `{io}` errors because the Wire peripheral isn't initialized.

### 4.2 Sending Requests

There are two patterns for sending requests to the Notecard:

#### Fire-and-Forget (Command)
```cpp
J *req = notecard.newRequest("hub.set");
if (req) {
  JAddStringToObject(req, "product", "com.example:myproduct");
  JAddStringToObject(req, "mode", "continuous");
  notecard.sendRequest(req);  // No response expected
}
```

Use `sendRequest()` for:
- Configuration commands where you don't need the response
- `hub.set`, `card.wire`, and similar configuration commands
- Commands sent with `"cmd"` prefix

#### Request-and-Response (Query)
```cpp
J *req = notecard.newRequest("card.status");
J *rsp = notecard.requestAndResponse(req);
if (rsp) {
  if (notecard.responseError(rsp)) {
    // Handle error
    const char *err = JGetString(rsp, "err");
  } else {
    // Process response
    bool connected = JGetBool(rsp, "connected");
  }
  notecard.deleteResponse(rsp);
}
```

Use `requestAndResponse()` for:
- Any request where you need to read the response
- `card.status`, `hub.get`, `note.add` (to confirm delivery)
- Queries that return data

#### Retry Variants
```cpp
// Auto-retry on failure
notecard.sendRequestWithRetry(req, 5);  // Up to 5 retries

J *rsp = notecard.requestAndResponseWithRetry(req, 5);  // Up to 5 retries
```

### 4.3 JSON Helper Functions

```cpp
// Create objects
J *req = notecard.newRequest("note.add");
J *body = JCreateObject();

// Add fields
JAddStringToObject(req, "file", "data.qo");
JAddBoolToObject(req, "sync", true);
JAddNumberToObject(body, "temperature", 25.5);
JAddIntToObject(body, "count", 42);
JAddItemToObject(req, "body", body);

// Read fields from response
const char *str = JGetString(rsp, "field_name");
double num = JGetNumber(rsp, "field_name");
int i = JGetInt(rsp, "field_name");
bool b = JGetBool(rsp, "field_name");
```

### 4.4 Error Handling

```cpp
// Check if response contains an error
if (notecard.responseError(rsp)) {
  const char *err = JGetString(rsp, "err");
  Serial.print("Notecard error: ");
  Serial.println(err);
}
```

### 4.5 Debug Output

```cpp
// Enable debug output to Serial
notecard.setDebugOutputStream(Serial);

// All Notecard requests/responses will be echoed to Serial
```

---

## 5. Notecard API Reference for I2C

### 5.1 card.io — I2C Configuration

**Purpose:** Override the Notecard's I2C address and control I2C master behavior.

```json
// Change I2C address to 0x18
{"req": "card.io", "i2c": 24}

// Reset to default address (0x17)
{"req": "card.io", "i2c": -1}

// Disable Notecard as I2C master
{"req": "card.io", "mode": "i2c-master-disable"}

// Re-enable Notecard as I2C master
{"req": "card.io", "mode": "i2c-master-enable"}

// Disable USB port
{"req": "card.io", "mode": "-usb"}
```

### 5.2 card.wire — DEPRECATED / REMOVED

> **⚠️ CRITICAL FINDING:** `card.wire` is **NOT present** in the latest Blues Notecard API documentation (v9.x / Latest). It does not appear in the sidebar navigation, nor in the body of the card Requests page. This API has been **deprecated or removed**.

The old code used `card.wire` to set the I2C bus speed:

```cpp
// OLD CODE — used in original working firmware
J *req = notecard.newRequest("card.wire");
if (req) {
  JAddIntToObject(req, "speed", 400000);
  notecard.sendRequest(req);
}
```

**Implications:**
- Sending `card.wire` to a Notecard running firmware v9.x may return an error or be silently ignored
- The Notecard may persist the old 400kHz setting from previous firmware, or it may have been reset
- If the Notecard firmware was updated, the `card.wire` configuration would be lost
- The current I2C speed negotiation between host and Notecard is unclear without this API

### 5.3 card.status — Health Check

Useful for verifying I2C communication is working:

```json
{"req": "card.status"}
```

Returns:
```json
{
  "status": "{normal}",
  "usb": true,
  "storage": 8,
  "connected": true,
  "cell": true,
  "sync": true
}
```

### 5.4 card.version — Device Info

```json
{"req": "card.version"}
```

Returns firmware version, DeviceUID, SKU, board version. Also returns the `"device"` field which contains the DeviceUID.

### 5.5 hub.get — Get Device UID

```json
{"req": "hub.get"}
```

Returns the `"device"` field containing the DeviceUID (e.g., `"dev:XXXXXXXXXXXX"`).

---

## 6. Our Code — Current Implementation

### 6.1 Client `setup()` (Line ~778)

```cpp
void setup() {
  // ... LED and Serial init ...
  
  Wire.begin();
  Wire.setClock(400000UL);
  
  initializeNotecard();
  ensureTimeSync();
  
  // ... rest of setup ...
}
```

### 6.2 Client `initializeNotecard()` (Line ~2203)

```cpp
void initializeNotecard() {
  notecard.begin(NOTECARD_I2C_ADDRESS);  // 0x17
  
  // Set I2C speed on Notecard side
  J *req = notecard.newRequest("card.wire");
  if (req) {
    JAddIntToObject(req, "speed", 400000);
    notecard.sendRequest(req);  // Fire and forget
  }
  
  // Configure hub mode
  configureNotecardHubMode();
  
  // Get Device UID
  req = notecard.newRequest("hub.get");
  J *rsp = notecard.requestAndResponse(req);
  if (rsp) {
    const char *deviceUID = JGetString(rsp, "device");
    if (deviceUID && strlen(deviceUID) > 0) {
      strlcpy(gConfig.deviceLabel, deviceUID, sizeof(gConfig.deviceLabel));
    }
    notecard.deleteResponse(rsp);
  }
}
```

### 6.3 Original Working Code (from `old_ino_dump.txt`, Line ~3880)

The server firmware that is currently running and **working** at 192.168.7.117:

```cpp
void initializeNotecard() {
  notecard.begin(NOTECARD_I2C_ADDRESS);   // 0x17
  
  J *req = notecard.newRequest("card.wire");
  if (req) {
    JAddIntToObject(req, "speed", (int)NOTECARD_I2C_FREQUENCY);  // 400000
    notecard.sendRequest(req);
  }
  
  req = notecard.newRequest("hub.set");
  if (req) {
    JAddStringToObject(req, "product", productUid);
    JAddStringToObject(req, "mode", "continuous");
    notecard.sendRequest(req);
  }
  
  req = notecard.newRequest("card.uuid");  // NOTE: This doesn't exist either!
  J *rsp = notecard.requestAndResponse(req);
  // ... process UID ...
}
```

**Key observations about the original working code:**
- It uses `card.wire` (now deprecated)
- It uses `card.uuid` (which doesn't exist in ANY version of the API)
- It calls `Wire.begin()` and `Wire.setClock(400000)` in `setup()` before `initializeNotecard()`
- No delays between I2C operations
- The server running this exact code is working fine

### 6.4 Common Header (TankAlarm_Common.h)

```cpp
#define NOTECARD_I2C_ADDRESS 0x17
// NOTECARD_I2C_FREQUENCY was REMOVED (was 400000UL)
```

---

## 7. What We've Tried — I2C Debugging History

### Error Observed

After flashing the modified client firmware, ALL Notecard communication fails with:
```
publishNote: Notecard error: failed to reset Notecard interface {io}
```

The `{io}` error comes from the `NoteI2c` singleton in `Notecard.cpp`. The error message "failed to reset Notecard interface" means the `NoteI2c->reset()` call is failing at the Wire transport level — the I2C bus itself is not communicating.

### Attempt 1: Move Wire.setClock After card.wire

**Theory:** Wire.setClock should be called after telling the Notecard to use 400kHz.  
**Change:** Moved `Wire.setClock(400000)` to after `card.wire` sendRequest.  
**Result:** ❌ Still `{io}` errors.

### Attempt 2: 15-Attempt Retry Loop

**Theory:** The Notecard might need time to wake up.  
**Change:** Added a retry loop with 500ms delays, trying up to 15 times to send `card.status`.  
**Result:** ❌ All 15 attempts failed. Device stuck forever in retry loop.

### Attempt 3: Remove Wire.begin() from setup()

**Theory:** Calling `Wire.begin()` in `setup()` AND having `notecard.begin()` internally use Wire could cause a double-init on Mbed OS, corrupting the I2C peripheral.  
**Change:** Removed `Wire.begin()` from `setup()`, relying on `notecard.begin()` to handle initialization.  
**Result:** ❌ Still failed. (Note: `notecard.begin()` does NOT call `Wire.begin()` — the caller must do it.)

### Attempt 4: Wire.begin() + Wire.setClock Before notecard.begin()

**Theory:** Speed mismatch — the Notecard may have persisted 400kHz from old firmware, so the host needs to be at 400kHz from the start.  
**Change:** `Wire.begin()` → `Wire.setClock(400000)` → `notecard.begin(0x17)`.  
**Result:** ❌ Still `{io}` errors.

### Attempt 5: Remove All Speed Configuration (100kHz Default)

**Theory:** The Notecard may have reset its I2C speed to 100kHz (default). Remove all `Wire.setClock()` and `card.wire` calls, use 100kHz only.  
**Change:** Removed `Wire.setClock()`, removed `card.wire` request. Only `Wire.begin()` → `notecard.begin(0x17)`.  
**Result:** ❌ Still failed. (If the Notecard had persisted 400kHz from old firmware, there would be a speed mismatch.)

### Attempt 6: Auto-Detect Speed

**Theory:** Try both speeds and see which works.  
**Change:** Added a function that tries `card.status` at 100kHz first, then at 400kHz, using `requestAndResponse` to detect success.  
**Result:** ❌ BOTH speeds failed. Neither 100kHz nor 400kHz produced a valid response.

### Attempt 7: Restore Exact Original Pattern

**Theory:** Match the original working code exactly.  
**Change:** Restored the exact sequence from `old_ino_dump.txt`:
```cpp
// In setup():
Wire.begin();
Wire.setClock(400000UL);

// In initializeNotecard():
notecard.begin(NOTECARD_I2C_ADDRESS);

J *req = notecard.newRequest("card.wire");
if (req) {
  JAddIntToObject(req, "speed", 400000);
  notecard.sendRequest(req);
}

req = notecard.newRequest("hub.set");
if (req) {
  JAddStringToObject(req, "product", productUid);
  JAddStringToObject(req, "mode", "continuous");
  notecard.sendRequest(req);
}
```
No delays, no retries, exact same order as the working server.  
**Result:** ❌ Still getting `{io}` on subsequent `note.add` calls.

---

## 8. All Possible Code Patterns

### 8.1 Minimal I2C Init (100 kHz)

```cpp
Wire.begin();
notecard.begin(0x17);
```

### 8.2 Fast Mode I2C Init (400 kHz)

```cpp
Wire.begin();
Wire.setClock(400000);
notecard.begin(0x17);
```

### 8.3 With card.wire Speed (Legacy)

```cpp
Wire.begin();
Wire.setClock(400000);
notecard.begin(0x17);

J *req = notecard.newRequest("card.wire");
if (req) {
  JAddIntToObject(req, "speed", 400000);
  notecard.sendRequest(req);
}
```

### 8.4 With Explicit Wire Port

```cpp
Wire.begin();
Wire.setClock(400000);
notecard.begin(0x17, 0, Wire);  // Explicit Wire port parameter
```

### 8.5 With Debug Output

```cpp
Wire.begin();
Wire.setClock(400000);
notecard.setDebugOutputStream(Serial);
notecard.begin(0x17);
```

### 8.6 With card.restore Factory Reset

```cpp
Wire.begin();
notecard.begin(0x17);

// Factory reset the Notecard
J *req = notecard.newRequest("card.restore");
if (req) {
  JAddBoolToObject(req, "delete", true);
  notecard.sendRequest(req);
}
delay(5000);  // Wait for restart

// Re-initialize
notecard.begin(0x17);
```

### 8.7 With card.restart

```cpp
Wire.begin();
notecard.begin(0x17);

// Restart Notecard firmware
J *req = notecard.newCommand("card.restart");  // NOTE: uses "cmd" not "req"
notecard.sendRequest(req);
delay(5000);  // Wait for restart

// Re-initialize
notecard.begin(0x17);
```

### 8.8 With OptaBlue Coordination

```cpp
// Initialize expansion modules FIRST
OptaController.begin();

// Then initialize Notecard
Wire.begin();  // May need to re-begin after OptaBlue
Wire.setClock(400000);
notecard.begin(0x17);
```

### 8.9 Manual I2C Probe

```cpp
Wire.begin();

// Scan for Notecard on bus
Wire.beginTransmission(0x17);
int error = Wire.endTransmission();

if (error == 0) {
  Serial.println("Notecard found at 0x17");
  notecard.begin(0x17);
} else {
  Serial.print("Notecard NOT found. Error: ");
  Serial.println(error);
  // Error codes: 1=data too long, 2=NACK address, 3=NACK data, 4=other
}
```

### 8.10 Full I2C Bus Scan

```cpp
Wire.begin();
Serial.println("Scanning I2C bus...");

for (byte addr = 1; addr < 127; addr++) {
  Wire.beginTransmission(addr);
  int error = Wire.endTransmission();
  
  if (error == 0) {
    Serial.print("Device found at 0x");
    if (addr < 16) Serial.print("0");
    Serial.println(addr, HEX);
  }
}
```

---

## 9. Critical Findings

### 9.1 `card.wire` is DEPRECATED/REMOVED

The `card.wire` API does **not appear** in the latest Blues Notecard API documentation (v9.x / "Latest" firmware). The sidebar navigation lists APIs alphabetically from `card.attn` to `card.wireless.penalty`, and `card.wire` is completely absent.

**This means:**
- The old code that relies on `card.wire` to set I2C speed may be sending an invalid request
- If the Notecard was updated to firmware v9.x, the `card.wire` command would fail or be ignored
- The I2C speed may now be auto-negotiated or fixed at 100kHz

### 9.2 `card.uuid` Never Existed

The old working code uses `card.uuid` to get the device UID. This API has **never existed** in any version of the Blues documentation. Yet the old server code works fine — this suggests `requestAndResponse` simply returns an error/empty response for invalid APIs, and the code gracefully handles it.

### 9.3 The Working Server Uses OLD Firmware

The server at 192.168.7.117 is running **unmodified** old firmware and works perfectly. This old firmware may be using an older Notecard firmware version that still supports `card.wire`. If the client's Notecard has a different (newer or older) firmware version, that could explain the discrepancy.

### 9.4 The `{io}` Error Is Transport-Level

From the `Notecard.cpp` source code, the `{io}` error can come from:

1. **`NoteI2c` is nullptr:** `"i2c: A call to Notecard::begin() is required. {io}"` — This means `begin()` was never called.

2. **Transport reset failure:** `"failed to reset Notecard interface {io}"` — This means the `NoteI2c->reset()` function returned false. On the Wire implementation, this means the I2C bus reset (Wire.end() + Wire.begin()) failed.

3. **Transmit/Receive failure:** Other `{io}` errors from failed Wire.write() or Wire.requestFrom() calls.

The error we see ("failed to reset Notecard interface") is case #2 — the I2C bus cannot be reset/reinitialized.

### 9.5 Potential Bus Contention

The Opta has **both** the Notecard (0x17) and expansion modules (0x0B+) on the same I2C bus. If OptaBlue code runs during or overlapping with Notecard communication, there could be bus contention. The OptaBlue library may also call `Wire.begin()` or change clock settings.

---

## 10. Hypotheses and Next Steps

### Hypothesis A: Hardware Issue

The client device may have a hardware issue specific to that unit:
- Loose I2C connector (M.2 slot or QwiiC connection)
- Damaged I2C pull-up resistors
- Notecard not properly seated in its slot
- Power supply issue affecting I2C voltage levels

**Test:** 
- Physical inspection of all I2C connections
- Swap the Notecard between client and server devices
- Use a logic analyzer on SDA/SCL to see actual bus signals

### Hypothesis B: Notecard Firmware Mismatch

The two Notecards may be running different firmware versions. If the client's Notecard has firmware v9.x where `card.wire` was removed, while the server has an older version, this could explain the difference.

**Test:**
- Flash a minimal sketch that ONLY does `Wire.begin()` → `notecard.begin()` → `card.version` to check firmware
- Compare firmware versions between client and server Notecards

### Hypothesis C: OptaBlue Library Conflict

The `OptaController.begin()` call may reinitialize Wire or change clock settings, interfering with Notecard communication after it was established.

**Test:**
- Try initializing the Notecard BEFORE `OptaController.begin()`
- Try initializing the Notecard AFTER `OptaController.begin()` with a fresh `Wire.begin()`
- Remove OptaBlue entirely from a test sketch to isolate

### Hypothesis D: Persistent Notecard State

The Notecard may have a corrupted or incompatible configuration persisted from a previous session (e.g., wrong I2C address via `card.io`, disabled I2C master mode, or a firmware issue).

**Test:**
- Try `card.restore` with `"delete": true` to factory reset
- Note that `card.restore` does NOT reset the I2C address — try `card.io` with `"i2c": -1` to reset address

### Hypothesis E: Code Difference Causing Init Order Issue

Despite matching the original pattern, there may be other code in the modified firmware that runs before `initializeNotecard()` and interferes with the I2C bus (e.g., analog readings, digital I/O config, other library initializations).

**Test:**
- Create a minimal "Notecard-only" test sketch with nothing but:
  ```cpp
  #include <Wire.h>
  #include <Notecard.h>
  
  Notecard notecard;
  
  void setup() {
    Serial.begin(115200);
    delay(2500);
    
    Wire.begin();
    notecard.begin(0x17);
    
    J *req = notecard.newRequest("card.status");
    J *rsp = notecard.requestAndResponse(req);
    if (rsp) {
      Serial.print("Status: ");
      Serial.println(JGetString(rsp, "status"));
      notecard.deleteResponse(rsp);
    } else {
      Serial.println("No response from Notecard");
    }
  }
  
  void loop() { }
  ```
- Flash this to the client device. If it works, the issue is in our full firmware. If it fails, the issue is hardware or Notecard-side.

### Hypothesis F: I2C Bus Scan First

Before any Notecard initialization, scan the I2C bus to see what's actually present:

```cpp
void setup() {
  Serial.begin(115200);
  delay(2500);
  
  Wire.begin();
  
  Serial.println("=== I2C Bus Scan ===");
  int devCount = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    int err = Wire.endTransmission();
    if (err == 0) {
      Serial.print("  Found device at 0x");
      if (addr < 16) Serial.print("0");
      Serial.print(addr, HEX);
      if (addr == 0x17) Serial.print(" (Notecard)");
      if (addr >= 0x0B && addr <= 0x0F) Serial.print(" (Expansion)");
      Serial.println();
      devCount++;
    }
  }
  Serial.print("Total devices: ");
  Serial.println(devCount);
  
  if (devCount == 0) {
    Serial.println("ERROR: No I2C devices found! Check wiring.");
  }
}
```

### Priority Order for Next Steps

1. **Flash I2C bus scan sketch** — determine if ANY I2C device is visible
2. **Flash minimal Notecard test** — determine if the issue is hardware or firmware
3. **Check Notecard firmware version** — compare with working server's Notecard
4. **Physical inspection** — check M.2 connector, pull-ups, wiring
5. **Swap Notecards** — definitive test for hardware vs software issue

---

## Appendix A: Reference Links

| Resource | URL |
|----------|-----|
| Blues Notecard API — Card Requests | https://dev.blues.io/api-reference/notecard-api/card-requests/ |
| note-arduino Library (Arduino) | https://dev.blues.io/tools-and-sdks/firmware-libraries/arduino-library/ |
| note-arduino GitHub | https://github.com/blues/note-arduino |
| Arduino Opta Blueprint GitHub | https://github.com/arduino-libraries/Arduino_Opta_Blueprint |
| Blues I2C Guide | https://dev.blues.io/notecard/notecard-walkthrough/essential-requests/ |
| Notecard Error Codes | https://dev.blues.io/support/notecard-error-and-status-codes/ |

## Appendix B: I2C Addresses in Use

| Address | Device | Notes |
|---------|--------|-------|
| `0x0B` | Opta Ext A0602 #1 | Analog expansion module |
| `0x17` | Blues Notecard | Default address |

## Appendix C: Error Code Reference

| Error | Meaning |
|-------|---------|
| `{io}` | I2C transport-level failure |
| `"failed to reset Notecard interface {io}"` | Wire.end()/Wire.begin() sequence failed |
| `"i2c: A call to Notecard::begin() is required. {io}"` | NoteI2c singleton is null — begin() not called |
| `{bad-bin}` | Binary data transfer error |
| `{bad-route}` | Routing error for Notes |
