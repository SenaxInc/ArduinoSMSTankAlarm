# Future Improvement 3.2.5 — Startup Scan Results in Health Telemetry

**Priority:** Medium  
**Effort:** 1–2 hours  
**Risk:** Very Low — adds fields to an existing note, no behavioral change  
**Prerequisite:** None  
**Date:** 2026-02-26  

---

## Problem Statement

The I2C bus scan runs on every boot and logs results to Serial. However:

1. **Serial output is ephemeral** — only visible if someone is watching the console during boot
2. **Field devices boot unattended** — no serial monitor attached in production deployments
3. **Missing peripherals are silent** — if the A0602 isn't found on boot (loose connector), the device starts anyway with a Serial warning that nobody sees
4. **No fleet visibility** — Notehub shows the device came online but has no record of what I2C devices were (or weren't) found

Including the startup scan results in the first health telemetry report after boot would make this information visible in the Notehub dashboard, enabling remote diagnosis of field units that boot with missing peripherals.

---

## Current Startup Scan Code

```cpp
// In setup() — Client .ino ~864
uint8_t expectedAddrs[] = { NOTECARD_I2C_ADDRESS, CURRENT_LOOP_I2C_ADDRESS };
const char *expectedNames[] = { "Notecard", "A0602 Current Loop" };
uint8_t scanAttempt = 0;
bool allFound = false;

while (scanAttempt < I2C_STARTUP_SCAN_RETRIES && !allFound) {
  // ... scan logic with retry ...
  allFound = true;
  for (uint8_t idx = 0; idx < 2; idx++) {
    Wire.beginTransmission(expectedAddrs[idx]);
    uint8_t err = Wire.endTransmission();
    if (err != 0) {
      allFound = false;
    }
  }
  scanAttempt++;
}
```

The `allFound` and `scanAttempt` values are local to the scope block and discarded after the scan completes.

---

## Implementation

### Step 1 — Persist Scan Results in Globals

```cpp
// New globals to store startup scan results (Client .ino globals section)
static bool gStartupNotecardFound = false;
static bool gStartupCurrentLoopFound = false;
static uint8_t gStartupScanRetries = 0;         // Number of retry attempts used
static uint8_t gStartupUnexpectedDevices = 0;    // Count of unexpected I2C devices
static bool gStartupScanReported = false;         // Flag to send results only once
```

### Step 2 — Update Startup Scan to Store Results

```cpp
// In setup(), after the scan completes:
while (scanAttempt < I2C_STARTUP_SCAN_RETRIES && !allFound) {
  // ... existing scan logic ...
  for (uint8_t idx = 0; idx < 2; idx++) {
    Wire.beginTransmission(expectedAddrs[idx]);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      if (idx == 0) gStartupNotecardFound = true;
      if (idx == 1) gStartupCurrentLoopFound = true;
    }
    // ... existing logging ...
  }
  scanAttempt++;
}
gStartupScanRetries = scanAttempt;

// Quick scan for unexpected devices
uint8_t unexpectedCount = 0;
for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
  if (addr == NOTECARD_I2C_ADDRESS || addr == CURRENT_LOOP_I2C_ADDRESS) continue;
  Wire.beginTransmission(addr);
  if (Wire.endTransmission() == 0) {
    unexpectedCount++;
    // ... existing logging ...
  }
}
gStartupUnexpectedDevices = unexpectedCount;
```

### Step 3 — Include in First Health Telemetry Report

Modify `sendHealthTelemetry()` to include scan results on the first call after boot:

```cpp
static void sendHealthTelemetry() {
  if (!gNotecardAvailable) return;

  JsonDocument doc;
  // ... existing health fields ...

  // Include startup I2C scan results (first report only)
  if (!gStartupScanReported) {
    doc["scan_nc"] = gStartupNotecardFound;       // Notecard found?
    doc["scan_cl"] = gStartupCurrentLoopFound;    // Current loop found?
    doc["scan_retries"] = gStartupScanRetries;    // Retries used (0 = found first try)
    doc["scan_unexpected"] = gStartupUnexpectedDevices; // Unexpected device count
    gStartupScanReported = true;
  }

  publishNote(HEALTH_FILE, doc, false);
}
```

### Alternative: Dedicated Boot Report Note

Instead of adding to health telemetry, publish a one-time boot report:

```cpp
// After scan in setup(), before initializeNotecard():
// ... can't do this here because Notecard isn't initialized yet ...

// After initializeNotecard() in setup():
static void sendBootReport() {
  if (!gNotecardAvailable) return;
  
  JsonDocument doc;
  doc["c"] = gDeviceUID;
  doc["ev"] = "boot";
  doc["fw"] = FIRMWARE_VERSION;
  doc["scan_nc"] = gStartupNotecardFound;
  doc["scan_cl"] = gStartupCurrentLoopFound;
  doc["scan_retries"] = gStartupScanRetries;
  doc["scan_unexpected"] = gStartupUnexpectedDevices;
  doc["t"] = currentEpoch();
  publishNote(HEALTH_FILE, doc, false);
}
```

**Recommendation:** Include in the first health telemetry report rather than a separate note. This avoids adding a new note type and the timing issue (boot reports need Notecard initialized first, which creates a chicken-and-egg problem if the Notecard wasn't found in the scan).

---

## Telemetry Payload Impact

| Field | Type | Bytes | Frequency |
|-------|------|------:|-----------|
| `scan_nc` | bool | ~12 | First health note only |
| `scan_cl` | bool | ~12 | First health note only |
| `scan_retries` | uint8 | ~16 | First health note only |
| `scan_unexpected` | uint8 | ~19 | First health note only |
| **Total** | | ~59 | One-time per boot |

59 extra bytes on a single health note per boot — zero ongoing impact.

---

## Notehub Dashboard Usage

Operators can query boot scan data in Notehub to identify problematic devices:

```
// Find devices that booted with missing peripherals:
SELECT device, body.scan_nc, body.scan_cl, body.scan_retries
FROM health.qo
WHERE body.scan_cl = false
ORDER BY received DESC
```

This would surface field units where the A0602 expansion connector has come loose — a common issue in vibration-prone industrial environments.

---

## Memory Impact

5 new global variables:
- 2 × `bool` (2 bytes)
- 2 × `uint8_t` (2 bytes)  
- 1 × `bool` (1 byte)

**Total: 5 bytes of SRAM** — negligible.

---

## Testing Plan

| Test | Procedure | Pass Criteria |
|------|-----------|---------------|
| All devices found | Boot with everything connected | `scan_nc=true`, `scan_cl=true`, `scan_retries=1` |
| A0602 missing | Boot without A0602 | `scan_cl=false`, `scan_retries=3` |
| Retry visible | Boot with A0602 connected loosely (intermittent) | `scan_retries > 1`, `scan_cl=true` |
| Unexpected device | Connect extra I2C device | `scan_unexpected > 0` |
| First report only | Check multiple health notes | `scan_*` fields only in first note |

---

## Files Affected

| File | Change |
|------|--------|
| Client .ino | +5 globals, ~10 lines in scan block, ~5 lines in `sendHealthTelemetry()` |

No configuration changes needed — scan results are unconditionally included in the first health note.
