# Bugfix: Watchdog Starvation During Boot & Long-Running Loops (02/26/2026)

## Symptom
Client reboots immediately after printing `Sending boot telemetry...` on the serial monitor. The device never reaches `Client setup complete`.

## Root Cause
The 30-second Mbed watchdog is started in `setup()` but never kicked again before `setup()` returns. The boot telemetry path calls `sampleTanks()` → `sendTelemetry()` → `publishNote()` → `notecard.requestAndResponse()`, which is a blocking I2C transaction that can take up to 30 seconds when the Notecard modem is busy (e.g., establishing cellular connectivity at boot). The accumulated time from watchdog enable through initialization and the Notecard transaction exceeds the 30-second window, triggering a hardware reset.

A full audit revealed the same class of bug in several other functions across both Client and Server firmware.

## Files Changed
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`

## Fixes Applied

### 1. Client `setup()` — Boot Telemetry (Original Crash)
**Location:** Before the `sampleTanks()` / `sendRegistration()` block at end of `setup()`
**Fix:** Added `mbedWatchdog.kick()` to reset the 30-second window before the slow code path begins.

### 2. Client `publishNote()` — Before Blocking I2C Transaction
**Location:** Before `notecard.requestAndResponse(req)` in `publishNote()`
**Fix:** Added `mbedWatchdog.kick()` before every blocking Notecard send. This protects all callers (boot telemetry, alarms, daily reports, etc.) — not just the boot path.

### 3. Server `setup()` — Post-Watchdog Notecard Calls
**Location:** Before `checkServerVoltage()` + `checkForFirmwareUpdate()` at end of `setup()`
**Fix:** Added `mbedWatchdog.kick()` before the two back-to-back `requestAndResponse` calls (`card.voltage` and `dfu.status`).

### 4. Client `flushBufferedNotes()` — Unbounded Note Flush Loop
**Location:** Both Mbed and LittleFS code paths in `flushBufferedNotes()`
**Risk:** Loops over all buffered notes (potentially 80+), each performing a blocking `sendRequest` I2C call with no watchdog kick and no iteration cap.
**Fix:**
- Added `mbedWatchdog.kick()` at the top of each loop iteration.
- Added a cap of 20 notes per flush call. Remaining notes are preserved in the buffer file for the next flush opportunity.

### 5. Client `pollForSerialRequests()` — Unbounded `while(true)` Loop
**Location:** `pollForSerialRequests()` function
**Risk:** `while(true)` loop with `requestAndResponse` per iteration and no exit condition other than an empty notefile.
**Fix:**
- Changed `while(true)` to `while(processed < 5)` with a counter.
- Added `mbedWatchdog.kick()` per iteration.

### 6. Server `purgePendingConfigNotes()` — Delete Loop
**Location:** The `for` loop deleting up to 20 stale config notes via `note.delete`
**Risk:** Up to 20 blocking `requestAndResponse` calls in sequence with no watchdog kick.
**Fix:** Added `mbedWatchdog.kick()` at the top of each loop iteration.

### 7. Server `dispatchPendingConfigs()` — Multi-Client Config Retry
**Location:** The `for` loop retrying config dispatch for each pending client
**Risk:** Each iteration calls `sendConfigViaNotecard()` which itself chains `purgePendingConfigNotes()` (up to 21 I2C calls) + `note.add` (1 more call). Multiple clients compound the time.
**Fix:** Added `mbedWatchdog.kick()` before each client dispatch.

## Functions Verified Safe (No Fix Needed)

| Function | Reason |
|---|---|
| Server `processNotefile()` | Already calls `safeSleep(1)` per iteration which kicks watchdog; capped at `MAX_NOTES_PER_FILE_PER_POLL` (10) |
| Client `sendDailyReport()` | Calls `publishNote()` per chunk, which now has the watchdog kick from fix #2 |
| Server `pollNotecard()` | Chains 9 `processNotefile()` calls — each one kicks internally |
| Viewer `setup()` | Watchdog starts *after* all blocking Notecard calls complete |
| Client/Server `safeSleep()` | Already chunks sleep into half-watchdog intervals with kicks |

## Design Pattern
All fixes follow the same pattern — a platform-guarded watchdog kick:
```cpp
#ifdef TANKALARM_WATCHDOG_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    mbedWatchdog.kick();
  #else
    IWatchdog.reload();
  #endif
#endif
```

## Testing
- Verify Client boots cleanly past `Sending boot telemetry...` and reaches `Client setup complete`
- Verify Server boots cleanly past `checkServerVoltage()` / `checkForFirmwareUpdate()`
- Stress-test `flushBufferedNotes()` with >20 buffered notes — confirm it flushes 20, preserves the rest, and completes without reset
- Verify `pollForSerialRequests()` processes up to 5 requests and exits
- Verify `dispatchPendingConfigs()` with multiple pending clients does not cause reset
