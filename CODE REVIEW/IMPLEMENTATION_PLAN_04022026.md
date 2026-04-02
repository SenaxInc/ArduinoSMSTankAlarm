# Implementation Plan — TankAlarm v1.2.1 Review Findings

**Date:** April 2, 2026  
**Source:** CODE_REVIEW_04022026_COMPREHENSIVE.md (with peer cross-validation)  
**Target Version:** 1.2.2 (or 1.3.0 if relay logic changes warrant a minor bump)  
**Implementation Status:** Updated April 2, 2026  

---

## Implementation Summary

| Finding | Status | Component |
|---------|--------|-----------|
| CRITICAL-1 (HW RNG) | ✅ DONE | Server |
| CRITICAL-2 (Relay rate limit) | ✅ DONE | Client |
| CRITICAL-3 (UNTIL_CLEAR) | ✅ DONE | Client |
| HIGH-1 (Relay UID) | ✅ ALREADY IMPLEMENTED | Client |
| HIGH-2 (CORS/Security) | ✅ DONE | Server |
| HIGH-6 (PIN validation) | ✅ DONE | Server |
| HIGH-7 (SMS intent) | ✅ DONE | Server |
| HIGH-8 (Unload flags) | ✅ DONE | Client |
| MED-12 (Rate limit order) | ✅ DONE | Client |
| MED-15 (Hibernate relay) | ✅ DONE | Client |
| MED-17 (Hot-tier filter) | ✅ DONE | Server |
| HIGH-4 (Relay fwd auth) | ⏭️ DEFERRED | Server — needs config schema changes |
| HIGH-5 (String parsing) | ⏭️ DEFERRED | Server — large refactor |
| MED-13 (Manual JSON) | ⏭️ DEFERRED | Server — large refactor |
| MED-14 (Per-IP lockout) | ⏭️ DEFERRED | Server — consider for v1.4 |
| MED-16 (Batch relay) | ⏭️ DEFERRED | Client+Server — protocol change |

---

## Overview

This plan addresses **3 critical**, **8 high**, and selected medium findings from the comprehensive review. Items are grouped into implementation phases ordered by safety impact, then by complexity.

---

## Phase 1: Safety-Critical Relay Logic (CRITICAL-2, CRITICAL-3, MED-12, MED-15)

**Priority:** IMMEDIATE — these bugs affect physical equipment control  
**Component:** Client firmware  
**Risk:** LOW — changes are localized to `sendAlarm()` and `checkAlarmRateLimit()`  
**Testing:** Bench test with relay board; simulate rate-limit conditions and alarm clear sequences  

### Task 1.1 — Decouple relay actuation from rate-limited transmission (CRITICAL-2)

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Lines:** ~4710-4720  

**Current behavior:** `sendAlarm()` calls `checkAlarmRateLimit()` and returns early if rate-limited — before reaching relay control logic at line ~4808.

**Change:** Extract relay control into a separate function and call it BEFORE the rate limit check. The rate limit should only gate Notecard message transmission, not physical relay actuation.

```cpp
// In sendAlarm(), after activateLocalAlarm() and before rate limit check:
activateLocalAlarm(idx, isAlarm);
handleRemoteRelayControl(idx, alarmType, isAlarm, cfg);  // NEW

if (!checkAlarmRateLimit(idx, alarmType)) {
  return;  // Only blocks Notecard note transmission
}
```

Create `handleRemoteRelayControl()` by extracting lines ~4795-4845 (the relay control block) into a standalone function.

### Task 1.2 — Fix UNTIL_CLEAR relay clearing logic (CRITICAL-3)

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Lines:** ~4819-4826  

**Current behavior:** When `alarmType = "clear"`, the code compares `strcmp(alarmType, "high") == 0` — always false.

**Change:** When clearing, don't match against the cleared alarm type. The relay was activated for this monitor; the clear event means the condition is resolved.

```cpp
} else {
  // Alarm cleared — deactivate if UNTIL_CLEAR mode and relay is active
  if (cfg.relayMode == RELAY_MODE_UNTIL_CLEAR && gRelayActiveForMonitor[idx]) {
    shouldDeactivateRelay = true;
  }
}
```

### Task 1.3 — Fix per-monitor quota consumption order (MED-12)

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Lines:** ~4617-4645  

**Current behavior:** Per-monitor timestamp added at line ~4625, then global check at line ~4639 may reject. Per-monitor budget consumed even when global cap blocks.

**Change:** Move the per-monitor timestamp insertion to AFTER the global check succeeds. Restructure to:

```cpp
// 1. Check per-monitor hourly limit (read-only check)
if (state.alarmCount >= MAX_ALARMS_PER_HOUR) {
  return false;
}

// 2. Check global hourly limit (read-only check)
if (gGlobalAlarmCount >= MAX_GLOBAL_ALARMS_PER_HOUR) {
  return false;
}

// 3. Both passed — NOW commit timestamps to both budgets
state.alarmTimestamps[state.alarmCount++] = now;
gGlobalAlarmTimestamps[gGlobalAlarmCount++] = now;
```

### Task 1.4 — Restore relay state after critical-hibernate recovery (MED-15)

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Location:** Power state machine recovery path (transition from CRITICAL_HIBERNATE to lower state)

**Change:** When recovering from CRITICAL_HIBERNATE, re-evaluate alarm conditions for all monitors. If any alarm is still latched and has an UNTIL_CLEAR or MANUAL_RESET relay configured, reactivate the relay.

```cpp
// In power state transition handler, when exiting CRITICAL_HIBERNATE:
if (previousState == POWER_CRITICAL_HIBERNATE) {
  for (uint8_t i = 0; i < gConfig.monitorCount; i++) {
    if (gMonitorState[i].highAlarmLatched || gMonitorState[i].lowAlarmLatched) {
      // Re-evaluate relay state for monitors with latched alarms
      const MonitorConfig &cfg = gConfig.monitors[i];
      if (cfg.relayTargetClient[0] != '\0' && cfg.relayMask != 0 &&
          cfg.relayMode != RELAY_MODE_MOMENTARY) {
        triggerRemoteRelays(cfg.relayTargetClient, cfg.relayMask, true);
        gRelayActiveForMonitor[i] = true;
        gRelayActiveMaskForMonitor[i] = cfg.relayMask;
      }
    }
  }
}
```

---

## Phase 2: Notification Logic Fixes (HIGH-7, HIGH-8)

**Priority:** HIGH — SMS/email notifications silently broken for some scenarios  
**Component:** Client + Server firmware  
**Risk:** LOW — additive changes, no existing behavior disrupted  
**Testing:** Trigger unload event and verify SMS delivery; test alarm with `enableAlarmSms = false`  

### Task 2.1 — Add unload notification flags to client payload (HIGH-8)

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Lines:** ~4983 (`sendUnloadEvent()`)

**Change:** Include `sms` and `email` flags from monitor config in the unload note:

```cpp
// In sendUnloadEvent(), before publishNote():
if (cfg.unloadSmsEnabled) {
  doc["sms"] = true;
}
if (cfg.unloadEmailEnabled) {
  doc["email"] = true;
}
```

**Note:** Verify `MonitorConfig` has `unloadSmsEnabled` / `unloadEmailEnabled` fields. If not, derive from the monitor's general `enableAlarmSms` setting or add new config fields.

### Task 2.2 — Respect client SMS intent in server alarm handler (HIGH-7)

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Lines:** ~8412-8434 (`handleAlarm()`)

**Change:** Use the client's `se` (SMS escalation) flag as a necessary condition alongside the server's policy flags:

```cpp
// Current: smsEnabled = true (unconditional for alarm types)
// New: smsEnabled = clientSmsFlagPresent && serverPolicyAllows
bool clientWantsSms = doc["se"] | false;
bool serverSmsPolicy = false;
if (strcmp(alarmType, "high") == 0) serverSmsPolicy = rec->smsOnHigh;
else if (strcmp(alarmType, "low") == 0) serverSmsPolicy = rec->smsOnLow;
else if (strcmp(alarmType, "clear") == 0) serverSmsPolicy = rec->smsOnClear;

smsEnabled = clientWantsSms && serverSmsPolicy;
```

**Decision needed:** Should server policy be able to FORCE SMS even when client opts out? If so, make this `clientWantsSms || serverSmsPolicy` instead. Document the chosen behavior.

---

## Phase 3: Security Hardening (CRITICAL-1, HIGH-1, HIGH-4, HIGH-6, MED-14)

**Priority:** HIGH — authentication and authorization gaps  
**Component:** Server + Client firmware  
**Risk:** MEDIUM — PRNG change requires testing; relay auth requires config schema awareness  
**Testing:** Verify session tokens are non-sequential; test relay commands with mismatched UIDs  

### Task 3.1 — Replace LCG PRNG with hardware RNG (CRITICAL-1)

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** `generateSessionToken()`

**Change:** Use STM32H747 hardware RNG instead of LCG:

```cpp
#include "stm32h7xx_hal.h"

static void generateSessionToken() {
  RNG_HandleTypeDef hrng;
  hrng.Instance = RNG;
  if (HAL_RNG_Init(&hrng) != HAL_OK) {
    // Fallback to existing LCG if HW RNG fails
    generateSessionTokenLCG();
    return;
  }
  
  char *p = gSessionToken;
  for (int i = 0; i < SESSION_TOKEN_LENGTH; i++) {
    uint32_t rng;
    if (HAL_RNG_GenerateRandomNumber(&hrng, &rng) != HAL_OK) {
      generateSessionTokenLCG();
      return;
    }
    static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    p[i] = charset[rng % (sizeof(charset) - 1)];
  }
  p[SESSION_TOKEN_LENGTH] = '\0';
  HAL_RNG_DeInit(&hrng);
}
```

**Note:** Verify `__HAL_RCC_RNG_CLK_ENABLE()` is called at startup for the RNG peripheral.

### Task 3.2 — Add client UID verification on relay commands (HIGH-1)

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Location:** Relay command processing handler

**Change:** Before acting on a relay command, verify the `target` field matches this device:

```cpp
const char *target = doc["target"] | "";
if (target[0] != '\0' && strcmp(target, gDeviceUID) != 0) {
  Serial.println(F("Relay command for different device — ignoring"));
  return;
}
```

### Task 3.3 — Validate relay forward authorization (HIGH-4)

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** Relay forward handler (processes `relay_forward.qi`)

**Change:** Before re-issuing a relay command, verify the requesting client's config lists the target as a `relayTargetClient`:

```cpp
const char *sourceClient = doc["client"] | "";
const char *targetClient = doc["target"] | "";

// Look up source client's config and verify relay target is authorized
ClientMetadata *meta = findClientMetadata(sourceClient);
if (!meta) {
  Serial.println(F("Relay forward from unknown client — rejected"));
  return;
}
bool authorized = false;
for (uint8_t i = 0; i < meta->monitorCount; i++) {
  // Check if any monitor on this client targets the requested device
  // (Requires storing relayTargetClient in client metadata or config cache)
}
```

**Note:** This requires the server to cache each client's `relayTargetClient` values. Currently the server may not store per-monitor relay config. May need to add fields to `ClientMetadata`.

### Task 3.4 — Validate PIN digits on first login (HIGH-6)

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Lines:** ~5950

**Change:** Add `isValidPin()` call in the first-login path:

```cpp
// Current: if (strlen(pin) == 4)
// New:
if (strlen(pin) == 4 && isValidPin(pin))
```

### Task 3.5 — Consider per-IP auth lockout (MED-14)

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** Auth lockout tracking

**Change (option A — simple):** Increase `AUTH_MAX_FAILURES` to 10 and `AUTH_LOCKOUT_DURATION` to 15 seconds. This doesn't fix the root cause but reduces impact.

**Change (option B — per-IP):** Track last 4-8 IP addresses with their failure counts. Given the LAN scope, 4-8 entries is usually sufficient:

```cpp
struct AuthTracker {
  IPAddress ip;
  uint8_t failures;
  unsigned long lockoutStart;
};
static AuthTracker gAuthTrackers[8];
```

---

## Phase 4: Stability & Data Quality (HIGH-2, HIGH-5, MED-13, MED-16, MED-17)

**Priority:** MEDIUM — heap fragmentation and data correctness  
**Component:** Server firmware  
**Risk:** MEDIUM — HTTP parser refactoring is touching critical path  
**Testing:** Long-running soak test (48+ hours with periodic API calls); verify MoM/YoY data accuracy  

### Task 4.1 — Add CORS denial headers (HIGH-2)

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** HTTP response helper functions

**Change:** Add to every HTTP response:

```cpp
client.println(F("Access-Control-Allow-Origin: *"));  // Or restrict to server IP
client.println(F("X-Content-Type-Options: nosniff"));
```

For preflight OPTIONS requests, respond with 204 and appropriate headers.

### Task 4.2 — Replace String-based HTTP parsing (HIGH-5)

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** `handleWebRequests()` / HTTP request parsing

**Change:** Replace `String` concatenation with a fixed-size `char[]` buffer:

```cpp
static char httpBuffer[2048];  // Or appropriate size
uint16_t bufLen = 0;
// Read into httpBuffer with bounds checking
```

This is the largest single change and should be split into sub-tasks if needed.

### Task 4.3 — Migrate manual JSON building to ArduinoJson (MED-13)

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Lines:** ~13294+

**Change:** Replace String concatenation JSON building with `JsonDocument` serialization. Lower priority since inputs are from controlled sources, but eliminates the escaping vulnerability class entirely.

### Task 4.4 — Add month/year filtering to hot-tier analytics (MED-17)

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Lines:** ~11107 (MoM) and ~11280 (YoY)

**Change:** When aggregating hot-tier snapshots, filter by target month/year using the snapshot's `epochTime` field:

```cpp
for (uint16_t j = 0; j < hist.snapshotCount; j++) {
  uint16_t idx = (hist.writeIndex - hist.snapshotCount + j + MAX_HOURLY_HISTORY_PER_SENSOR) % MAX_HOURLY_HISTORY_PER_SENSOR;
  TelemetrySnapshot &snap = hist.snapshots[idx];
  
  // Filter by target month
  time_t snapTime = (time_t)snap.epochTime;
  struct tm *snapTm = gmtime(&snapTime);
  if (!snapTm || snapTm->tm_year + 1900 != targetYear || snapTm->tm_mon + 1 != targetMonth) {
    continue;
  }
  // ... aggregate stats
}
```

### Task 4.5 — Batch relay notes into single payload (MED-16)

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Location:** `triggerRemoteRelays()`

**Change:** Instead of emitting one `note.add` per relay bit, combine all relay states into a single note with a bitmask:

```cpp
J *body = JCreateObject();
JAddStringToObject(body, "target", targetClient);
JAddStringToObject(body, "client", gDeviceUID);
JAddNumberToObject(body, "mask", relayMask);  // Full bitmask
JAddBoolToObject(body, "state", activate);
```

Server and target client handlers would need to be updated to process the bitmask.

---

## Phase 5: Deferred Items

| Finding | Decision | Rationale |
|---------|----------|-----------|
| HIGH-3 (Viewer auth) | Deferred | Viewer is intentionally open for kiosk use; add optional PIN if requested |
| MED-5 (Config field reset) | Deferred | No reported field with this issue currently |
| MED-6 (Server vs client level) | Acknowledged | Dual-path is a calibration feature |
| MED-7 (NWS HTTP) | Acknowledged | Arduino Ethernet limitation |
| MED-8 (Viewer JSON streaming) | Deferred | Low sensor count makes OOM unlikely |
| LOW-10/11/12 | Deferred | Minimal real-world impact |

---

## Versioning Strategy

Given that Phase 1 changes relay control behavior (safety-critical), recommend bumping to **v1.3.0**:
- Phase 1 + 2 → v1.3.0-rc1 (release candidate for field testing)
- Phase 3 → v1.3.0-rc2
- Phase 4 → v1.3.0 (stable)

---

## Testing Checklist

### Phase 1 Validation
- [ ] Trigger alarm while rate limit is hit → verify relay still activates
- [ ] Activate UNTIL_CLEAR relay with HIGH trigger → clear alarm → verify relay deactivates
- [ ] Activate UNTIL_CLEAR relay with LOW trigger → clear alarm → verify relay deactivates
- [ ] Hit global alarm cap → verify per-monitor counter not consumed
- [ ] Enter CRITICAL_HIBERNATE with active relay → recover → verify relay reactivated

### Phase 2 Validation
- [ ] Trigger unload event with SMS enabled → verify SMS received
- [ ] Send alarm with `enableAlarmSms = false` → verify no SMS sent
- [ ] Send alarm with `enableAlarmSms = true` → verify SMS sent

### Phase 3 Validation
- [ ] Generate 100+ session tokens → verify no sequential patterns
- [ ] Send relay command with wrong target UID → verify ignored
- [ ] Send relay forward from unauthorized client → verify rejected
- [ ] Set PIN with non-digit characters on first login → verify rejected

### Phase 4 Validation
- [ ] Run server for 48+ hours with periodic API calls → monitor heap via `/api/debug/heap`
- [ ] Query MoM at month boundary → verify stats only include target month
- [ ] Trigger 4-relay activation → verify all relays activate within 1 poll cycle
