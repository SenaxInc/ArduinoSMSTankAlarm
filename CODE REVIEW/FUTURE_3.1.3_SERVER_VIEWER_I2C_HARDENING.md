# Future Improvement 3.1.3 — Server/Viewer I2C Hardening

**Priority:** High  
**Effort:** 4–8 hours  
**Risk:** Low — well-understood patterns, grid-powered devices  
**Prerequisite:** 3.1.2 (Extract I2C into `TankAlarm_I2C.h`) must be completed first  
**Date:** 2026-02-26  

---

## Problem Statement

The Server and Viewer sketches communicate with the Blues Notecard over I2C (address 0x17) but have **zero I2C recovery infrastructure**. If the I2C bus hangs or the Notecard becomes unresponsive, these sketches will silently stop communicating with the cellular network and never recover.

While Server and Viewer devices are typically grid-powered (less susceptible to the brown-out-induced I2C glitches that affect the battery-powered Client), they are still vulnerable to:

1. **ESD events** — static discharge near I2C lines can lock up the bus
2. **Notecard firmware hangs** — rare but documented in Blues forums
3. **Long-running deployments** — Server devices may run for months without reboot
4. **I2C bus noise** — industrial environments with motors, pumps, solenoids

The Server is especially critical because it manages the fleet — if it loses Notecard connectivity, no alarms reach the cloud.

---

## Current State: Server Sketch

### I2C Usage (from `TankAlarm-112025-Server-BluesOpta.ino`)

| Location | Usage | Error Handling |
|----------|-------|----------------|
| Line ~2342 | `Wire.begin()` in `setup()` | None |
| Line ~5047 | `notecard.begin(NOTECARD_I2C_ADDRESS)` | None |
| Line ~5050-5076 | `hub.set`, `hub.get` in setup | Checks response, logs error, continues |
| Line ~2168 | `note.add` for alarm relay | Checks `sendRequest()` return |
| Line ~2193 | `requestAndResponse()` for status | Checks null response |
| Line ~2232 | `note.add` for viewer summary | Checks `sendRequest()` return |

**What's missing:**
- No `recoverI2CBus()` function
- No Notecard health check loop
- No failure counter or offline mode
- No retry on I2C failure
- No bus scan on startup
- No watchdog (Server runs Ethernet + web server, watchdog could be added)

---

## Current State: Viewer Sketch

### I2C Usage (from `TankAlarm-112025-Viewer-BluesOpta.ino`)

| Location | Usage | Error Handling |
|----------|-------|----------------|
| Line ~221 | `Wire.begin()` in `setup()` | None |
| Line ~285 | `notecard.begin(NOTECARD_I2C_ADDRESS)` | None |
| Line ~288-322 | `hub.set`, `hub.get` in setup | Checks response, logs error |
| Line ~608-645 | `note.get` to fetch viewer summary | Checks response, logs error |
| Line ~716-773 | `dfu.status` check | Checks response error |

**What's missing:** Same as Server — no recovery, no health check, no offline mode.

---

## Implementation Plan

### Phase 1 — After TankAlarm_I2C.h Extraction (3.1.2)

Once the shared `TankAlarm_I2C.h` header exists, add to both Server and Viewer:

#### 1a. Global Error Counters

```cpp
// In Server/Viewer sketch globals section
uint32_t gCurrentLoopI2cErrors = 0;   // Not used by Server/Viewer but required by header
uint32_t gI2cBusRecoveryCount = 0;
static bool gNotecardAvailable = true;
static uint16_t gNotecardFailureCount = 0;
```

#### 1b. Startup Bus Scan

Add the shared `scanI2CBus()` call after `Wire.begin()` in `setup()`:

```cpp
Wire.begin();

// Verify Notecard is present on I2C bus
const uint8_t expectedAddrs[] = { NOTECARD_I2C_ADDRESS };
const char *expectedNames[] = { "Notecard" };
I2CScanResult scan = scanI2CBus(expectedAddrs, expectedNames, 1);
if (!scan.allFound) {
  Serial.println(F("WARNING: Notecard not found on I2C bus"));
}
```

**Note:** Server and Viewer do NOT have a current loop expansion, so only scan for Notecard (0x17).

#### 1c. Notecard Health Check

Add a periodic health check in `loop()`:

```cpp
// Server/Viewer: Check Notecard health every 5 minutes
static unsigned long lastNcHealthCheck = 0;
if (millis() - lastNcHealthCheck > 300000UL) {
  lastNcHealthCheck = millis();
  if (!gNotecardAvailable) {
    J *req = notecard.newRequest("card.version");
    if (req) {
      J *rsp = notecard.requestAndResponse(req);
      if (rsp) {
        notecard.deleteResponse(rsp);
        gNotecardAvailable = true;
        gNotecardFailureCount = 0;
        notecard.begin(NOTECARD_I2C_ADDRESS);
        Serial.println(F("Notecard recovered"));
      } else {
        gNotecardFailureCount++;
        if (gNotecardFailureCount >= I2C_NOTECARD_RECOVERY_THRESHOLD) {
          recoverI2CBus(false);  // No DFU on Server/Viewer
          notecard.begin(NOTECARD_I2C_ADDRESS);
          gNotecardFailureCount = 0;
        }
      }
    }
  }
}
```

#### 1d. Wrap Notecard Calls with Failure Detection

For each existing `notecard.requestAndResponse()` or `notecard.sendRequest()` call, add failure tracking:

```cpp
// Before (Server example):
J *rsp = notecard.requestAndResponse(req);
if (!rsp) {
  Serial.println(F("Notecard request failed"));
  return;
}

// After:
J *rsp = notecard.requestAndResponse(req);
if (!rsp) {
  gNotecardFailureCount++;
  if (gNotecardFailureCount >= NOTECARD_FAILURE_THRESHOLD && gNotecardAvailable) {
    gNotecardAvailable = false;
    Serial.println(F("Notecard unavailable - requests will be retried"));
  }
  return;
}
gNotecardFailureCount = 0;
if (!gNotecardAvailable) {
  gNotecardAvailable = true;
  Serial.println(F("Notecard recovered"));
}
```

---

## Server-Specific Considerations

### Web UI Status Indicator

The Server's web UI already shows Notecard connection status (the "I2C OK — Notecard responding" / "I2C FAILED" indicator). Currently this is checked on-demand via an API endpoint. After hardening, the status should reflect `gNotecardAvailable` for immediate accuracy:

```cpp
// In the /api/notecard/status handler:
void handleNotecardStatus() {
  JsonDocument doc;
  doc["connected"] = gNotecardAvailable;
  // ... existing fields ...
}
```

### Alarm Relay Impact

The Server relays alarms from Client devices to SMS contacts. If Notecard is unavailable, incoming alarm notes will queue in the Notecard's inbound file and be processed when connectivity is restored. No data loss occurs, but there will be a delay in alarm notifications while the Notecard is down.

**Recommendation:** Add a local alarm buffer similar to the Client's `bufferNoteForRetry()` for critical outbound messages (SMS trigger notifications to Notehub).

---

## Viewer-Specific Considerations

### Display Behavior During Outage

The Viewer fetches tank summary data from the Notecard every poll interval. During a Notecard outage:
- The display should show the last known data with a "stale" indicator
- The last-update timestamp tells the operator how old the data is
- No I2C recovery is visible on the display (Serial only)

### Minimal Impact

The Viewer is read-only — it doesn't publish any notes, only reads. I2C recovery is still important to prevent permanent display freeze, but the consequence of a stuck bus is less severe than for the Server.

---

## Testing Plan

| Test | Device | Procedure | Pass Criteria |
|------|--------|-----------|---------------|
| Server bus recovery | Server Opta | Pull Notecard I2C wires for 60s, reconnect | Serial shows recovery, web UI shows "Connected" |
| Server alarm relay during outage | Server Opta | Disconnect Notecard, trigger Client alarm, reconnect | Alarm processed after reconnection |
| Server startup scan | Server Opta | Boot with Notecard connected | "0x17 Notecard - OK" in Serial |
| Server startup scan (missing) | Server Opta | Boot without Notecard | Retry log, "WARNING: Notecard not found" |
| Viewer bus recovery | Viewer Opta | Pull I2C wires, reconnect | Display resumes updating |
| Viewer stale data indicator | Viewer Opta | Pull I2C wires for 30min | Display shows last-known data |

---

## Estimated Line Changes

| File | Added | Net |
|------|------:|----:|
| Server .ino | +60 | +60 |
| Viewer .ino | +40 | +40 |
| **Total** | +100 | **+100** |

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Recovery disrupts active HTTP request | Low | Medium — client timeout | Health check runs in main loop, not in HTTP handlers |
| Watchdog not available on Server | N/A | None | Server is grid-powered, no watchdog needed but could be added later |
| Multiple `notecard.begin()` calls (see 3.1.1) | Low | Low | Audit 3.1.1 first |
| Recovery pins different on Server hardware | Very Low | High | Uses `PIN_WIRE_SCL`/`PIN_WIRE_SDA` from board variant |

---

## References

- 3.1.2 — Extract I2C Header (prerequisite)
- 3.1.1 — Notecard begin() Idempotency Audit (recommended prerequisite)
- Client sketch I2C hardening (already implemented — reference implementation)
- `TankAlarm-112025-Server-BluesOpta.ino` — Server sketch
- `TankAlarm-112025-Viewer-BluesOpta.ino` — Viewer sketch
