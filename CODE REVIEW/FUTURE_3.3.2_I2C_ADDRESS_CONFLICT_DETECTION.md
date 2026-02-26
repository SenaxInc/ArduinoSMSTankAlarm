# Future Improvement 3.3.2 — I2C Address Conflict Detection

**Priority:** Low  
**Effort:** 1–2 hours  
**Risk:** Very Low — detection only, no behavioral change  
**Prerequisite:** None  
**Date:** 2026-02-26  

---

## Problem Statement

The TankAlarm system uses two I2C devices with fixed addresses:
- **Blues Notecard:** 0x17 (fixed by Blues hardware, not changeable)
- **Arduino Pro Opta Ext A0602:** 0x64 (factory default, may be configurable via DIP switches or firmware on some variants)

While these addresses are well-separated (0x17 vs 0x64), misconfiguration scenarios exist:

1. **Wrong expansion module** — An I2C expansion module other than the A0602 is installed that happens to use 0x17 or conflicts with the Notecard
2. **Custom I2C device** — A future sensor or peripheral is added to the bus with an overlapping address
3. **A0602 address changed** — Some expansion modules allow address reconfiguration; if the A0602 is set to 0x17, it conflicts with the Notecard
4. **Multiple A0602 modules** — Two A0602 modules on the same bus both at 0x64 would cause data corruption without errors

These conflicts produce subtle, hard-to-diagnose symptoms: intermittent NACK errors, corrupted readings, one device working while the other fails "randomly."

---

## Current Startup Scan Behavior

The existing startup bus scan (implemented in 1.8.4 + 1.9.1) checks:
1. Whether expected devices respond (Notecard at 0x17, A0602 at 0x64)
2. Whether any unexpected devices are found on the bus (0x08–0x77)

**What it does NOT detect:**
- Two devices responding at the same address
- A wrong device at a correct address (e.g., a temperature sensor responding at 0x64 instead of the A0602)
- Intermittent address conflicts where one device overwhelms the other

---

## Implementation

### Phase 1: Address Uniqueness Validation

After the startup scan, verify that no unexpected devices share addresses with expected ones:

```cpp
// In setup(), after the startup bus scan:

// Validate no address conflicts
bool conflictDetected = false;

// Check 1: Verify Notecard address is unique
// The Notecard should be the ONLY device at 0x17
// Read a known response to validate identity
{
  J *req = notecard.newRequest("card.version");
  if (req) {
    J *rsp = notecard.requestAndResponse(req);
    if (rsp) {
      const char *device = JGetString(rsp, "device");
      if (device && strstr(device, "notecard") != nullptr) {
        Serial.println(F("  0x17 identity confirmed: Blues Notecard"));
      } else {
        Serial.println(F("WARNING: Device at 0x17 is NOT a Notecard!"));
        conflictDetected = true;
      }
      notecard.deleteResponse(rsp);
    }
  }
}

// Check 2: Verify A0602 identity
// Read channel 0 — the A0602 should return a 2-byte value in 4-20mA range
{
  Wire.beginTransmission(CURRENT_LOOP_I2C_ADDRESS);
  Wire.write((uint8_t)0);  // Channel 0
  uint8_t err = Wire.endTransmission(false);
  if (err == 0) {
    uint8_t bytesReceived = Wire.requestFrom(CURRENT_LOOP_I2C_ADDRESS, (uint8_t)2);
    if (bytesReceived == 2) {
      uint8_t high = Wire.read();
      uint8_t low = Wire.read();
      uint16_t raw = ((uint16_t)high << 8) | low;
      // A0602 raw values should be in a plausible range (not 0xFFFF or 0x0000 exactly)
      if (raw == 0xFFFF || raw == 0x0000) {
        Serial.println(F("WARNING: Device at 0x64 may not be A0602 (suspect raw value)"));
      } else {
        Serial.println(F("  0x64 identity likely: A0602 (plausible raw data)"));
      }
    }
  }
}

// Check 3: Scan for duplicate device responses
// If the same address produces different responses on consecutive reads,
// it suggests two devices are fighting for the address
{
  uint8_t testAddrs[] = { NOTECARD_I2C_ADDRESS, CURRENT_LOOP_I2C_ADDRESS };
  for (uint8_t i = 0; i < 2; i++) {
    uint8_t responses = 0;
    // Try 5 rapid probes — a conflicting device may respond intermittently
    for (uint8_t probe = 0; probe < 5; probe++) {
      Wire.beginTransmission(testAddrs[i]);
      if (Wire.endTransmission() == 0) responses++;
      delayMicroseconds(100);
    }
    if (responses > 0 && responses < 5) {
      Serial.print(F("WARNING: Intermittent response at 0x"));
      Serial.print(testAddrs[i], HEX);
      Serial.print(F(" ("));
      Serial.print(responses);
      Serial.println(F("/5 probes) — possible address conflict"));
      conflictDetected = true;
    }
  }
}

if (conflictDetected) {
  Serial.println(F("I2C ADDRESS CONFLICT DETECTED — check wiring and device configuration"));
  // Optionally: set a global flag for health telemetry
  gI2cAddressConflictDetected = true;
}
```

### Phase 2: Runtime-Configurable Address Support

The `gConfig.currentLoopI2cAddress` field already supports runtime address override. Add validation:

```cpp
// In applyConfigUpdate():
if (!doc["currentLoopI2cAddr"].isNull()) {
  uint8_t newAddr = doc["currentLoopI2cAddr"];
  
  // Refuse if it conflicts with Notecard
  if (newAddr == NOTECARD_I2C_ADDRESS) {
    Serial.println(F("ERROR: Current loop address 0x17 conflicts with Notecard"));
    // Don't apply
  } else if (newAddr < 0x08 || newAddr > 0x77) {
    Serial.println(F("ERROR: Current loop address out of valid I2C range"));
  } else {
    gConfig.currentLoopI2cAddress = newAddr;
    Serial.print(F("Current loop I2C address updated to 0x"));
    Serial.println(newAddr, HEX);
  }
}
```

---

## Known I2C Address Ranges

| Range | Category | Common Devices |
|-------|----------|---------------|
| 0x00-0x07 | Reserved | General call, CBUS, different bus format |
| 0x08-0x0F | Rare | SMBus host, SMBus alert response |
| 0x10-0x1F | Sensors | Notecard (0x17), some IMUs |
| 0x20-0x3F | I/O expanders | PCF8574 (0x20-0x27), MCP23017 |
| 0x40-0x4F | DACs, sensors | INA219 (0x40-0x4F), PCA9685 |
| 0x50-0x5F | EEPROMs | AT24C32 (0x50-0x57) |
| 0x60-0x6F | Misc | A0602 (0x64), MCP4725 DAC |
| 0x70-0x77 | Reserved/displays | LCD, general call reset |
| 0x78-0x7F | Reserved | 10-bit addressing |

The Notecard (0x17) and A0602 (0x64) are in well-separated ranges — accidental conflict requires deliberate misconfiguration or wrong hardware.

---

## Alert Integration

Report address conflicts in health telemetry:

```cpp
// In sendHealthTelemetry():
if (gI2cAddressConflictDetected) {
  doc["i2c_conflict"] = true;
}
```

And optionally as an alarm:

```cpp
if (gI2cAddressConflictDetected) {
  JsonDocument doc;
  doc["c"] = gDeviceUID;
  doc["s"] = gConfig.siteName;
  doc["y"] = "i2c-conflict";
  doc["t"] = currentEpoch();
  publishNote(ALARM_FILE, doc, true);
}
```

---

## Testing Plan

| Test | Procedure | Pass Criteria |
|------|-----------|---------------|
| Normal boot (no conflict) | Standard hardware setup | No warnings, no conflict flag |
| Wrong device at 0x64 | Connect non-A0602 I2C device at 0x64 | Warning logged, suspect raw value |
| Intermittent device | Loose I2C connection | Intermittent probe warning |
| Address override validation | Set currentLoopI2cAddr to 0x17 via config | Error logged, address not changed |
| Health telemetry flag | Cause conflict, check health note | `i2c_conflict: true` in note |

---

## Files Affected

| File | Change |
|------|--------|
| Client .ino | ~40 lines in `setup()` for conflict detection, +1 global, +1 line in health telemetry |
| `TankAlarm_Config.h` | No changes needed |

---

## Limitation

True address conflict detection on I2C is fundamentally limited — if two devices respond identically at the same address, there's no electrical way to distinguish them. The detection here relies on behavioral heuristics (intermittent responses, unexpected data patterns) rather than definitive identification. For production-critical installations, physical wiring verification remains the gold standard.
