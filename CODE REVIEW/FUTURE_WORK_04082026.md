# Future Work — Deferred Code Review Items

**Date:** April 8, 2026  
**Source:** CODE_REVIEW_04082026_COMPREHENSIVE.md  
**Firmware versions at time of review:** Common 1.5.0, Server 1.3.0, Client 1.4.0, Viewer 1.4.0

This document contains detailed implementation plans for 8 findings from the April 2026 comprehensive code review that were deferred due to their scope — each requires architectural changes, new features, config schema modifications, or platform-level work that goes beyond targeted bug fixes.

---

## Table of Contents

1. [CRIT-01/02: DFU A/B Partitioning and Firmware Integrity Verification](#crit-0102-dfu-ab-partitioning-and-firmware-integrity-verification)
2. [HIGH-03: Note Idempotency System](#high-03-note-idempotency-system)
3. [MED-02: Max Relay ON Duration Safety Timeout](#med-02-max-relay-on-duration-safety-timeout)
4. [MED-03: Separate Debounce Counters for High/Low Alarm Clear](#med-03-separate-debounce-counters-for-highlow-alarm-clear)
5. [MED-04: STM32duino Non-Atomic Write Audit](#med-04-stm32duino-non-atomic-write-audit)
6. [MED-06: Manual Relay Bookkeeping Unification](#med-06-manual-relay-bookkeeping-unification)
7. [LOW-03/04/06: Documentation Items](#low-030406-documentation-items)
8. [Copilot Review and Suggested Execution Path](#copilot-review-and-suggested-execution-path)
9. [GPT-5.3-Codex Addendum: Final Review and Execution Blueprint](#gpt-53-codex-addendum-final-review-and-execution-blueprint)
10. [Gemini 3.1 Pro (Preview) Review and Execution Plan](#gemini-31-pro-preview-review-and-execution-plan)
11. [Final Review — Synthesized Assessment and Implementation Plan](#final-review--synthesized-assessment-and-implementation-plan)
12. [Implementation Review](#implementation-review)

---

## CRIT-01/02: DFU A/B Partitioning and Firmware Integrity Verification

**Severity:** Critical  
**Affected files:** `TankAlarm-112025-Common/src/TankAlarm_DFU.h`  
**Affected sketches:** Client and Viewer only (Server uses Notecard Outboard DFU via `card.dfu`, unaffected)  
**Risk:** A power loss or I2C failure during firmware update permanently bricks the device, requiring JTAG/SWD recovery in the field.

### Problem Statement

The current IAP (In-Application Programming) update function `tankalarm_performIapUpdate()` follows a dangerous erase-then-download sequence:

1. **Step 1 (L320–L363):** Erases the *entire* application flash region in one pass via `flash.erase(eraseAddr, eraseSize)` — all application sectors wiped at once.
2. **Step 2 (L371–L478):** Downloads firmware chunk-by-chunk from the Notecard via `dfu.get` → base64 decode → `flash.program()`, 128 bytes at a time.
3. **Step 3 (L501–L509):** Immediately reboots via `NVIC_SystemReset()` without any CRC/hash verification of the written firmware.

If Step 2 fails at any point (I2C bus hang, Notecard timeout, power loss, base64 decode error, flash program failure), the application flash is partially written or blank. The failure path at `iap_restore_hub:` (L533–L554) restores the Notecard hub mode but **cannot restore the erased application code**. The STM32H747 bootloader at 0x08000000–0x0803FFFF survives (it's below the erase region), but the device has no recoverable application.

Additionally, even when the download completes, no integrity check is performed. A single bit-flip during flash programming — caused by electrical noise, marginal flash cell, or incomplete erase — goes undetected and could cause undefined behavior or a crash loop on reboot.

### Proposed Solution: Dual-Slot A/B Partitioning with CRC Verification

#### Flash Memory Layout (STM32H747XI — 2 MB Internal Flash)

The STM32H747XI has 2 MB of internal flash organized in two 1 MB banks. Current application binaries compile to approximately 500–700 KB. The proposed layout:

| Region | Address Range | Size | Purpose |
|--------|--------------|------|---------|
| Bootloader | 0x08000000 – 0x0803FFFF | 256 KB | STM32 bootloader (untouched) |
| Slot A (active) | 0x08040000 – 0x080BFFFF | 512 KB | Currently running application |
| Slot B (staging) | 0x080C0000 – 0x0813FFFF | 512 KB | New firmware downloaded here |
| Metadata | 0x08140000 – 0x08140FFF | 4 KB | Slot metadata: CRC, version, boot count, active slot flag |
| Warm tier storage | 0x08141000 – 0x081FFFFF | ~764 KB | LittleFS daily summaries (existing) |

> **Note:** If application binaries grow beyond 512 KB, the slot sizes need to be adjusted by reducing the warm tier storage allocation. Monitor compiled binary sizes during development.

#### Implementation Plan

**Phase 1: CRC Verification (addresses CRIT-02 only, minimal change)**

This is a quick win that can be implemented independently before the full A/B partitioning:

1. **After the flash write loop completes** (after L478 in `tankalarm_performIapUpdate()`), read back the entire written region and compute a CRC32.
2. **Obtain the expected CRC** from the Notecard. The `dfu.status` response includes firmware metadata — check if it provides a checksum. If not, compute a CRC32 of the downloaded chunks during the write loop (accumulate as each chunk is decoded) and then verify the flash read-back matches.
3. **If CRC mismatch:** Do NOT reboot. Restore hub mode, log the error, set `gDfuError` to indicate verification failure. The device continues running the (now-erased) application... which is still a brick. This is why Phase 2 (A/B) is needed for true safety.
4. **If CRC match:** Proceed with reboot as normal.

Estimated code changes:
- Add a `uint32_t crc32` accumulation variable in the download loop
- Add a flash read-back and CRC comparison after the loop
- Add a verification failure error path

**Phase 2: A/B Slot Architecture (addresses CRIT-01)**

1. **Add slot metadata struct** to `TankAlarm_DFU.h`:
   ```cpp
   struct DfuSlotMetadata {
     uint32_t magic;           // 0xDFUAB001
     uint32_t crc32;           // CRC32 of firmware in this slot
     uint32_t firmwareSize;    // Bytes of firmware written
     uint32_t bootCount;       // Incremented each boot, reset on confirmed-good
     char version[32];         // Firmware version string
     uint8_t activeSlot;       // 0 = Slot A, 1 = Slot B
     uint8_t confirmed;        // 1 = confirmed good, 0 = pending verification
     uint8_t padding[22];      // Pad to 96 bytes for alignment
   };
   ```

2. **Modify `tankalarm_performIapUpdate()` to write to the inactive slot:**
   - Determine active slot from metadata (default: Slot A)
   - Erase the *inactive* slot only
   - Download and write firmware to the inactive slot
   - Compute CRC32 during download; verify flash read-back matches
   - If verification passes: update metadata to mark the inactive slot as `activeSlot` with `confirmed = 0`
   - Reboot

3. **Add a boot verification stage** (in each sketch's `setup()`, before `ensureConfigLoaded()`):
   - Read slot metadata
   - If `confirmed == 0`: increment `bootCount`
     - If `bootCount > 3` (three failed boot attempts): revert `activeSlot` to the other slot, reset `bootCount`, reboot
     - If application reaches a "known good" checkpoint (e.g., successful Notecard communication in `setup()`): set `confirmed = 1`, reset `bootCount`
   - This provides automatic rollback if the new firmware crashes during boot

4. **Modify the bootloader vector table jump** (or use an early `setup()` trampoline) to select the active slot based on metadata. On STM32H747, this can be done by:
   - Reading the metadata sector at a fixed address
   - Setting the VTOR (Vector Table Offset Register) to point to the active slot's vector table
   - Jumping to the active slot's reset handler

5. **Add a "confirm firmware" API endpoint** on the Server (`POST /api/dfu/confirm`) that the web UI can call after verifying the device is operating correctly post-update. This sets `confirmed = 1` for the active slot.

#### Testing Strategy

- **Unit test CRC32:** Verify the CRC implementation against known test vectors.
- **Simulated power loss:** Start a DFU update, interrupt power during the download phase, verify the device boots from the original slot.
- **Simulated corruption:** Write a known-bad firmware with wrong CRC to the staging slot, verify the device detects the mismatch and does not reboot into the bad slot.
- **Rollback test:** Deploy a firmware that crashes in `setup()`, verify the boot counter increments and the device automatically reverts after 3 attempts.
- **Full cycle test:** Deploy a valid firmware update, verify the device boots into the new slot, confirm the firmware via API, then deploy another update to verify the slot swap works in both directions.

#### Dependencies and Risks

- Requires understanding the STM32H747 flash bank architecture and sector sizes (128 KB sectors in Bank 2)
- Bootloader modification or VTOR trampoline is platform-specific and requires careful testing
- LittleFS partition may need relocation if slot sizes need to be larger than 512 KB
- The Notecard `dfu.get` API may need to support offset-based reads if it doesn't already (for resumable downloads)
- Must not break the Server's Outboard DFU path (`card.dfu name=stm32`)

---

## HIGH-03: Note Idempotency System

**Severity:** High  
**Affected files:** `TankAlarm-112025-Server-BluesOpta.ino` (primary), `TankAlarm-112025-Viewer-BluesOpta.ino` (secondary)  
**Risk:** A crash between processing and deleting a note causes duplicate processing on next boot — duplicate SMS alerts, duplicate alarm log entries, or duplicate daily report emails.

### Problem Statement

The Server's `processNotefile()` function (L8037–L8097) uses a peek-then-delete pattern for processing Notecard notes:

1. `note.get` (peek, without `delete:true`) — reads the note body
2. Handler function processes the note (e.g., `handleAlarm()` dispatches SMS)
3. `note.get` with `delete:true` — consumes the note

If the device crashes or resets between steps 2 and 3, the note remains in the Notecard queue and will be re-processed on the next boot. This is a classic TOCTOU (Time-of-Check-Time-of-Use) race condition.

The Server processes 9 notefiles through this pattern:

| Notefile | Handler | Duplicate Impact |
|----------|---------|-----------------|
| `telemetry.qi` | `handleTelemetry()` | **Low** — upserts sensor record (idempotent) |
| `alarm.qi` | `handleAlarm()` | **High** — triggers SMS dispatch (duplicate SMS) |
| `daily.qi` | `handleDaily()` | **Medium** — triggers email aggregation |
| `unload.qi` | `handleUnload()` | **Medium** — logs unload event twice |
| `serial_log.qi` | `handleSerialLog()` | **Low** — appends to log |
| `serial_ack.qi` | `handleSerialAck()` | **Low** — updates state |
| `relay_forward.qi` | `handleRelayForward()` | **Medium** — could activate relay twice |
| `location_resp.qi` | `handleLocationResponse()` | **Low** — updates cache |
| `config_ack.qi` | `handleConfigAck()` | **Low** — updates state |

The Viewer has the same pattern in `fetchViewerSummary()` (L761–L852), though it was partially fixed in the current release to only delete on successful parse. The Viewer impact is lower since it only processes read-only sensor summaries.

The Client's `publishNote()` uses `note.add` (outbound, fire-and-forget) and is unaffected. The Client's `pollForRelayCommands()` correctly uses `note.get` with `delete:true` in a single call.

### Proposed Solution: Persistent Idempotency Key Tracking

#### Approach A: "Delete-First" Pattern (Simplest, Some Trade-offs)

Reverse the order: delete the note first, then process it. If the device crashes after deletion but before processing, the note is lost (not duplicated). This trades "at-least-once" for "at-most-once" delivery.

**Pros:** Minimal code change — swap the `note.get delete:true` to be the first call, then process the body from the response.  
**Cons:** A crash after deletion but before processing silently loses the note. For alarm notifications, this could mean a missed SMS — arguably worse than a duplicate.

**Assessment:** Not recommended for alarm and relay handlers. Acceptable for telemetry, logs, and acks.

#### Approach B: Idempotency Key Tracking (Recommended)

Add a per-note unique identifier and track which notes have been successfully processed in persistent storage.

**Step 1: Add unique IDs to outbound notes (Client side)**

In the Client's `publishNote()` (L6449), add an `_nid` (note ID) field to every note body:

```cpp
// Add idempotency key: epoch + monotonic counter
static uint32_t noteSeq = 0;
doc["_nid"] = String((uint32_t)currentEpoch()) + "-" + String(noteSeq++);
```

The `_nid` value is a combination of the epoch timestamp and a monotonic counter, making it unique per session. Even after a reboot (counter resets to 0), the epoch value provides uniqueness because the same epoch won't produce the same counter value.

**Step 2: Track processed note IDs on the Server**

Add a small persistent "processed notes" ring buffer to the Server:

```cpp
// Circular buffer of recently processed note IDs (persisted to LittleFS)
static const uint8_t MAX_PROCESSED_NOTE_IDS = 64;
static char gProcessedNoteIds[MAX_PROCESSED_NOTE_IDS][24]; // epoch-counter strings
static uint8_t gProcessedNoteIdCount = 0;
static uint8_t gProcessedNoteIdWriteIndex = 0;
```

Persist this to `/fs/processed_notes.json` on LittleFS. Load it in `setup()`, save it after each note is processed. Use a ring buffer so old entries are automatically aged out.

**Step 3: Check before processing in `processNotefile()`**

```cpp
// In processNotefile(), after deserializing the note body:
const char *nid = doc["_nid"] | "";
if (nid[0] != '\0' && isNoteAlreadyProcessed(nid)) {
    Serial.print(F("Skipping duplicate note: "));
    Serial.println(nid);
    // Still delete the note from the Notecard to drain the queue
    deleteNote(notefile);
    continue;
}

// Process the note...
handler(doc, epoch);

// Mark as processed BEFORE deleting (so a crash between here and delete
// just means we skip it next time instead of re-processing)
markNoteProcessed(nid);

// Now delete the note from the Notecard
deleteNote(notefile);
```

**Step 4: Persistence**

- Save the processed IDs ring buffer to LittleFS every N notes (e.g., every 10) to reduce flash wear, and on clean shutdown
- On boot, load the ring buffer from LittleFS
- The ring buffer automatically handles the case of very old note IDs being evicted — if a note somehow survives in the Notecard queue longer than 64 notes, the duplicate check will miss it, but this is an extreme edge case

**Step 5: Backward compatibility**

Notes without an `_nid` field (from older Client firmware) should be processed without idempotency checking. The `_nid` check is only applied when the field is present.

#### Testing Strategy

- **Normal operation:** Verify notes are processed exactly once and `_nid` values are recorded
- **Simulated crash:** Process a note, kill the Server before deletion, restart, verify the note is skipped on re-processing
- **Ring buffer overflow:** Process 65+ notes, verify the oldest ID is evicted and the 65th note processes correctly
- **Mixed firmware:** Send notes from an old Client (no `_nid`) and a new Client (with `_nid`), verify both are handled correctly
- **Persistence test:** Process notes, reboot the Server, verify processed IDs survive reboot

#### Dependencies

- Requires Client firmware update to add `_nid` to note payloads (increases payload size by ~15 bytes per note)
- Requires `NOTEFILE_SCHEMA_VERSION` bump since payload format changes
- LittleFS write for persistence adds flash wear (mitigated by batching writes)
- Ring buffer size of 64 IDs uses ~1.5 KB RAM

---

## MED-02: Max Relay ON Duration Safety Timeout

**Severity:** Medium  
**Affected files:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Risk:** In `RELAY_MODE_MANUAL_RESET`, if the Notecard loses cellular connectivity, the server's reset command never arrives and the relay stays ON indefinitely — potentially running a pump dry, overflowing a tank, or wasting power.

### Problem Statement

The Client supports three relay modes (defined in `RelayMode` enum, L449–L453):

| Mode | Behavior | Auto-Off? |
|------|----------|-----------|
| `RELAY_MODE_MOMENTARY` (0) | Auto-off after configured duration | Yes — `checkRelayMomentaryTimeout()` handles this |
| `RELAY_MODE_UNTIL_CLEAR` (1) | Stays on until alarm condition clears | Yes — clears when sensor reading returns to normal |
| `RELAY_MODE_MANUAL_RESET` (2) | Stays on until server sends explicit reset | **No** — requires remote command |

When `RELAY_MODE_MANUAL_RESET` is active, the only ways to turn off the relay are:

1. **Remote reset command** via `processRelayCommand()` (L7058–L7066) — requires `relay_reset_sensor` field in a note from the server. This path depends on Notecard cellular connectivity.
2. **Local clear button** via `clearButtonPin` (L720–L725) — requires a physical hardware button to be wired and configured. Defaults to `-1` (disabled) in `ClientConfig`.
3. **Device reboot** — relay state is not persisted across reboots (hardware relays default OFF).

There is no safety timeout. If the Notecard goes offline (cellular outage, SIM issue, antenna damage), the relay remains energized indefinitely.

### Proposed Solution: Configurable Max ON Duration with Grace Period

#### Config Schema Changes

Add a new field to `MonitorConfig` (after `relayMomentarySeconds[4]` at L486):

```cpp
uint32_t relayMaxOnSeconds;  // Maximum ON duration for MANUAL_RESET mode (0 = no limit)
```

- Default value: `0` (no limit — preserves existing behavior for backward compatibility)
- Reasonable range: 0–604800 (0 to 7 days)
- Config schema version bump: `CONFIG_SCHEMA_VERSION` 1 → 2 (or increment existing version)

Add the corresponding JSON config field in `parseMonitorFromJson()` (around L2249):

```cpp
if (t["relayMaxOnSeconds"].is<uint32_t>()) {
    mon.relayMaxOnSeconds = t["relayMaxOnSeconds"].as<uint32_t>();
}
```

And in `serializeConfig()` (around L2747):

```cpp
t["relayMaxOnSeconds"] = cfg.monitors[i].relayMaxOnSeconds;
```

#### Runtime Logic Changes

Modify `checkRelayMomentaryTimeout()` (L7179–L7229) to also check `RELAY_MODE_MANUAL_RESET`:

```cpp
// After the existing RELAY_MODE_MOMENTARY check:
if (cfg.relayMode == RELAY_MODE_MANUAL_RESET && cfg.relayMaxOnSeconds > 0) {
    for (uint8_t r = 0; r < MAX_RELAYS; r++) {
        if (!(cfg.relayMask & (1 << r))) continue;
        if (!gRelayState[r]) continue;
        if (gRelayActivationTime[r] == 0) continue;
        
        unsigned long elapsed = millis() - gRelayActivationTime[r];
        if (elapsed >= (unsigned long)cfg.relayMaxOnSeconds * 1000UL) {
            Serial.print(F("SAFETY: Relay "));
            Serial.print(r);
            Serial.println(F(" max ON duration exceeded — forcing OFF"));
            setRelayState(r, false);
            gRelayActivationTime[r] = 0;
            
            // Publish a safety-timeout alarm note to the server
            publishRelayTimeoutAlarm(i, r, cfg.relayMaxOnSeconds);
        }
    }
}
```

> **Important dependency:** This requires MED-06 (manual relay bookkeeping) to be implemented first, so that `gRelayActivationTime[r]` is populated for manual relay commands. Without MED-06, the activation time is never set for the manual path.

#### Server UI Changes

Add the `relayMaxOnSeconds` field to the Config Generator page (Server embedded HTML):

- Add a numeric input in the relay configuration section
- Label: "Max ON Duration (Manual Reset)" with a help tooltip explaining the safety purpose
- Show a warning when set to 0: "No safety limit — relay may stay ON indefinitely if server unreachable"
- Suggested presets: 1 hour (3600), 4 hours (14400), 8 hours (28800), 24 hours (86400)

#### Notification on Safety Timeout

When the safety timeout fires, the Client should:

1. Turn off the relay
2. Publish a `relay_timeout.qo` note to the server with details (monitor index, relay number, duration exceeded)
3. Log a WARNING to Serial output
4. If alarm SMS is enabled for the monitor, queue an SMS alert indicating the safety timeout fired

The Server should:

1. Log the timeout event in the transmission log
2. Show a banner/toast on the dashboard if a relay safety timeout was triggered recently
3. Send an email notification if configured

#### Testing Strategy

- **Basic timeout:** Configure a relay with `RELAY_MODE_MANUAL_RESET` and `relayMaxOnSeconds = 60`, trigger the relay, verify it auto-turns off after 60 seconds
- **Zero value:** Verify `relayMaxOnSeconds = 0` preserves the existing no-limit behavior
- **Connectivity test:** Enable the relay, disable Notecard (disconnect antenna), wait for timeout, verify the relay turns off even with no server connectivity
- **Notification test:** Verify the `relay_timeout.qo` note is published when the timeout fires
- **Config round-trip:** Set the value via Config Generator, push to Client, verify it persists and is honored

#### Dependencies

- **MED-06 must be implemented first** — manual relay commands currently don't populate `gRelayActivationTime[]`
- Config schema version increment
- Server Config Generator UI update
- New outbound notefile (`relay_timeout.qo`) needs a server handler

---

## MED-03: Separate Debounce Counters for High/Low Alarm Clear

**Severity:** Medium  
**Affected file:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Risk:** If a sensor reading oscillates between high and low alarm zones within the hysteresis band, the shared `clearDebounceCount` is continuously reset, potentially preventing alarm clear indefinitely.

### Problem Statement

The `MonitorRuntime` struct (L570–L575) has separate debounce counters for alarm *activation* but a single shared counter for alarm *clear*:

```cpp
uint8_t highAlarmDebounceCount;   // Counts consecutive high-alarm readings
uint8_t lowAlarmDebounceCount;    // Counts consecutive low-alarm readings
uint8_t clearDebounceCount;       // ← Shared between high clear AND low clear
```

In the alarm evaluation logic (L4495–L4555), the flow is:

1. If high alarm condition is true → `state.clearDebounceCount = 0` (reset clear counter)
2. If high alarm condition is false (in clear zone) → `state.clearDebounceCount++`
3. If low alarm condition is true → `state.clearDebounceCount = 0` (reset clear counter **again**)
4. If low alarm condition is false (in clear zone) → `state.clearDebounceCount++`
5. Final guard (L4551): `if (highCondition || lowCondition) state.clearDebounceCount = 0`

**The bug scenario:** A sensor oscillates such that every other reading falls in the high alarm zone and the clear zone. Each time the high alarm condition fires, it resets `clearDebounceCount` to 0. The counter never reaches `ALARM_DEBOUNCE_COUNT` (3), so the alarm never clears. The same pattern applies to oscillation between low alarm and clear zones.

This is most likely to occur with sensors that have noisy output near the alarm threshold — exactly the scenario where debounce is most important.

### Proposed Solution: Split into Three Counters

#### Struct Change

Replace the single `clearDebounceCount` with two separate clear counters:

```cpp
struct MonitorRuntime {
    // ... existing fields ...
    uint8_t highAlarmDebounceCount;     // Counts consecutive high-alarm readings
    uint8_t lowAlarmDebounceCount;      // Counts consecutive low-alarm readings
    uint8_t highClearDebounceCount;     // Counts consecutive high-clear readings
    uint8_t lowClearDebounceCount;      // Counts consecutive low-clear readings
};
```

#### Logic Change in Alarm Evaluation (analog/current loop path, L4495–L4555)

The current logic block needs to split: high alarm evaluation only touches `highClearDebounceCount`, and low alarm evaluation only touches `lowClearDebounceCount`. The two paths become fully independent:

```
HIGH ALARM PATH:
  if (highCondition) {
      state.highAlarmDebounceCount++;
      state.highClearDebounceCount = 0;   // Only reset HIGH clear counter
      if (state.highAlarmDebounceCount >= ALARM_DEBOUNCE_COUNT) {
          triggerHighAlarm();
      }
  } else if (alarmActive && alarmType == HIGH) {
      state.highClearDebounceCount++;     // Only increment HIGH clear counter
      state.highAlarmDebounceCount = 0;
      if (state.highClearDebounceCount >= ALARM_DEBOUNCE_COUNT) {
          clearHighAlarm();
      }
  }

LOW ALARM PATH:
  if (lowCondition) {
      state.lowAlarmDebounceCount++;
      state.lowClearDebounceCount = 0;    // Only reset LOW clear counter
      if (state.lowAlarmDebounceCount >= ALARM_DEBOUNCE_COUNT) {
          triggerLowAlarm();
      }
  } else if (alarmActive && alarmType == LOW) {
      state.lowClearDebounceCount++;      // Only increment LOW clear counter
      state.lowAlarmDebounceCount = 0;
      if (state.lowClearDebounceCount >= ALARM_DEBOUNCE_COUNT) {
          clearLowAlarm();
      }
  }
```

#### All Code Locations Requiring Changes

The `clearDebounceCount` field is referenced at these locations — all need to be updated:

| Line(s) | Context | Change |
|---------|---------|--------|
| L570–575 | `MonitorRuntime` struct definition | Add `highClearDebounceCount`, `lowClearDebounceCount`, remove `clearDebounceCount` |
| L1325 | Initialization in `setup()` | Initialize both new counters to 0 |
| L3628, L3870 | Power state recovery | Reset both new counters to 0 |
| L4467–L4490 | Digital sensor alarm path | Split into separate counters |
| L4505–L4555 | Analog/current loop alarm path | Split into separate counters per the logic above |

#### Edge Case: Simultaneous High and Low Conditions

In theory, a sensor cannot be simultaneously above the high threshold and below the low threshold. However, if the high threshold is set *below* the low threshold (misconfiguration), the code should handle it gracefully. With separate counters, both debounce independently, and the last-triggered alarm wins — which is the existing behavior.

#### Testing Strategy

- **Oscillation test:** Simulate a sensor that alternates between 100" (high alarm zone at 96") and 90" (clear zone) every sample. Verify the high alarm eventually clears after 3 consecutive readings in the clear zone, even though the low alarm zone is never entered.
- **Noise test:** Simulate a sensor with ±5" random noise centered on the high alarm threshold. Verify alarms activate and clear within a reasonable number of samples.
- **Concurrent alarms:** If high and low thresholds are configured close together, verify each alarm path operates independently.
- **Regression test:** Run the existing alarm test vectors from the test plan to verify no behavioral changes for non-oscillating sensors.

---

## MED-04: STM32duino Non-Atomic Write Audit

**Severity:** Medium  
**Affected files:** `TankAlarm-112025-Common/src/TankAlarm_Platform.h`, all sketch files  
**Status:** Partially resolved — audit needed to confirm full coverage

### Current State

The original review finding (non-atomic flash writes on STM32duino platforms) appears to have been **already addressed** in the codebase:

1. **Mbed/Opta path:** Uses `tankalarm_posix_write_file_atomic()` — writes to a `.tmp` file, then `rename()` to the final path. This is POSIX-atomic.
2. **STM32duino path:** Uses `tankalarm_littlefs_write_file_atomic()` (defined in `TankAlarm_Platform.h` L278–L313) — writes to a `.tmp` file, then `LittleFS.rename()`. This is atomic at the LittleFS level.
3. **Volatile pulse counters:** `gRpmAccumulatedPulses[MAX_MONITORS]` (L736 in Client) is accessed via `noInterrupts()`/`interrupts()` guards in all atomic helper functions (L740–L766). This is correct for Cortex-M7.

### Remaining Audit Items

Despite the core paths being addressed, a comprehensive audit should verify:

1. **All file write paths use the atomic helpers.** Search for any direct `fopen()/fclose()` or `LittleFS.open()/close()` patterns that bypass the atomic write functions. Files to check:
   - Config saves in Server, Client, Viewer
   - Calibration data saves (`saveCalibrationData()`)
   - History/daily summary file writes
   - Sensor registry saves
   - Any file writes in `TankAlarm_DFU.h`

2. **No raw `volatile` access without interrupt guards.** Search for any `volatile` variables that are read/written without `noInterrupts()` protection. On ARM Cortex-M7, 32-bit aligned reads/writes are atomic at the hardware level, but read-modify-write sequences (e.g., `gCounter++`) are not.

3. **STM32duino LittleFS.rename() atomicity guarantee.** Verify that the underlying LittleFS implementation on STM32duino provides atomic rename semantics. If the rename crosses block boundaries, it may not be truly atomic on power loss. The Mbed OS POSIX `rename()` delegates to the filesystem driver — check that LittleFS guarantees rename atomicity (it should, as rename is a metadata-only operation in LittleFS).

4. **Server-specific file operations.** The Server runs on Mbed OS (Arduino Opta) and uses POSIX file I/O throughout. Verify that the warm-tier daily summary writes (`saveDailySummary()`) and FTP archive writes also use atomic patterns.

### Recommended Action

This item should be treated as an **audit task**, not a code change. Create a checklist of all file write operations across all three sketches, verify each uses the appropriate atomic helper, and document any gaps. If any gaps are found, wrap them in the existing `tankalarm_posix_write_file_atomic()` or `tankalarm_littlefs_write_file_atomic()` functions.

---

## MED-06: Manual Relay Bookkeeping Unification

**Severity:** Medium  
**Affected file:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Risk:** Manual relay commands from the Server bypass all timeout and state bookkeeping, making relays activated by manual command immune to momentary auto-off, invisible to the timeout checker, and creating an inconsistent state machine.

### Problem Statement

When a relay is activated via the **alarm path** (`sendAlarm()` at L4808–L4820), three bookkeeping updates occur:

```cpp
gRelayActiveForMonitor[idx] = true;           // L4810 — marks monitor has active relay
gRelayActiveMaskForMonitor[idx] = cfg.relayMask;  // L4811 — which relay bits are active
gRelayActivationTime[r] = activateTime;       // L4816 — when each relay was turned on
```

When a relay is activated via the **manual command path** (`processRelayCommand()` at L7035–L7121), only the physical relay state is changed:

```cpp
setRelayState(relayNum, state);  // L7096 — sets GPIO and gRelayState[] only
```

None of the three bookkeeping arrays are updated. This causes:

| Behavior | Alarm Path | Manual Path |
|----------|-----------|-------------|
| `checkRelayMomentaryTimeout()` auto-off | ✅ Works | ❌ Never fires — `gRelayActiveForMonitor` is false |
| `gRelayActivationTime[r]` tracking | ✅ Set correctly | ❌ Always 0 — duration unknown |
| `gRelayActiveMaskForMonitor[idx]` tracking | ✅ Set correctly | ❌ Always 0 — mask unknown |
| Dashboard relay status display | ✅ Shows active | ❌ Shows inconsistent state |

Additionally, `processRelayCommand()` reads a `"duration"` parameter (L7105–L7112) but only logs "ignored — use relay modes instead" — this is dead code (LOW-03).

### Proposed Solution: Unified Relay Activation Function

#### Step 1: Create a shared `activateRelay()` function

Extract the bookkeeping logic from `sendAlarm()` into a shared function that both the alarm path and the manual command path can call:

```cpp
// Shared relay activation with full bookkeeping
static void activateRelayForMonitor(uint8_t monitorIdx, uint8_t relayMask, 
                                     unsigned long activateTime) {
    for (uint8_t r = 0; r < MAX_RELAYS; r++) {
        if (!(relayMask & (1 << r))) continue;
        setRelayState(r, true);
        gRelayActivationTime[r] = activateTime;
    }
    if (monitorIdx < MAX_MONITORS) {
        gRelayActiveForMonitor[monitorIdx] = true;
        gRelayActiveMaskForMonitor[monitorIdx] = relayMask;
    }
}

// Shared relay deactivation with full bookkeeping
static void deactivateRelayForMonitor(uint8_t monitorIdx, uint8_t relayMask) {
    for (uint8_t r = 0; r < MAX_RELAYS; r++) {
        if (!(relayMask & (1 << r))) continue;
        setRelayState(r, false);
        gRelayActivationTime[r] = 0;
    }
    if (monitorIdx < MAX_MONITORS) {
        gRelayActiveForMonitor[monitorIdx] = false;
        gRelayActiveMaskForMonitor[monitorIdx] = 0;
    }
}
```

#### Step 2: Update the alarm path (`sendAlarm()`)

Replace the inline bookkeeping at L4808–L4820 with a call to `activateRelayForMonitor()`.

#### Step 3: Update the manual command path (`processRelayCommand()`)

At L7096, replace the bare `setRelayState(relayNum, state)` with:

```cpp
if (state) {
    uint8_t mask = 1 << relayNum;
    // Find the monitor index that maps to this relay (if any)
    uint8_t monitorIdx = findMonitorForRelay(relayNum);
    activateRelayForMonitor(monitorIdx, mask, millis());
} else {
    uint8_t mask = 1 << relayNum;
    uint8_t monitorIdx = findMonitorForRelay(relayNum);
    deactivateRelayForMonitor(monitorIdx, mask);
}
```

The `findMonitorForRelay()` helper scans `gConfig.monitors[]` to find which monitor has the given relay in its `relayMask`. If no monitor maps to the relay (standalone manual control), use `MAX_MONITORS` as a sentinel to skip the per-monitor bookkeeping.

#### Step 4: Implement the `"duration"` parameter (resolve LOW-03)

Replace the dead code at L7105–L7112 with an actual implementation:

```cpp
if (!doc["duration"].isNull() && state) {
    uint16_t duration = doc["duration"].as<uint16_t>();
    if (duration > 0 && duration <= 86400) {  // Max 24 hours for safety
        // Override the momentary timeout for this activation
        MonitorConfig &cfg = gConfig.monitors[monitorIdx];
        // Store the custom duration in gRelayActivationTime 
        // and set a one-time custom momentary duration
        gRelayCustomDurationSec[relayNum] = duration;
    }
}
```

This requires a new `gRelayCustomDurationSec[MAX_RELAYS]` array that `checkRelayMomentaryTimeout()` checks as an override.

#### Step 5: Update `resetRelayForMonitor()` (L7232–L7260)

The existing `resetRelayForMonitor()` already handles proper bookkeeping cleanup. Verify it uses `deactivateRelayForMonitor()` after this refactor.

#### Testing Strategy

- **Manual ON/OFF:** Send a manual relay command from the server, verify `gRelayActiveForMonitor` and `gRelayActivationTime` are populated
- **Manual with momentary mode:** Configure a monitor with `RELAY_MODE_MOMENTARY` and 30-second duration, send a manual ON command, verify the relay auto-turns off after 30 seconds
- **Manual with duration:** Send `{"relay": 1, "state": true, "duration": 60}`, verify the relay turns off after 60 seconds
- **Alarm then manual:** Trigger an alarm-driven relay, then send a manual reset command, verify all bookkeeping is cleaned up
- **Dashboard consistency:** Verify the Server dashboard shows correct relay state for both alarm-driven and manual activations

---

## LOW-03/04/06: Documentation Items

These three items are documentation and design clarification tasks that don't require code changes.

### LOW-03: Document the Dead `duration` Parameter in `processRelayCommand()`

**File:** `TankAlarm-112025-Client-BluesOpta.ino`, L7105–L7112  
**Current state:** The `duration` field is read from the relay command note, logged as "ignored — use relay modes instead", and discarded.

**If MED-06 is implemented:** This becomes moot — the duration parameter will be implemented as part of the manual relay bookkeeping unification.

**If MED-06 is NOT implemented:** Add a code comment explaining why the parameter is intentionally ignored and what the intended future behavior is. The current "Note: Custom duration ... ignored" log message is user-facing and may confuse operators checking Serial logs. Either:
- Remove the dead code entirely (just delete the `if (!doc["duration"]...)` block)
- Or add a `// TODO: Implement as part of MED-06 relay bookkeeping unification` comment

### LOW-04: Document PRNG Security Model for `generateSessionToken()`

**File:** `TankAlarm-112025-Server-BluesOpta.ino`, `generateSessionToken()` at L752–L788

**Current entropy sources:**
- `micros()` — microsecond timer (2 reads at L768, L773)
- `millis()` — millisecond timer (L774)
- `analogRead(A0)` — VIN voltage ADC (L769)
- `analogRead(A1/A2/A3)` — three floating ADC pins for noise (L770–L772)
- 64-bit seed mixed with prime shifts (L775–L777)
- Knuth MMIX LCG: multiplier `6364136223846793005`, increment `1442695040888963407` (L779)
- Output: 16 hex characters (64-bit token space)

**What to document (add as a comment block above `generateSessionToken()`):**

```cpp
/**
 * Session Token PRNG Security Model
 * 
 * THREAT MODEL: LAN-only access control for a single-operator industrial kiosk.
 * The token protects the web dashboard served over Ethernet to devices on the
 * same physical LAN segment. Internet exposure is not supported.
 * 
 * ENTROPY: 6 entropy sources (4 ADC + 2 timers) combined into a 64-bit seed.
 * ADC noise on floating pins provides ~2-4 bits of entropy per read on STM32H7.
 * Timer values provide uniqueness across boots. Total practical entropy: ~20-30 bits.
 * 
 * PRNG: Knuth MMIX LCG with full 64-bit period. Not cryptographically secure.
 * Acceptable for this deployment because:
 *   1. Attacker must be on the same physical LAN
 *   2. Session tokens rotate on every login
 *   3. Single concurrent session enforced (new login invalidates previous)
 *   4. Rate limiting prevents brute-force (MED-01 fixes)
 *   5. Only 4-digit PIN protects the dashboard — the PRNG is not the weakest link
 * 
 * IF INTERNET EXPOSURE IS ADDED: Replace with hardware RNG (STM32H7 has a TRNG
 * peripheral at RNG_BASE = 0x48021800) or use the Notecard's entropy endpoint.
 * The constant-time comparison in sessionTokenMatches() is already timing-safe.
 */
```

### LOW-06: Document Viewer Authentication By-Design Decision

**File:** `TankAlarm-112025-Viewer-BluesOpta.ino`, file header (L1–L15)

**Current state:** The Viewer has no authentication. Its HTTP handler serves:
- `GET /` → read-only dashboard HTML
- `GET /api/sensors` → sensor data JSON
- Everything else → 404

There are no POST handlers, no PIN validation, no session tokens, and no relay/config write endpoints. The Viewer receives data from the Server via Notecard (`viewer_summary.qi`), not from user input. An attacker on the LAN can only read the same sensor data displayed on the kiosk screen.

**What to document (add to the file header comment block):**

```
Security Model:
- This device is a read-only display kiosk. No authentication is implemented.
- No control paths (relay commands, configuration changes, firmware updates)
  are exposed via the HTTP interface.
- All data flows one-way: Server → Notecard → Viewer. The Viewer cannot
  modify server state or send commands to clients.
- The HTTP server is LAN-only (Ethernet, no WiFi). Physical network access
  is required to reach the dashboard.
- If authentication is needed for the Viewer in the future (e.g., for
  internet-exposed deployments), implement the same PIN + session token
  model used by the Server.
- MAX_HTTP_BODY_BYTES (1024) provides basic DoS protection for the HTTP parser.
```

---

## Implementation Priority and Dependencies

```
Priority Order:
  ┌─────────────────────────────────────────────┐
  │ 1. MED-06  Manual relay bookkeeping          │ ← Unblocks MED-02, resolves LOW-03
  │ 2. MED-02  Max relay ON duration              │ ← Depends on MED-06
  │ 3. MED-03  Separate debounce counters         │ ← Independent
  │ 4. HIGH-03 Note idempotency                   │ ← Independent (schema change)
  │ 5. CRIT-02 CRC verification (Phase 1)         │ ← Quick win, independent
  │ 6. CRIT-01 A/B partitioning (Phase 2)         │ ← Depends on CRIT-02
  │ 7. MED-04  Atomic write audit                 │ ← Audit only, independent
  │ 8. LOW-*   Documentation items                │ ← Independent, can be done anytime
  └─────────────────────────────────────────────┘

Dependency Graph:
  MED-06 ──→ MED-02
  MED-06 ──→ LOW-03 (resolved by MED-06)
  CRIT-02 ──→ CRIT-01

Config Schema Impact:
  MED-02 requires CONFIG_SCHEMA_VERSION bump
  HIGH-03 requires NOTEFILE_SCHEMA_VERSION bump
```

---

## Copilot Review and Suggested Execution Path

### Overall Review

- This document is targeting the right backlog items. The main changes I would make are to the implementation order and a few design assumptions before coding starts.
- The highest-value changes are the relay/runtime foundation, server-side note idempotency, and DFU integrity verification.
- Three design corrections should be made up front:
  1. The `CRIT-01` A/B-slot plan is not implementable as written until image placement is proven. The current Arduino application is linked for a fixed flash base today; selecting a different slot by changing `VTOR` alone is not enough unless the image is built to run from that alternate address.
  2. The `CRIT-01` example layout uses 512 KB slots while this document also states current binaries are approximately 500-700 KB. That means the proposed slot size can already be too small for current builds.
  3. The `HIGH-03` batching suggestion (saving processed IDs every 10 notes) is too weak for high-impact handlers. A crash after SMS/email/relay side effects but before the next flush can still duplicate the event on reboot.
- One scope adjustment: the Viewer is no longer the primary idempotency risk. Its current `fetchViewerSummary()` path already counts consecutive parse failures and deletes a poison note after three failures, so the Server should remain the main target for durable dedupe work.

### Recommended Delivery Order

1. `MED-06` first. It is the relay-state foundation and it unblocks `MED-02`.
2. `MED-02` second. Once relay ownership and activation timestamps are unified, the safety timeout is a small extension.
3. `MED-03` third. This is an isolated Client-side logic fix with no schema or route changes.
4. `HIGH-03` fourth. It is a larger feature, but mostly independent once relay behavior is stable.
5. `CRIT-02` fifth. Add end-to-end verification now, even if true rollback takes longer.
6. `CRIT-01` sixth. Only start after a design spike resolves slot sizing and image-addressing questions.
7. `MED-04` and `LOW-*` last. These are mostly audit/commentary tasks and can run in parallel with code review.

### Detailed Suggestions

#### CRIT-01/02: DFU A/B Partitioning and Firmware Integrity Verification

**Review:**

- The current risk assessment is correct.
- I do not recommend starting with executable A/B slots until the image-address problem is answered.
- I also do not recommend using "download CRC equals flash read-back CRC" as the only integrity source. That only proves the bytes were written consistently, not that the firmware matches the intended release artifact.

**Suggested plan:**

1. Add a short architecture spike before any flash-layout change.
    - Produce current Client and Viewer build sizes and linker outputs for release builds.
    - Confirm whether the build can emit slot-specific images or whether the code is fixed-address only.
    - Decide between two implementation families:
      - True dual executable slots with separate link targets and boot metadata.
      - Verified staging slot plus a fixed-address install/copy stub in a safe boot area.
2. Implement `CRIT-02` as an integrity phase that is useful regardless of the final install model.
    - On the Server, compute and persist the expected `firmwareSize` plus `crc32` or `sha256` when firmware is uploaded/staged.
    - Send that expected hash to the Client/Viewer as part of DFU metadata instead of discovering it from the downloaded bytes.
    - In `TankAlarm_DFU.h`, add helpers such as:
      - `tankalarm_crc32_update(...)`
      - `tankalarm_read_flash_crc32(...)`
      - `tankalarm_verify_programmed_image(...)`
    - Split `tankalarm_performIapUpdate()` into smaller steps: enter DFU mode, download/program, verify, finalize status, restore hub mode.
3. Only then implement the safe-install model selected in step 1.
    - If dual-slot is feasible:
      - add slot metadata read/write helpers
      - add slot-specific linker addresses or equivalent build artifacts
      - add boot-attempt counting and rollback confirmation
    - If dual-slot is not feasible:
      - add a staging region
      - add a recovery/install stub that verifies the staged image before erasing the active slot
      - make the copy/install step resumable or at least detect interrupted install state

**Code changes to plan for:**

- `TankAlarm-112025-Common/src/TankAlarm_DFU.h`
  - Refactor `tankalarm_performIapUpdate()`
  - Add image-hash helpers and structured failure codes
  - Add metadata helpers if slot/staging manifests are introduced
- `TankAlarm-112025-Client-BluesOpta.ino`
  - Add an early boot verification/confirmation hook in `setup()`
  - Surface verification/install status to diagnostics if needed
- `TankAlarm-112025-Viewer-BluesOpta.ino`
  - Add the same early boot verification/confirmation hook as the Client
- Server DFU upload/state code
  - Persist the expected artifact hash and version metadata
  - Expose install/confirm state if rollback/confirmation is implemented

#### HIGH-03: Note Idempotency System

**Review:**

- The problem statement is correct.
- I would not use `_nid = epoch + counter` by itself because `currentEpoch()` may be unavailable or repeated across reboot windows.
- I would not batch-persist processed IDs for high-impact notefiles.

**Suggested plan:**

1. Generate a stable outbound ID once, before serialization and before buffering.
    - Preferred format: `<deviceUid>:<bootNonce>:<seq>`.
    - Acceptable fallback: `<deviceUid>:<bootEpoch>:<seq>`.
    - Keep `_nid` in the buffered JSON so retries reuse the same ID instead of generating a new one.
2. Centralize note stamping in `publishNote()`.
    - Create a working document or envelope that adds `_sv` and `_nid`.
    - Serialize and buffer/send that stamped payload, not the raw caller document.
3. On the Server, implement a durable processed-note journal.
    - Add `isProcessedNoteId(...)`, `appendProcessedNoteId(...)`, `loadProcessedNoteJournal()`, and periodic compaction.
    - Store IDs in an append-only log or ring file on LittleFS or `/fs`.
4. Use different durability rules by notefile risk.
    - High risk: `alarm.qi`, `daily.qi`, `unload.qi`, `relay_forward.qi`
      - persist the processed ID immediately after successful handler side effects and before note deletion
    - Lower risk: `telemetry.qi`, `serial_log.qi`, `serial_ack.qi`, `location_resp.qi`, `config_ack.qi`
      - optional simpler path such as delete-first or RAM-backed dedupe
5. Keep Viewer work lightweight.
    - The current `summaryParseFailCount` poison-note path already lowers operational risk.
    - Only add full durable dedupe to the Viewer if duplicate summary side effects become important later.

**Code changes to plan for:**

- `TankAlarm-112025-Client-BluesOpta.ino`
  - `publishNote()`
  - boot-level nonce/sequence initialization in `setup()`
  - retry-buffer format if buffered notes must preserve `_nid`
- `TankAlarm-112025-Server-BluesOpta.ino`
  - `processNotefile()`
  - new processed-note journal helpers near other persistence helpers
  - optional per-handler delivery policy comments
- `TankAlarm-112025-Viewer-BluesOpta.ino`
  - optional only; no urgent architectural change required

#### MED-06 and MED-02: Relay Runtime Foundation and Safety Timeout

**Review:**

- These two items should be implemented together, not separately.
- The proposed `findMonitorForRelay()` first-match helper is only safe if the config guarantees that one relay bit belongs to one monitor. Without that guarantee, timeout and clear behavior remain ambiguous.

**Suggested plan:**

1. Normalize relay state around a relay-centric runtime structure.

```cpp
struct RelayRuntime {
  bool active;
  uint8_t ownerMonitor;       // MAX_MONITORS = no owner
  uint8_t source;             // alarm, manual, clear-button, reset
  unsigned long activatedAt;
  uint32_t customDurationSec; // 0 = none
};
```

2. Decide relay ownership semantics up front.
    - Preferred rule: reject overlapping `relayMask` ownership across monitors in config validation and Config Generator UI.
    - Alternative: support shared ownership with reference counts. This is more complex and probably not worth it here.
3. Add shared helpers and move all existing paths to them.
    - `activateRelayMaskForMonitor(...)`
    - `deactivateRelayMaskForMonitor(...)`
    - `applyManualRelayCommand(...)`
    - `clearRelayRuntime(...)`
4. After that foundation is in place, add `relayMaxOnSeconds`.
    - Add the field to `MonitorConfig`
    - Parse it in `parseMonitorFromJson()`
    - Serialize it in `saveConfigToFlash()`
    - Expose it in Config Generator and cloud-config JSON
5. Reuse an existing notefile if possible for timeout notifications.
    - I would prefer reusing the existing alarm path with `y: "relay_timeout"` instead of introducing a brand-new `relay_timeout.qo` route, unless operational separation is required.
    - That keeps routing, parsing, and notification code smaller.

**Code changes to plan for:**

- `TankAlarm-112025-Client-BluesOpta.ino`
  - `MonitorConfig`
  - `processRelayCommand()`
  - `sendAlarm()`
  - `checkRelayMomentaryTimeout()`
  - `resetRelayForMonitor()`
  - config parse/save helpers
  - any status-report code that reads relay bookkeeping arrays
- `TankAlarm-112025-Server-BluesOpta.ino`
  - Config Generator page and JSON schema
  - alarm handler / dashboard banner for `relay_timeout`
  - config validation so overlapping relay ownership is caught before deploy

#### MED-03: Separate Debounce Counters for High/Low Alarm Clear

**Review:**

- This is a clean, low-risk fix and a good candidate for an isolated PR.
- The minimal version is enough; no need to redesign the whole alarm engine in the same patch.

**Suggested plan:**

1. Replace `clearDebounceCount` with `highClearDebounceCount` and `lowClearDebounceCount`.
2. Update all init/reset paths that currently zero `clearDebounceCount`.
3. Update the analog/current-loop evaluation so high and low clear paths stop touching the same counter.
4. Update the digital path explicitly.
    - Digital sensors only have one meaningful alarm-clear path today, so using one of the new clear counters there is fine as long as the code comment makes that explicit.
5. Optional follow-up, not required in the same PR:
    - Replace `highAlarmLatched` plus `lowAlarmLatched` with a single latched-alarm enum to remove impossible mixed states.

**Code changes to plan for:**

- `TankAlarm-112025-Client-BluesOpta.ino`
  - `MonitorRuntime`
  - `setup()` initialization
  - power-state recovery/reset blocks
  - digital alarm evaluation
  - analog/current-loop alarm evaluation

#### MED-04: STM32duino Non-Atomic Write Audit

**Review:**

- This is correctly framed as an audit, not a feature.
- The shared atomic helpers are already in place, so the audit needs to separate full-file replacement from append-log behavior instead of blindly converting all raw file calls.

**Suggested plan:**

1. Build a checklist with three categories.
    - Full-file replacement: must use `tankalarm_posix_write_file_atomic()` or `tankalarm_littlefs_write_file_atomic()`
    - Append-only logs: acceptable to leave as append if truncated-tail writes are acceptable and old content cannot be corrupted
    - Queue compaction or rewrite: must use temp plus rename
2. Start with the known hotspots from current source.
    - Server calibration-log append paths
    - Client pending-note buffer append and compaction paths
    - Any manual `.tmp` rename sequences that duplicate shared helper logic
3. Produce a short audit report first.
    - Only change code where the audit finds a genuine gap.
    - Do not churn stable append-only code without a concrete failure mode.

**Code changes to plan for:**

- Likely none at first
- If gaps are found, wrap them with the existing helper functions in `TankAlarm_Platform.h`

#### LOW-03/04/06: Documentation Items

**Review:**

- These are worth doing, but I would pair them with the feature changes they describe so the comments match reality.
- The current `generateSessionToken()` comments should be softened: 64-bit internal state is not the same thing as 64 bits of real entropy.

**Suggested plan:**

1. `LOW-03`
    - If `MED-06` lands, remove the "reserved for future use" ambiguity and document the actual duration behavior.
    - If `MED-06` is deferred, replace the operator-facing serial message with a quieter developer comment or TODO.
2. `LOW-04`
    - Add the security-model comment block above `generateSessionToken()`.
    - Change the wording to: acceptable for LAN-local kiosk use, not cryptographically strong, replace for internet exposure.
    - Optional future code change: on STM32H7 builds, prefer hardware RNG when available and keep the current analog/timer mixer only as fallback.
3. `LOW-06`
    - Add the proposed Viewer header comment.
    - Optional hardening: explicitly return HTTP `405 Method Not Allowed` for non-GET requests instead of treating everything as a generic miss, if the current request parser makes that easy.

### Suggested PR Breakdown

1. `PR-1 Relay Runtime Foundation`
    - `MED-06`
    - Accept only if all relay activation paths use shared helpers and timeout bookkeeping is unified.
2. `PR-2 Relay Safety Timeout`
    - `MED-02`
    - Accept only if manual-reset relays can auto-clear offline and the Server surfaces the event.
3. `PR-3 Alarm Debounce Split`
    - `MED-03`
    - Accept only if oscillation/noise tests show clears are no longer starved.
4. `PR-4 Server Note Idempotency`
    - `HIGH-03`
    - Accept only if crash-between-handle-and-delete no longer duplicates high-impact notes.
5. `PR-5 DFU Integrity Verification`
    - `CRIT-02`
    - Accept only if the programmed image hash is compared to a Server-provided expected artifact hash.
6. `PR-6 DFU Safe Install Architecture`
    - `CRIT-01`
    - Accept only after slot sizing, link addresses, and interrupted-install recovery are proven in hardware tests.
7. `PR-7 Audit and Documentation`
    - `MED-04`, `LOW-03`, `LOW-04`, `LOW-06`
    - Accept only if the audit report distinguishes atomic replacement from append semantics and the comments match the final design.

---

## GPT-5.3-Codex Addendum: Final Review and Execution Blueprint

This addendum is a second-pass execution plan focused on reducing operational risk quickly, limiting migration blast radius, and keeping each change independently releasable.

### Final Review Summary

- The selected backlog is correct and should be kept.
- The highest near-term risk remains DFU safety on Client and Viewer.
- The highest day-to-day reliability risk remains relay behavior in manual-reset mode.
- The highest duplication risk remains Server note processing for side-effecting handlers.
- The current sequencing is good; the best improvement is to add explicit release gates and schema compatibility rules before writing code.

### Proceed in 5 Controlled Tracks

1. Track A (foundation): relay runtime unification (`MED-06`) and max-on safety timeout (`MED-02`).
2. Track B (logic correctness): split clear debounce counters (`MED-03`).
3. Track C (delivery semantics): durable idempotency for high-impact Server handlers (`HIGH-03`).
4. Track D (firmware safety): DFU integrity verification first (`CRIT-02`), then safe install architecture (`CRIT-01`).
5. Track E (confidence and docs): atomic-write audit plus security-model documentation (`MED-04`, `LOW-03`, `LOW-04`, `LOW-06`).

### Detailed Implementation Plan by Track

#### Track A - Relay Runtime Foundation + Safety Timeout (MED-06, MED-02)

Goal: make all relay activation paths use one state machine so timeout, reset, and status reporting always agree.

Implementation steps:

1. Add relay-centric runtime state to Client.
2. Refactor alarm path and manual command path to shared helpers.
3. Enforce relay ownership rules in config validation.
4. Add `relayMaxOnSeconds` in schema, parser, serializer, and Config Generator UI.
5. Emit timeout events through existing alarm pipeline (recommended `y: "relay_timeout"`).

Code changes to make:

- File: `TankAlarm-112025-Client-BluesOpta.ino`
- Add new runtime structure and storage:
  - `struct RelayRuntime { bool active; uint8_t ownerMonitor; uint8_t source; unsigned long activatedAt; uint32_t customDurationSec; };`
  - `static RelayRuntime gRelayRuntime[MAX_RELAYS];`
- Update or replace these functions:
  - `sendAlarm()` -> call `activateRelayMaskForMonitor(...)`
  - `processRelayCommand()` -> call `applyManualRelayCommand(...)`
  - `checkRelayMomentaryTimeout()` -> evaluate momentary and max-on safety timeout against `gRelayRuntime[]`
  - `resetRelayForMonitor()` -> call unified deactivate helper
- Add schema field in `MonitorConfig`:
  - `uint32_t relayMaxOnSeconds;`
- Parse and serialize this field in config JSON helpers.
- Add overlap validation for `relayMask` ownership.

- File: `TankAlarm-112025-Server-BluesOpta.ino`
- Update Config Generator UI and payload schema for `relayMaxOnSeconds`.
- Handle and display timeout events in dashboard/alert log pipeline.

Acceptance criteria:

1. Manual relay ON always sets activation timestamp and ownership metadata.
2. Manual-reset relay auto-turns OFF when `relayMaxOnSeconds` is exceeded.
3. Momentary timeout and manual timeout do not conflict for the same relay.
4. Dashboard relay status matches actual GPIO state after alarm and manual operations.

#### Track B - Debounce Split for Clear Path (MED-03)

Goal: prevent clear starvation when readings oscillate near alarm thresholds.

Implementation steps:

1. Replace shared clear counter with separate high-clear and low-clear counters.
2. Update all init/reset codepaths.
3. Isolate high-path and low-path clear logic so each path only touches its own counters.

Code changes to make:

- File: `TankAlarm-112025-Client-BluesOpta.ino`
- In `MonitorRuntime`, replace:
  - `clearDebounceCount`
- With:
  - `highClearDebounceCount`
  - `lowClearDebounceCount`
- Update these areas:
  - `setup()` runtime initialization
  - power-state recovery/reset blocks
  - digital alarm evaluation block
  - analog/current-loop alarm evaluation block

Acceptance criteria:

1. High alarm can clear even when low alarm logic is active elsewhere.
2. Low alarm can clear even when high alarm logic is active elsewhere.
3. Oscillation tests show no permanent latched alarm due solely to shared clear-counter resets.

#### Track C - Durable Idempotency for High-Impact Notes (HIGH-03)

Goal: eliminate duplicate side effects after crash between process and delete.

Implementation steps:

1. Stamp each outbound note with stable `_nid` before send or local buffering.
2. Persist processed IDs on Server with immediate durability for high-impact handlers.
3. Apply dedupe check before handler side effects; still delete duplicates from Notecard queue.
4. Keep low-risk handlers on simpler policy if desired.

Code changes to make:

- File: `TankAlarm-112025-Client-BluesOpta.ino`
- In `publishNote()`:
  - create/stamp `_nid` and `_sv` in outbound payload before serialization
  - ensure buffered notes preserve same `_nid` on retry
- Add boot-level nonce initialization and monotonic sequence counter.

- File: `TankAlarm-112025-Server-BluesOpta.ino`
- In `processNotefile()`:
  - extract `_nid`
  - call `isProcessedNoteId(...)`
  - short-circuit duplicate processing for dedupe-enabled notefiles
  - call `appendProcessedNoteId(...)` immediately after successful side effects and before delete
- Add persistence helpers:
  - `loadProcessedNoteJournal()`
  - `saveProcessedNoteJournal()`
  - `compactProcessedNoteJournal()`

- File: `TankAlarm-112025-Viewer-BluesOpta.ino`
- Keep current poison-note handling.
- Optional: add lightweight `_nid` check only if duplicate summary side effects become operationally relevant.

Acceptance criteria:

1. Crash after side effects but before delete does not duplicate SMS/email/relay-forward actions on reboot.
2. Duplicate notes are drained from queue without re-triggering side effects.
3. Backward compatibility: notes without `_nid` still process normally.

#### Track D - DFU Safety (CRIT-02 then CRIT-01)

Goal: prevent bricking and reject bad firmware artifacts before reboot.

Implementation steps:

1. Implement artifact-integrity verification using expected hash from Server metadata.
2. Refactor DFU routine into small explicit phases with structured failure codes.
3. Run architecture spike to choose safe-install model (dual-slot vs staged install stub).
4. Implement selected install model with boot-attempt tracking and rollback/abort behavior.

Code changes to make:

- File: `TankAlarm-112025-Common/src/TankAlarm_DFU.h`
- Refactor `tankalarm_performIapUpdate()` into phase helpers:
  - `tankalarm_dfu_enter_mode(...)`
  - `tankalarm_dfu_download_and_program(...)`
  - `tankalarm_dfu_verify_image(...)`
  - `tankalarm_dfu_finalize(...)`
- Add hash helpers and error enums:
  - `tankalarm_crc32_update(...)`
  - `tankalarm_read_flash_crc32(...)`
  - `enum TankAlarmDfuErrorCode { ... }`
- Add metadata structs only after architecture spike confirms layout.

- File: `TankAlarm-112025-Server-BluesOpta.ino`
- Persist expected `firmwareSize` and artifact hash with version metadata.
- Include expected hash in DFU-control metadata sent to Client/Viewer.
- Surface verify/install status in DFU API responses.

- Files: `TankAlarm-112025-Client-BluesOpta.ino`, `TankAlarm-112025-Viewer-BluesOpta.ino`
- Add early boot verification/confirmation hook in `setup()` once safe-install model is selected.

Acceptance criteria:

1. Device never reboots into newly programmed firmware unless hash verification passes.
2. Failed verification leaves a recoverable path and explicit error state.
3. Interrupted update behavior is deterministic and testable across power-loss tests.

#### Track E - Audit and Documentation (MED-04, LOW-03, LOW-04, LOW-06)

Goal: close uncertainty gaps and align docs with actual behavior.

Implementation steps:

1. Audit all file writes by intent: full-replace vs append vs compaction.
2. Convert only full-replace/compaction writes to atomic helpers where needed.
3. Update inline comments for relay duration behavior, session token threat model, and Viewer auth scope.

Code changes to make:

- File: `TankAlarm-112025-Common/src/TankAlarm_Platform.h`
- Likely no behavior change; reference helper APIs in audit report and enforce usage consistency.

- File: `TankAlarm-112025-Client-BluesOpta.ino`
- Clarify `duration` behavior in relay command path based on Track A outcome.

- File: `TankAlarm-112025-Server-BluesOpta.ino`
- Add explicit threat-model comments above session-token generation.

- File: `TankAlarm-112025-Viewer-BluesOpta.ino`
- Add top-of-file security model comment block (read-only kiosk, no control endpoints).

Acceptance criteria:

1. Every full-file replace/compaction write path is atomic.
2. Remaining append paths are documented as intentional.
3. Security comments match deployed behavior and threat model.

### Schema and Compatibility Rules

1. `MED-02` changes config payload shape and requires `CONFIG_SCHEMA_VERSION` bump.
2. `HIGH-03` changes note payload shape and requires `NOTEFILE_SCHEMA_VERSION` bump.
3. Maintain backward compatibility in parsers for one release window:
   - missing `relayMaxOnSeconds` -> default `0`
   - missing `_nid` -> process without dedupe guard
4. Add migration notes in release docs before rollout.

### Suggested PR Plan (Ready to Execute)

1. PR-A1: Relay runtime foundation (`MED-06` core refactor).
2. PR-A2: Relay max-on timeout + UI/schema integration (`MED-02`).
3. PR-B1: Debounce split (`MED-03`).
4. PR-C1: Client note ID stamping + Server durable dedupe (`HIGH-03`).
5. PR-D1: DFU hash verification and phase refactor (`CRIT-02`).
6. PR-D2: Safe-install architecture implementation (`CRIT-01`) after architecture spike sign-off.
7. PR-E1: Atomic-write audit fixes and documentation updates (`MED-04`, `LOW-*`).

### Final Recommendation

Start immediately with PR-A1 and PR-A2. These two deliver the fastest real-world safety improvement with the lowest architectural uncertainty. In parallel, start the DFU architecture spike so PR-D1 can begin as soon as relay/runtime work is stable.

---

## Gemini 3.1 Pro (Preview) Review and Execution Plan

### Assessment and Nuances

The identified execution tracks from previous reviews are logically sequenced, but I recommend a few strategic adjustments to drastically reduce implementation friction and leverage the STM32H7 hardware capabilities natively.

1. **Hardware Dual-Bank Swapping for CRIT-01:** The previous reviews questioned the complexity of position-independent code or re-linking for A/B slots. However, the STM32H747 MCU used on the Blues Opta natively supports **hardware flash bank swapping** via the `FLASH_OPTSR_SWAP_BANK` option byte.
    *   **Implication:** You do *not* need separate link targets or a complex RAM trampoline. The firmware can always be compiled to execute from `0x08000000`. By downloading the new firmware to Bank 2 (`0x08100000`), verifying the hash, and toggling the `SWAP_BANK` bit, the MCU hardware automatically remaps Bank 2 to address `0x08000000` on the next reset.
    *   **Recommendation:** Skip the software architecture spike and instead implement a hardware-based bank-swap proof-of-concept. This guarantees identical execution context for both slots with zero linker script modifications.

2. **Combined Relay Struct Updates (MED-06, MED-02, MED-03):** The proposed "Track A" and "Track B" both heavily touch `MonitorRuntime`, the relay arrays (`gRelayState`, `gRelayActiveMaskForMonitor`), and the alarm loop in `TankAlarm-112025-Client-BluesOpta.ino`.
    *   **Implication:** Breaking these into 3 separate PRs will cause artificial merge conflicts and redundant test matrix execution.
    *   **Recommendation:** Execute `MED-06`, `MED-02`, and `MED-03` simultaneously in a single "Relay & Alarm State Machine Overhaul" PR. Combining the new clear debounce counters with the new `RelayRuntime` structure reduces overall code churn since the logic converges in the alarm evaluation block.

3. **Transaction Logging for Idempotency (HIGH-03):** While batching writes creates gaps (as noted in the Codex review), synchronous writes to LittleFS for every received note will cause noticeable filesystem wear and delay the handler thread.
    *   **Implication:** We need strict durability without blocking the main loop or excessive sector churn.
    *   **Recommendation:** Implement a single pre-allocated Write-Ahead Log (WAL) file for `_nid` tracking. Pre-allocate a 4KB file of null bytes on boot. Before a note is processed, do an `fseek` and `fwrite` of the 16-byte `_nid` directly into a slot without renaming or compacting. When the 4KB wraps around, rotate it into the persistent ring buffer in a background task. This provides millisecond-level durability without LittleFS metadata churn.

### Accelerated Execution Path

I propose condensing the execution tracks into the following high-velocity phases:

#### Phase 1: Alarm & Relay Kernel (Combines MED-06, MED-02, MED-03)
*   **Target:** `TankAlarm-112025-Client-BluesOpta.ino`
*   **Code Changes:**
    1. Replace `clearDebounceCount` with `highClearDebounceCount` and `lowClearDebounceCount` inside `MonitorRuntime` (around line 570). Set both to 0 during `setup()` and power transitions.
    2. Replace standalone relay arrays with a `RelayRuntime` struct array as suggested above.
    3. Overhaul `processRelayCommand()` and `sendAlarm()` to use unified `activateRelayMaskForMonitor()` and `deactivateRelayMaskForMonitor()` functions.
    4. Fold in `relayMaxOnSeconds` checking into `checkRelayMomentaryTimeout()` since the evaluation loop is the same. Add the config variable to the schemas.
*   **Why here:** Unifies test scope. You only have to regression test the Client alarm evaluation board once instead of three times.

#### Phase 2: Staged OTA with Hardware Bank Swap (CRIT-02 & CRIT-01)
*   **Target:** `TankAlarm-112025-Common/src/TankAlarm_DFU.h`
*   **Code Changes:**
    1. Introduce `tankalarm_crc32_update()` accumulation directly into the `dfu.get` download loop (lines ~371-478).
    2. Change the flash destination from Bank 1 to Bank 2 (the inactive bank). Instead of wiping Bank 1, clear Bank 2.
    3. Download the firmware chunk-by-chunk into Bank 2.
    4. Perform a final CRC32 check across Bank 2.
    5. **If successful:** Unlock flash option bytes, toggle the `SWAP_BANK` bit in the STM32H7 `FLASH_OPTSR` register, lock option bytes, and call `NVIC_SystemReset()`.
    6. **If failed:** Abort, leave Bank 1 intact, and resume normal operation.
*   **Why here:** Bricking devices in the field is the only fatal risk to the system. The Bank Swap technique turns this from a dangerous multi-week software project into a relatively straightforward register manipulation fix that is native to the Arm architecture.

#### Phase 3: Durable Dedupe with Write-Ahead Logging (HIGH-03)
*   **Target:** `TankAlarm-112025-Server-BluesOpta.ino` and `TankAlarm-112025-Client-BluesOpta.ino`
*   **Code Changes:**
    1. Add `<deviceUid>:<bootNonce>:<seq>` logic at the top of `publishNote()` on the Client.
    2. On the Server, create a pre-allocated `/fs/nid_wal.bin` file.
    3. In `processNotefile()`, check the `_nid` against RAM history. If new, append it natively into `nid_wal.bin` using a byte offset (no file truncation) *before* triggering SMS/email logic.
    4. Upon completion, delete the note.
*   **Why here:** Assures complete transaction safety, reducing duplicate SMS messages to zero while saving the Mbed filesystem from premature exhaustion.

#### Phase 4: Atomic Audit & Security Documentation (MED-04, LOW-03/04/06)
*   Deploy the audit verification and documentation suggestions concurrently with Phase 3 or immediately after, as these are primarily non-blocking commentary and API compliance checks.

---

## Final Review — Synthesized Assessment and Implementation Plan

**Date:** April 8, 2026  
**Reviewer:** GitHub Copilot (Claude Opus 4.6)  
**Scope:** Evaluation of all three AI review addendums (Copilot, GPT-5.3-Codex, Gemini 3.1 Pro) against verified codebase state

This section synthesizes the three review addendums into a single actionable plan, resolves their disagreements, identifies which suggestions are worthwhile, and provides concrete implementation guidance for each item.

---

### Cross-Review Agreement and Disagreements

All three reviews agree on:
- **Execution order:** MED-06 → MED-02 → MED-03 → HIGH-03 → CRIT-02 → CRIT-01
- **`RelayRuntime` struct:** Replace the scattered global arrays with a per-relay runtime structure
- **Relay ownership validation:** Reject overlapping `relayMask` across monitors in config validation
- **Idempotency key format:** `<deviceUid>:<bootNonce>:<seq>` is more robust than `<epoch>:<counter>`
- **CRC/hash verification before DFU reboot:** Non-negotiable
- **Tiered durability for idempotency:** Immediate persist for high-impact handlers, simpler policy for low-impact
- **MED-04 is an audit, not a feature**

Key disagreements:

| Topic | Copilot / Codex | Gemini | Verdict |
|-------|-----------------|--------|---------|
| **DFU implementation** | Architecture spike first, solve image-address problem | Hardware `SWAP_BANK` bypasses the problem entirely | **Gemini's approach is superior — see analysis below** |
| **PR grouping for MED-03** | Separate PR from relay work | Combine MED-06 + MED-02 + MED-03 in one PR | **Split: MED-06+MED-02 together, MED-03 separate** |
| **Idempotency persistence** | Ring buffer in JSON on LittleFS | Pre-allocated 4KB WAL file with fseek+fwrite | **WAL approach wins on flash wear** |
| **Timeout notification** | New `relay_timeout.qo` notefile | Reuse alarm pipeline with `y: "relay_timeout"` | **Reuse alarm pipeline — less code, same outcome** |

---

### DFU: Hardware Bank Swap Assessment

Gemini's suggestion to use STM32H747 hardware bank swapping (`FLASH_OPTSR_SWAP_BANK`) is the most significant insight across all three reviews. Verified against the codebase:

**Current state** (from `TankAlarm_DFU.h` L333):
```
appStart = flashStart + 0x40000  // 0x08040000
```

The Arduino Opta application is linked to execute from 0x08040000. The STM32H747XI has two 1MB flash banks:
- Bank 1: 0x08000000–0x080FFFFF (bootloader + current app)
- Bank 2: 0x08100000–0x081FFFFF (currently used by LittleFS warm tier)

The hardware `SWAP_BANK` bit in `FLASH_OPTSR` remaps Bank 2 to Bank 1's address space at the bus level. The CPU sees 0x08000000 pointing to physical Bank 2 without any software relocation. This means:
- **No dual link targets needed** — same binary runs from same virtual address
- **No VTOR trampoline needed** — the hardware mapping is transparent
- **No linker script changes needed** — the build toolchain is unchanged
- **Rollback is a single register write** — toggle the swap bit and reset

**However, three risks must be validated before committing:**

1. **Arduino Opta bootloader compatibility:** The STM32 bootloader at 0x08000000–0x0803FFFF is in Bank 1. After a swap, the bootloader physical location moves. If the bootloader is stored in Bank 1 only, swapping would map the wrong bank as bootloader. **Mitigation:** The bootloader must exist identically in both banks, OR the bootloader must reside in a non-swappable region (OTP/system flash). This requires a hardware test.

2. **LittleFS partition conflict:** The warm-tier daily summaries currently live in Bank 2 (at high addresses in the 2MB space). A bank swap would put the active application on the same physical bank as LittleFS data. **Mitigation:** LittleFS partitioning must be rearchitected — either move warm tier to external storage, or allocate warm tier within the active bank's unused space.

3. **Mbed OS FlashIAP assumptions:** The `mbed::FlashIAP` driver may have hardcoded expectations about bank layout. After a swap, `flash.get_flash_start()` and `flash.get_sector_size()` may return incorrect values. **Mitigation:** Test `FlashIAP` behavior after a swap on real hardware.

**Recommendation:** Implement CRIT-02 (CRC verification) first — it's risk-free and immediately useful. Then run a focused hardware bank swap test (1–2 days) before implementing CRIT-01. If bank swap works, it eliminates weeks of software complexity. If it doesn't work (bootloader or LittleFS conflicts), fall back to the Copilot/Codex approach of a verified staging region with a copy stub.

---

### Suggestion Assessment — What Is Worth Implementing

| # | Suggestion | Source | Worth It? | Rationale |
|---|-----------|--------|-----------|-----------|
| 1 | `RelayRuntime` struct replacing scattered globals | All three | **Yes** | Cleaner state management, eliminates bookkeeping divergence between alarm and manual paths |
| 2 | Reject overlapping `relayMask` in config validation | Copilot/Codex | **Yes** | Prevents ambiguous ownership; cheap to implement in config parser and Config Generator UI |
| 3 | `relayMaxOnSeconds` reusing alarm pipeline (`y: "relay_timeout"`) | Codex/Gemini | **Yes** | Eliminates a new notefile, handler, and route; timeout events flow through existing dispatch |
| 4 | Split `clearDebounceCount` into two counters | All three | **Yes** | Straightforward fix, prevents clear starvation in noisy environments |
| 5 | `<deviceUid>:<bootNonce>:<seq>` for note IDs | All three | **Yes** | Immune to epoch-unavailable-at-boot and reboot-window collisions |
| 6 | WAL file for processed note IDs | Gemini | **Yes** | Lower flash wear than JSON ring buffer; pre-allocated file avoids LittleFS metadata churn |
| 7 | Tiered durability (immediate vs RAM-only) | Codex/Gemini | **Yes** | Pragmatic — high-impact handlers get crash safety, low-impact handlers get speed |
| 8 | Server-provided artifact hash for DFU verification | Copilot | **Yes** | Catches supply-chain or transmission errors that download-vs-readback CRC cannot |
| 9 | Refactor `tankalarm_performIapUpdate()` into phase helpers | Codex | **Yes** | Current 250-line monolith is hard to test and extend |
| 10 | Hardware bank swap for A/B DFU | Gemini | **Conditionally** | Only if hardware validation passes; transformative simplification if it works |
| 11 | Combine MED-06/MED-02/MED-03 into one PR | Gemini | **No** | MED-03 is logically independent (alarm evaluation, not relay bookkeeping); separate PR is cleaner for review and rollback |
| 12 | `highAlarmLatched`+`lowAlarmLatched` → single enum | Copilot | **No (defer)** | Nice cleanup but adds scope to an already-complex alarm change; follow-up PR at most |
| 13 | Viewer HTTP 405 for non-GET requests | Copilot | **No** | Marginal hardening for a LAN-only read-only kiosk; not worth the code churn |
| 14 | Hardware RNG for session tokens | Copilot/Gemini | **No (defer)** | Current PRNG is acceptable for LAN-only kiosk; document the threat model (LOW-04) instead |

---

### Implementation Plan

#### PR-1: Relay Runtime Foundation + Safety Timeout (MED-06 + MED-02)

**Goal:** Unify all relay activation paths through a single state machine with ownership tracking, and add a configurable max-on safety timeout for `RELAY_MODE_MANUAL_RESET`.

**File: `TankAlarm-112025-Client-BluesOpta.ino`**

Step 1 — Replace scattered relay globals with `RelayRuntime`:

```cpp
// Replace (at L709-718):
//   static bool gRelayState[MAX_RELAYS];
//   static unsigned long gRelayActivationTime[MAX_RELAYS];
//   static bool gRelayActiveForMonitor[MAX_MONITORS];
//   static uint8_t gRelayActiveMaskForMonitor[MAX_MONITORS];
// With:
enum RelaySource : uint8_t { RELAY_SRC_NONE = 0, RELAY_SRC_ALARM, RELAY_SRC_MANUAL, RELAY_SRC_CLEAR_BUTTON };
struct RelayRuntime {
  bool active;
  uint8_t ownerMonitor;        // MAX_MONITORS = no owner (standalone manual)
  RelaySource source;
  unsigned long activatedAt;   // millis() timestamp
  uint32_t customDurationSec;  // 0 = use monitor config
};
static RelayRuntime gRelayRuntime[MAX_RELAYS];
// Keep gRelayState[] for the physical GPIO state — it's used in setRelayState()
```

Step 2 — Add shared activation/deactivation helpers:

```cpp
static void activateRelayForMonitor(uint8_t monitorIdx, uint8_t relayMask,
                                     RelaySource source, unsigned long now,
                                     uint32_t customDurationSec = 0) {
  for (uint8_t r = 0; r < MAX_RELAYS; r++) {
    if (relayMask & (1 << r)) {
      setRelayState(r, true);
      gRelayRuntime[r].active = true;
      gRelayRuntime[r].ownerMonitor = monitorIdx;
      gRelayRuntime[r].source = source;
      gRelayRuntime[r].activatedAt = now;
      gRelayRuntime[r].customDurationSec = customDurationSec;
    }
  }
}

static void deactivateRelayForMonitor(uint8_t monitorIdx, uint8_t relayMask) {
  for (uint8_t r = 0; r < MAX_RELAYS; r++) {
    if (relayMask & (1 << r)) {
      setRelayState(r, false);
      gRelayRuntime[r] = {};  // Zero-init all fields
    }
  }
}
```

Step 3 — Refactor `sendAlarm()` (L4808–4835) to call `activateRelayForMonitor()`:

```cpp
// Replace inline bookkeeping with:
activateRelayForMonitor(idx, cfg.relayMask, RELAY_SRC_ALARM, millis());
```

Step 4 — Refactor `processRelayCommand()` (L7035–7121):

```cpp
// Replace bare setRelayState() with:
if (state) {
  uint8_t mask = 1 << relayNum;
  uint8_t monIdx = findMonitorForRelay(relayNum); // scan gConfig.monitors[]
  uint32_t dur = doc["duration"] | (uint32_t)0;
  activateRelayForMonitor(monIdx, mask, RELAY_SRC_MANUAL, millis(), dur);
} else {
  uint8_t mask = 1 << relayNum;
  uint8_t monIdx = findMonitorForRelay(relayNum);
  deactivateRelayForMonitor(monIdx, mask);
}
```

Step 5 — Add `findMonitorForRelay()`:

```cpp
static uint8_t findMonitorForRelay(uint8_t relayNum) {
  for (uint8_t i = 0; i < gConfig.monitorCount; i++) {
    if (gConfig.monitors[i].relayMask & (1 << relayNum)) return i;
  }
  return MAX_MONITORS;  // Sentinel: no owner
}
```

Step 6 — Extend `checkRelayMomentaryTimeout()` (L7179–7229) to also check `RELAY_MODE_MANUAL_RESET` with `relayMaxOnSeconds`:

```cpp
// After existing RELAY_MODE_MOMENTARY logic, add:
// For RELAY_MODE_MANUAL_RESET with relayMaxOnSeconds > 0:
for (uint8_t r = 0; r < MAX_RELAYS; r++) {
  if (!gRelayRuntime[r].active) continue;
  uint8_t monIdx = gRelayRuntime[r].ownerMonitor;
  if (monIdx >= gConfig.monitorCount) continue;
  const MonitorConfig &cfg = gConfig.monitors[monIdx];
  if (cfg.relayMode != RELAY_MODE_MANUAL_RESET || cfg.relayMaxOnSeconds == 0) continue;

  uint32_t elapsed = (now - gRelayRuntime[r].activatedAt) / 1000UL;
  uint32_t limit = gRelayRuntime[r].customDurationSec > 0
                   ? gRelayRuntime[r].customDurationSec
                   : cfg.relayMaxOnSeconds;
  if (elapsed >= limit) {
    Serial.print(F("Safety timeout: relay "));
    Serial.print(r + 1);
    Serial.print(F(" ON for "));
    Serial.print(elapsed);
    Serial.println(F("s — forcing OFF"));
    deactivateRelayForMonitor(monIdx, 1 << r);
    // Send timeout notification through alarm pipeline
    sendAlarm(monIdx, "relay_timeout", gMonitorRuntime[monIdx].currentInches);
  }
}
```

Step 7 — Add `relayMaxOnSeconds` to `MonitorConfig` and config parse/serialize:

```cpp
// In MonitorConfig struct (after relayMomentarySeconds[4]):
uint32_t relayMaxOnSeconds;  // 0 = no limit (default)

// In parseMonitorFromJson():
mon.relayMaxOnSeconds = t["relayMaxOnSeconds"] | (uint32_t)0;

// In serializeConfig():
t["relayMaxOnSeconds"] = cfg.monitors[i].relayMaxOnSeconds;
```

Step 8 — Add relay overlap validation in config parser:

```cpp
// After parsing all monitors, check for overlapping relayMask:
for (uint8_t i = 0; i < cfg.monitorCount; i++) {
  for (uint8_t j = i + 1; j < cfg.monitorCount; j++) {
    if (cfg.monitors[i].relayMask & cfg.monitors[j].relayMask) {
      Serial.print(F("WARNING: Monitors "));
      Serial.print(i); Serial.print(F(" and ")); Serial.print(j);
      Serial.println(F(" share relay bits — timeout behavior may be ambiguous"));
    }
  }
}
```

Step 9 — Update `resetRelayForMonitor()` (L7232–7260) to use `deactivateRelayForMonitor()`.

**File: `TankAlarm-112025-Server-BluesOpta.ino`**

Step 10 — Add `relayMaxOnSeconds` to Config Generator page HTML (numeric input in relay section).

Step 11 — Handle `"relay_timeout"` alarm type in `handleAlarm()` — log to transmission log and trigger email if configured.

Step 12 — Add dashboard banner for recent relay timeout events.

**Acceptance criteria:**
1. Manual relay ON always populates `gRelayRuntime[r]` with timestamp, owner, and source.
2. MANUAL_RESET relay auto-turns OFF when `relayMaxOnSeconds` exceeded.
3. Momentary timeout and max-on timeout coexist without conflict.
4. Dashboard relay status matches GPIO state for both alarm and manual operations.
5. Timeout notification arrives at Server through the alarm pipeline.
6. Overlapping `relayMask` produces a serial warning during config parse.

---

#### PR-2: Debounce Counter Split (MED-03)

**Goal:** Prevent clear starvation when readings oscillate near alarm thresholds by giving high and low alarm clear paths independent counters.

**File: `TankAlarm-112025-Client-BluesOpta.ino`**

Step 1 — Replace `clearDebounceCount` in `MonitorRuntime` (L570–575):

```cpp
// Remove:
//   uint8_t clearDebounceCount;
// Add:
uint8_t highClearDebounceCount;
uint8_t lowClearDebounceCount;
```

Step 2 — Update initialization in `setup()` (L1325): set both new counters to 0.

Step 3 — Update power-state recovery blocks (L3628, L3870): reset both counters to 0.

Step 4 — Update digital sensor alarm path (L4467–4490):
- When high alarm is latched and clear condition is met: increment `highClearDebounceCount`, leave `lowClearDebounceCount` alone.
- When low alarm is latched and clear condition is met: increment `lowClearDebounceCount`, leave `highClearDebounceCount` alone.
- When entering high alarm zone: reset `highClearDebounceCount` to 0 only.
- When entering low alarm zone: reset `lowClearDebounceCount` to 0 only.

Step 5 — Update analog/current loop alarm path (L4505–4555) with the same split logic:
- High alarm condition resets `highClearDebounceCount` only (not `lowClearDebounceCount`).
- Low alarm condition resets `lowClearDebounceCount` only (not `highClearDebounceCount`).
- Remove the final guard `if (highCondition || lowCondition) state.clearDebounceCount = 0` and replace with per-condition resets.

Step 6 — Grep the file for any other references to `clearDebounceCount` and update them.

**Acceptance criteria:**
1. High alarm can clear even when low alarm condition intermittently fires.
2. Low alarm can clear even when high alarm condition intermittently fires.
3. Oscillation between alarm zone and clear zone does not permanently latch the alarm.
4. Existing non-oscillating alarm behavior is unchanged (regression test).

---

#### PR-3: Server Note Idempotency (HIGH-03)

**Goal:** Eliminate duplicate side effects (SMS, email, relay forwarding) when a crash between processing and deletion causes note re-processing.

**File: `TankAlarm-112025-Client-BluesOpta.ino`**

Step 1 — Add boot nonce and sequence counter initialization in `setup()`:

```cpp
static char gDeviceUid[32] = "";  // Populated from card.status on boot
static uint32_t gBootNonce = 0;   // Random-ish value from micros() at boot
static uint32_t gNoteSeq = 0;     // Monotonic counter, resets on reboot
// In setup():
gBootNonce = micros();
```

Step 2 — Stamp each outbound note in `publishNote()` before serialization:

```cpp
// After doc is populated by the caller, before measureJson():
char nid[64];
snprintf(nid, sizeof(nid), "%s:%lu:%lu", gDeviceUid, gBootNonce, gNoteSeq++);
doc["_nid"] = nid;
```

Step 3 — Ensure `bufferNoteForRetry()` preserves the stamped JSON (the `_nid` is already in the serialized payload, so retries reuse the same ID).

**File: `TankAlarm-112025-Server-BluesOpta.ino`**

Step 4 — Add WAL-based processed note journal:

```cpp
// Pre-allocated Write-Ahead Log for processed note IDs
static const char *NID_WAL_PATH = "/fs/nid_wal.bin";
static const uint16_t NID_WAL_SLOTS = 128;
static const uint8_t NID_SLOT_SIZE = 32;  // Max _nid length
static const uint16_t NID_WAL_SIZE = NID_WAL_SLOTS * NID_SLOT_SIZE;  // 4KB
static uint16_t gNidWalWriteIndex = 0;
// RAM mirror for fast lookup
static char gNidCache[NID_WAL_SLOTS][NID_SLOT_SIZE];
static uint16_t gNidCacheCount = 0;
```

Step 5 — Implement WAL helpers:

```cpp
static void loadNidWal() {
  // Read NID_WAL_PATH into gNidCache; count valid (non-null) entries
  // Set gNidWalWriteIndex to first empty slot (or 0 if full/wrap)
}

static bool isProcessedNoteId(const char *nid) {
  // Linear scan of gNidCache (128 entries × 32 bytes is fast in RAM)
  for (uint16_t i = 0; i < gNidCacheCount; i++) {
    if (strncmp(gNidCache[i], nid, NID_SLOT_SIZE) == 0) return true;
  }
  return false;
}

static void appendProcessedNoteId(const char *nid) {
  // Write to RAM cache
  strncpy(gNidCache[gNidWalWriteIndex], nid, NID_SLOT_SIZE - 1);
  gNidCache[gNidWalWriteIndex][NID_SLOT_SIZE - 1] = '\0';
  if (gNidCacheCount < NID_WAL_SLOTS) gNidCacheCount++;
  // Write to WAL file at offset (no truncation, no rename)
  FILE *f = fopen(NID_WAL_PATH, "r+b");
  if (f) {
    fseek(f, (long)gNidWalWriteIndex * NID_SLOT_SIZE, SEEK_SET);
    fwrite(gNidCache[gNidWalWriteIndex], 1, NID_SLOT_SIZE, f);
    fclose(f);
  }
  gNidWalWriteIndex = (gNidWalWriteIndex + 1) % NID_WAL_SLOTS;
}
```

Step 6 — Add dedupe check in `processNotefile()` (L8037–8097), tiered by notefile risk:

```cpp
// High-risk notefiles: alarm.qi, daily.qi, unload.qi, relay_forward.qi
// → Check _nid before processing, persist _nid immediately after handler, then delete
const char *nid = doc["_nid"] | "";
bool highRisk = (strcmp(fileName, "alarm.qi") == 0 ||
                 strcmp(fileName, "daily.qi") == 0 ||
                 strcmp(fileName, "unload.qi") == 0 ||
                 strcmp(fileName, "relay_forward.qi") == 0);
if (nid[0] != '\0' && highRisk && isProcessedNoteId(nid)) {
  // Duplicate — drain from queue without re-processing
  // Delete the note and continue to next
  continue;
}
handler(doc, epoch);
if (nid[0] != '\0' && highRisk) {
  appendProcessedNoteId(nid);
}
// Delete note from Notecard queue
```

Step 7 — For low-risk notefiles (`telemetry.qi`, `serial_log.qi`, `serial_ack.qi`, `location_resp.qi`, `config_ack.qi`): no dedupe needed. These are either idempotent (telemetry upserts, state updates) or low-impact (log appends). Keep the current peek-then-delete pattern unchanged to avoid unnecessary complexity.

Step 8 — Backward compatibility: notes without `_nid` (from older Client firmware) skip the dedupe check entirely. The `nid[0] != '\0'` guard handles this.

Step 9 — Pre-allocate the WAL file on first boot if it doesn't exist:

```cpp
// In setup(), after filesystem mount:
if (!fileExists(NID_WAL_PATH)) {
  FILE *f = fopen(NID_WAL_PATH, "wb");
  if (f) {
    char zeros[NID_SLOT_SIZE] = {0};
    for (uint16_t i = 0; i < NID_WAL_SLOTS; i++) fwrite(zeros, 1, NID_SLOT_SIZE, f);
    fclose(f);
  }
}
loadNidWal();
```

**Acceptance criteria:**
1. Crash after alarm SMS dispatch but before note deletion does NOT re-send SMS on reboot.
2. Duplicate notes are drained from the Notecard queue without re-triggering side effects.
3. Notes without `_nid` (old firmware) process normally without dedupe.
4. WAL file rotation works correctly when all 128 slots are filled.
5. Server boot loads existing WAL and resumes correctly.

---

#### PR-4: DFU Integrity Verification (CRIT-02)

**Goal:** Prevent reboot into corrupt firmware by verifying the programmed flash matches the expected artifact.

**File: `TankAlarm-112025-Common/src/TankAlarm_DFU.h`**

Step 1 — Add CRC32 helper:

```cpp
static uint32_t tankalarm_crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
  }
  return ~crc;
}
```

Step 2 — Accumulate CRC during the download loop (inside the `while (offset < firmwareLength)` loop at L371–478):

```cpp
uint32_t downloadCrc = 0;
// ... in the loop, after successful base64 decode:
downloadCrc = tankalarm_crc32_update(downloadCrc, progBuf, decoded);
```

Step 3 — After the download loop completes (after L478), read back the entire programmed region and verify:

```cpp
// Verify flash contents match download CRC
uint32_t verifyCrc = 0;
uint32_t verifyOffset = 0;
uint8_t *verifyBuf = (uint8_t *)malloc(4096);
if (verifyBuf) {
  while (verifyOffset < firmwareLength) {
    if (kickWatchdog) kickWatchdog();
    uint32_t readLen = min((uint32_t)4096, firmwareLength - verifyOffset);
    memcpy(verifyBuf, (const void *)(appStart + verifyOffset), readLen);
    verifyCrc = tankalarm_crc32_update(verifyCrc, verifyBuf, readLen);
    verifyOffset += readLen;
  }
  free(verifyBuf);
}
if (verifyCrc != downloadCrc) {
  Serial.println(F("IAP DFU: VERIFICATION FAILED — flash contents do not match download"));
  Serial.print(F("  Download CRC: 0x")); Serial.println(downloadCrc, HEX);
  Serial.print(F("  Flash CRC:    0x")); Serial.println(verifyCrc, HEX);
  flash.deinit();
  goto iap_restore_hub;  // Do NOT reboot
}
Serial.println(F("IAP DFU: Verification passed"));
```

Step 4 — Refactor the monolithic function into named phases for testability (non-breaking restructure):

```cpp
// Phase 1: Enter DFU mode (existing L256-305)
// Phase 2: Erase flash (existing L307-368)
// Phase 3: Download and program (existing L370-478, with CRC accumulation)
// Phase 4: Verify (new — read-back comparison)
// Phase 5: Finalize and reboot (existing L480-509)
```

Each phase can be extracted into a helper function, but the current `goto iap_restore_hub` pattern makes this a light refactor — keep the goto for failure paths (it's idiomatic for C cleanup), but give each phase a clear comment block and serial progress message.

**Acceptance criteria:**
1. Flash corruption (even a single bit-flip) is detected before reboot.
2. On verification failure, the device does NOT reboot and reports the error.
3. Download CRC is computed incrementally without requiring the full firmware in RAM.
4. The verification pass adds minimal time (flash read at bus speed, ~100ms for 700KB).

---

#### PR-5: DFU Safe Install Architecture (CRIT-01)

**Depends on:** PR-4 (CRC verification) completed and a hardware bank swap validation test.

**Two possible implementation paths depending on the hardware test outcome:**

**Path A: Hardware Bank Swap (preferred if validation passes)**

Step 1 — Validate on real Arduino Opta hardware:
- Toggle `FLASH_OPTSR_SWAP_BANK` via `HAL_FLASH_OB_Unlock()` / `HAL_FLASHEx_OBProgram()`
- Verify the bootloader survives the swap (critical — if it doesn't, this path is blocked)
- Verify `FlashIAP` reports correct addresses after swap
- Verify LittleFS partition is either in non-swapped space or relocated

Step 2 — If validation passes, modify `tankalarm_performIapUpdate()`:
- Determine which bank is currently active (read `FLASH_OPTSR` register)
- Erase the inactive bank
- Download firmware to the inactive bank
- Verify CRC (from PR-4)
- Toggle `SWAP_BANK` bit via option bytes
- Reboot — MCU boots from the new bank at the same virtual address

Step 3 — Add boot-attempt counter in a non-swappable region (OTP or dedicated flash page):
- Increment on each boot, reset after confirmed-good (e.g., 30 seconds of stable operation)
- If boot count exceeds 3, toggle `SWAP_BANK` back and reboot (automatic rollback)

Step 4 — Add firmware confirmation API on Server (`POST /api/dfu/confirm` or automatic after watchdog survival period).

**Path B: Verified Staging + Copy Stub (fallback if bank swap fails validation)**

Step 1 — Designate Bank 2 upper region as a staging area
Step 2 — Download and verify firmware in the staging area (safe — active app is untouched)
Step 3 — After verification passes, erase the active area and copy from staging
Step 4 — The copy step is still vulnerable to power loss, but the staging area preserves the verified image for retry

This path is safer than the current erase-then-download sequence (the window of vulnerability is reduced to the copy step only), but not as safe as bank swap.

**Acceptance criteria (either path):**
1. Power loss during download does NOT brick the device.
2. Corrupt firmware is never booted.
3. Failed update leaves the device running the previous firmware.
4. Automatic rollback triggers if the new firmware fails to boot 3 times.

---

#### PR-6: Atomic Write Audit + Documentation (MED-04, LOW-03, LOW-04, LOW-06)

**Goal:** Close remaining uncertainty and align documentation with actual behavior.

**MED-04: Audit methodology**

Step 1 — Enumerate all file write operations across all three sketches. Categorize each as:

| Category | Required Pattern | Rationale |
|----------|-----------------|-----------|
| Full-file replacement | `tankalarm_*_write_file_atomic()` | Power loss during write must not corrupt existing data |
| Append-only log | Direct `fwrite()` in append mode is acceptable | Truncated tail is tolerable; existing data is preserved |
| Queue compaction/rewrite | Atomic temp+rename | Rewriting a queue file mid-operation must not lose entries |

Step 2 — Known hotspots to verify:
- Server `saveDailySummary()` — must be atomic (full-file replacement of summary JSON)
- Client `bufferNoteForRetry()` — append-only is acceptable (pending note log)
- Client `compactPendingNotes()` — if it rewrites the file, must use atomic pattern
- Server calibration log append — append-only is acceptable
- Config saves in all three sketches — must be atomic (already verified in prior review)

Step 3 — Produce a short audit report in `CODE REVIEW/` documenting the findings. Only change code where the audit finds a genuine gap.

**LOW-03: Relay duration documentation**

- If PR-1 is implemented: the `duration` parameter becomes functional. Remove the dead code and "ignored" log message. Add a code comment explaining the behavior.
- If PR-1 is not yet implemented: replace the operator-facing "ignored" serial message with `// TODO: Implement in MED-06 relay bookkeeping unification` and remove the runtime log.

**LOW-04: Session token PRNG documentation**

- Add the security-model comment block above `generateSessionToken()` as proposed in section 7 of this document.
- Adjust wording: "~20–30 bits of practical entropy" (not 64 bits), "acceptable for LAN-local kiosk use", "replace with STM32H7 hardware TRNG for internet exposure."

**LOW-06: Viewer authentication documentation**

- Add the security-model header comment to `TankAlarm-112025-Viewer-BluesOpta.ino` explaining the read-only kiosk design, the absence of control endpoints, and the LAN-only deployment assumption.

**Acceptance criteria:**
1. Every full-file replacement/compaction write path uses atomic helpers.
2. Append-only paths are documented as intentional.
3. Security comments match deployed behavior and threat model.
4. No operator-facing serial messages reference unimplemented features.

---

### Execution Order and Dependencies

```
Phase 1 (parallel):
  ├── PR-1: Relay Runtime + Safety Timeout (MED-06 + MED-02)
  └── PR-2: Debounce Split (MED-03)
        (independent — different code areas in same file)

Phase 2 (after Phase 1):
  └── PR-3: Note Idempotency (HIGH-03)
        (benefits from stable relay behavior in alarm pipeline)

Phase 3 (parallel with Phase 2):
  ├── PR-4: DFU CRC Verification (CRIT-02)
  └── Hardware bank swap validation test

Phase 4 (after PR-4 + bank swap test):
  └── PR-5: DFU Safe Install (CRIT-01)

Phase 5 (any time):
  └── PR-6: Audit + Documentation (MED-04, LOW-*)
```

### Schema Impact Summary

| PR | Schema Change | Version Bump | Backward Compatibility |
|----|--------------|--------------|----------------------|
| PR-1 | `relayMaxOnSeconds` added to `MonitorConfig` | `CONFIG_SCHEMA_VERSION` +1 | Missing field defaults to 0 (no limit) |
| PR-3 | `_nid` field added to outbound note payloads | `NOTEFILE_SCHEMA_VERSION` +1 | Missing `_nid` → process without dedupe |
| PR-2, PR-4, PR-5, PR-6 | None | None | Fully backward compatible |

### Risk Assessment

| PR | Implementation Risk | Operational Risk if Deferred |
|----|-------------------|------------------------------|
| PR-1 | Medium — touches alarm, relay, and config paths | **High** — manual-reset relay can stay ON indefinitely offline |
| PR-2 | Low — isolated logic change with clear test vectors | Medium — noisy sensors may fail to clear alarms |
| PR-3 | Medium — new persistence layer, schema change | **High** — crash can duplicate SMS/email/relay actions |
| PR-4 | Low — additive CRC check, no behavioral change on success | **Critical** — corrupt firmware can brick device |
| PR-5 | High — platform-specific flash manipulation | **Critical** — power loss during DFU bricks device |
| PR-6 | Low — audit and documentation only | Low — existing code is mostly correct |

### Final Recommendation

Start PR-1 and PR-2 in parallel immediately — they deliver the fastest real-world safety improvement. Launch the hardware bank swap validation test concurrently so PR-5 design is unblocked by the time PR-4 is ready. PR-3 can begin once Phase 1 is stable. PR-6 is low-priority background work that should be paired with whichever PR touches the relevant code last.

---

## Implementation Review

**Date:** April 8, 2026  
**Reviewer:** GitHub Copilot (GPT-5.3-Codex)  
**Scope reviewed:** Implemented Future Work changes reflected in v1.6.0 notes (PR-1, PR-2, PR-4)

### Verdict

The major planned changes were implemented and integrated, but **not all changes were made properly**. One **critical regression** was introduced in relay safety timeout handling, along with several correctness and robustness issues. Debounce split and CRC read-back verification are substantially correct.

### Findings (Ordered by Severity)

1. **Critical: safety timeout can re-activate relay immediately (`RELAY_TRIGGER_ANY`)**
   - Evidence:
     - Timeout path sends `relay_timeout`: `TankAlarm-112025-Client-BluesOpta.ino` line 7321.
     - `sendAlarm()` treats any type other than `"clear"` as alarm: line 4804.
     - Relay activation for `RELAY_TRIGGER_ANY` unconditionally sets activate flag: lines 4816-4817.
   - Impact:
     - A relay forced OFF by safety timeout can be turned back ON in the same flow, defeating the safety objective.
   - Required fix:
     - In `sendAlarm()`, treat `relay_timeout` as a non-activation event (same class as clear for relay actuation).
     - Do not call relay activation path for `relay_timeout`.
     - Prefer `activateLocalAlarm(idx, false)` (or no local relay actuation) for `relay_timeout` notifications.

2. **High: manual relay duration can be bypassed for standalone/manual-owned relays**
   - Evidence:
     - Manual command stores custom duration via runtime helper: `TankAlarm-112025-Client-BluesOpta.ino` line 7198.
     - Timeout loop skips any relay whose owner monitor is out of range: line 7269.
   - Impact:
     - A manual command with `duration` may never time out if no monitor owns that relay bit.
   - Required fix:
     - In `checkRelayMomentaryTimeout()`, evaluate `RELAY_SRC_MANUAL` + `customDurationSec` even when `ownerMonitor >= monitorCount`.
     - Use monitor config only as fallback when an owning monitor exists.

3. **Medium: duplicate relay writes in manual command path**
   - Evidence:
     - Direct write occurs before helper call: `TankAlarm-112025-Client-BluesOpta.ino` line 7191.
     - Helper then writes relay again: line 7198 (ON path) and line 7202 (OFF path via deactivate helper).
   - Impact:
     - Redundant GPIO writes and duplicate logging; possible edge/timing side effects with external relay hardware.
   - Required fix:
     - Remove the direct `setRelayState(relayNum, state)` call in `processRelayCommand()`.
     - Keep helper functions as the single source of truth for state transitions.

4. **Medium: DFU verification proves write consistency, not artifact authenticity**
   - Evidence:
     - CRC computed from downloaded bytes and compared to read-back flash CRC (`TankAlarm_DFU.h` lines 412, 520, 548).
   - Impact:
     - A wrong artifact that is consistently written still passes verification.
   - Required fix:
     - Add expected artifact hash/size from Server metadata and compare against that expected value before reboot.

5. **Low: unused DFU decode-buffer allocation adds failure surface**
   - Evidence:
     - `b64Buf` allocated and checked (`TankAlarm_DFU.h` lines 400, 402) but not used in decode path.
   - Impact:
     - Unnecessary heap pressure can cause update failure even when required buffers are available.
   - Required fix:
     - Remove `b64Buf` allocation/check/free or use it meaningfully.

## Implementation Review (Gemini 3.1 Pro Preview)

**Date:** April 8, 2026  
**Reviewer:** Gemini 3.1 Pro (Preview)  
**Scope reviewed:** Implemented Future Work changes reflected in `V1.6.0_RELEASE_NOTES.md` against original implementation plans.

### Overall Assessment

The v1.6.0 release successfully implements the core objectives of PR-1 (Relay Runtime), PR-2 (Debounce Counter Split), and PR-4 (CRC-32 Integrity Verification). Consolidating the scattered relay global arrays into `RelayRuntime` and standardizing the lifecycle helpers represent a massive improvement to the system's robustness. However, there is a **critical bug introduced in the DFU CRC verification logic** that will cause false-positive update failures, requiring an immediate patch before the next firmware deployment.

### 1. PR-4: DFU Integrity Verification (CRIT-02)

**Implementation Status:** Implemented with a Critical Flaw.

*   **The Issue - The Padding Hash Mismatch Bug:** According to the release notes, the running CRC-32 is accumulated over each decoded chunk's "raw bytes (before page-alignment padding)." However, the verification step "Reads back the entire written flash region... computes read-back CRC-32 in 4KB chunks."
*   **Why it fails:** Flash memory must be programmed in page-aligned blocks (e.g., 256 bytes). If a firmware binary is `500,001` bytes long, `flash.program()` will pad the end with hardware-specific filler (`0xFF` or `0x00`) up to the next page boundary (e.g., `500,224` bytes). Reading back the "entire written flash region" (all `500,224` bytes) and hashing it will yield a completely different CRC than the hash of the strictly unpadded `500,001` downloaded bytes.
*   **Required Fix:** The flash read-back loop must be strictly bounded to exactly `firmwareLength` bytes. If it reads back in 4KB chunks, the final chunk read must be truncated to `firmwareLength % 4096` bytes before hashing.
*   **Secondary Issue (Deferred):** The implementation successfully performs Phase 1 (CRC validation), but Phase 2 (A/B bank swap) remains deferred. A power loss during the actual flash writing loop will still result in a bricked device since the active application area is erased in Step 1.

### 2. PR-1: Relay Runtime Foundation + Safety Timeout (MED-06 + MED-02)

**Implementation Status:** Successfully Implemented with positive enhancements.

*   **Pros:** 
    *   The introduction of the `RelaySource` enum and `RelayRuntime` struct fully satisfies the MED-06 unification requirements.
    *   Adding `monitorIdx` overlap validation during config parsing is a great defensive measure. While the release notes indicate it only logs a warning, this is an excellent first step toward full enforcement.
    *   `relayMaxOnSeconds` handles the manual-reset safety timeout constraint well and properly iterates over the new unified state array.
*   **Minor Critique:** If two monitors share a relay bit (triggering the overlap warning), the `findMonitorForRelay(relayIndex)` helper will simply return the *first* matching monitor it finds. This means the timeout configuration of the first monitor will silently override the configuration of the second. Strictly rejecting overlapping relay bounds in the future config schema will fix this ambiguity.

### 3. PR-2: Debounce Counter Split (MED-03)

**Implementation Status:** Successfully Implemented.

*   **Pros:** Precisely matches the MED-03 spec. Splitting `clearDebounceCount` into `highClearDebounceCount` and `lowClearDebounceCount` allows the analog paths to evaluate independently while mapping the digital paths to the high counter, cleanly resolving the read-starvation edge cases for oscillating sensors. No issues found.

6. **Low: missing upper-bound guard on decoded chunk length**
   - Evidence:
     - Decoded length is trusted from `tankalarm_b64decode` and directly advances `offset` (`TankAlarm_DFU.h` lines 460, 487).
   - Impact:
     - Unexpected oversized payload could advance beyond intended chunk/remaining bounds.
   - Required fix:
     - Enforce `decoded > 0 && decoded <= thisChunk && decoded <= remaining`; abort update on violation.

### What Was Implemented Correctly

1. **Relay/runtime foundation is in place**
   - `RelayRuntime` added and used broadly (`TankAlarm-112025-Client-BluesOpta.ino` line 723 onward).
   - `relayMaxOnSeconds` field added, parsed, serialized, and capped (`lines 495, 2350-2351, 2836-2837`).
   - Server Config Generator includes safety max-on UI and payload round-trip support (`TankAlarm-112025-Server-BluesOpta.ino` line 1468+ and 1493+).

2. **Debounce split (MED-03) is correctly integrated**
   - Separate clear counters added and initialized (`lines 583-584, 1333-1334`).
   - High/low clear paths are independent in alarm evaluation (`lines 4532-4577`).

3. **CRC read-back verification (CRIT-02 partial) is correctly wired**
   - Running download CRC, read-back CRC, mismatch abort/no reboot behavior are present (`TankAlarm_DFU.h` lines 412, 524-555).

### Were New Problems Introduced?

Yes. The critical `relay_timeout` re-activation path is a new behavioral regression and should be fixed before treating the safety-timeout implementation as complete.

### Recommended Immediate Patch Sequence

1. Patch `sendAlarm()` to treat `relay_timeout` as non-activating and non-latching for relay actuation.
2. Patch timeout logic to honor manual custom duration even without owner monitor.
3. Remove duplicate `setRelayState()` in manual path.
4. Add DFU bounds checks (`decoded` vs `thisChunk`/`remaining`) and remove unused `b64Buf`.
5. Keep CRIT-01 (safe install architecture) in backlog; PR-4 reduced risk but did not eliminate erase-before-download exposure.

### GitHub Copilot (GPT-5.4) Review

**Date:** April 8, 2026  
**Reviewer:** GitHub Copilot (GPT-5.4)  
**Scope reviewed:** Current source implementation of the v1.6.0 Future Work changes described in PR-1, PR-2, and PR-4

#### Verdict

- The debounce split work was implemented cleanly.
- The CRC read-back verification is internally coherent and is a real improvement.
- The relay runtime and safety-timeout work was only partially implemented correctly.
- Not all changes were made properly. Yes, implementation errors were made. Yes, new problems were introduced.

#### Findings (Ordered by Severity)

1. **Critical: `relay_timeout` can immediately turn the relay back ON after the safety timeout fires**
   - Evidence:
     - Timeout path emits `sendAlarm(monIdx, "relay_timeout", ...)`: `TankAlarm-112025-Client-BluesOpta.ino` line 7321.
     - `sendAlarm()` treats every type except `"clear"` as an alarm: line 4804.
     - `RELAY_TRIGGER_ANY` then activates relays unconditionally: lines 4816-4817.
   - Impact:
     - A relay that was just forced OFF for safety can be re-energized in the same timeout flow, defeating the main purpose of `relayMaxOnSeconds`.
   - Required fix:
     - Treat `relay_timeout` as a non-activation event for relay actuation.
     - In `sendAlarm()`, exclude `relay_timeout` from the relay activation branch, even when `relayTrigger == RELAY_TRIGGER_ANY`.

2. **High: Server treats `relay_timeout` like a sensor clear and clears the live alarm state**
   - Evidence:
     - Client emits `relay_timeout` using the monitor's sensor identity: `TankAlarm-112025-Client-BluesOpta.ino` line 7321.
     - Server enters the clear path for `relay_timeout`: `TankAlarm-112025-Server-BluesOpta.ino` lines 8401-8410.
     - That path sets `alarmActive = false` and calls `clearAlarmEvent(...)`: lines 8403-8410.
   - Impact:
     - If the underlying high/low alarm condition is still active, the Server clears the dashboard/event state anyway.
     - Because the Client alarm latch remains set, the original high/low alarm may not be re-sent, leaving the Server in a falsely cleared state.
   - Required fix:
     - Handle `relay_timeout` as a distinct operational event, not as a synonym for `clear`.
     - Do not clear `rec->alarmActive` or call `clearAlarmEvent(...)` when processing `relay_timeout`.
     - Log/display it separately and decide SMS policy independently from sensor-clear semantics.

3. **Medium: standalone manual relays ignore custom duration because the timeout loop skips ownerless relays**
   - Evidence:
     - Manual command stores runtime duration via helper path: `TankAlarm-112025-Client-BluesOpta.ino` line 7198.
     - Timeout loop skips ownerless relays immediately: line 7269.
   - Impact:
     - A manual relay command with `duration` will never auto-expire when that relay is not owned by any configured monitor.
     - This leaves a gap in the release-note claim that manual relay duration is supported.
   - Required fix:
     - In `checkRelayMomentaryTimeout()`, evaluate `RELAY_SRC_MANUAL` plus `customDurationSec` even when `ownerMonitor >= monitorCount`.
     - Only use monitor config as fallback when an owning monitor exists.

4. **Medium: overlapping relay ownership is still ambiguous because validation only warns and runtime still uses first-match ownership**
   - Evidence:
     - Parser only logs a warning for shared relay bits: `TankAlarm-112025-Client-BluesOpta.ino` lines 2668-2676.
     - Ownership resolution still returns the first match: lines 7020-7024.
   - Impact:
     - Manual commands, timeout ownership, and reset behavior remain nondeterministic when two monitors share a relay bit.
     - The refactor reduced bookkeeping sprawl, but it did not actually eliminate the ambiguity that the design review called out.
   - Required fix:
     - Reject overlapping `relayMask` assignments during config validation and in the Config Generator UI.
     - Do not rely on warning-only behavior here.

5. **Low: `clearAllRelayAlarms()` can leave stale runtime state for standalone/manual relays**
   - Evidence:
     - Function clears monitor-owned relays via `resetRelayForMonitor()`: `TankAlarm-112025-Client-BluesOpta.ino` lines 7418-7425.
     - It then force-clears any remaining physical relays with `setRelayState(r, false)` only: lines 7430-7433.
   - Impact:
     - A standalone manual relay can be physically turned OFF while its `gRelayRuntime[r]` entry remains marked active with old owner/source/timing metadata.
     - That leaves GPIO state and runtime bookkeeping out of sync.
   - Required fix:
     - Replace the direct `setRelayState()` fallback with runtime-aware cleanup, or explicitly zero the matching `gRelayRuntime[r]` entries in the second loop.

#### What Was Implemented Properly

1. **Debounce split is sound**
   - `highClearDebounceCount` and `lowClearDebounceCount` were added and initialized correctly.
   - The analog high-clear and low-clear paths now operate independently.

2. **Schema/UI integration for `relayMaxOnSeconds` is present**
   - Client config version bumped to 2.
   - The field is parsed, capped, and serialized.
   - Server Config Generator round-trips the field and exposes a matching manual-reset UI control.

3. **CRC verification reduced DFU risk**
   - The running download CRC and read-back flash CRC are both present.
   - Mismatch correctly aborts reboot and reports failure.

#### Remaining Caveat on DFU

- The CRC work improves write verification, but it still does **not** solve the original erase-before-safe-install problem.
- If programming fails or CRC mismatches after the active image has already been erased, the device still has no valid application image for the next reboot.
- So PR-4 is a valid partial improvement, not a full resolution of the future-work DFU risk.

#### Final Answer To The Review Questions

- **Were all the changes made properly?** No. Several parts were implemented well, but the relay timeout path is not correct end-to-end.
- **Were any errors made?** Yes. The biggest errors are the `relay_timeout` relay re-activation path and the Server treating `relay_timeout` as a real alarm clear.
- **Were any new problems introduced?** Yes. Those two `relay_timeout` behaviors are new regressions introduced by the implementation.

---

## Section 13: v1.6.1 Implementation Review Fixes (Applied)

All 9 confirmed findings from the Section 12 implementation reviews have been addressed in v1.6.1.
Finding #5 (DFU CRC padding mismatch) was verified as a **false positive** — Gemini was wrong; the readback
loop uses `firmwareLength`, not page-aligned size.

### Fixes Applied

| # | Severity | Finding | Fix |
|---|----------|---------|-----|
| 1 | **CRITICAL** | `relay_timeout` re-activates relay via `sendAlarm()` (infinite loop) | `isAlarm` now excludes `relay_timeout`: `bool isAlarm = ... && !isRelayTimeout;` — relay activation block only fires for actual alarms |
| 2 | **HIGH** | Server clears alarm state on `relay_timeout` | `relay_timeout` now handled as a separate branch that does NOT set `alarmActive = false` or call `clearAlarmEvent()` |
| 3 | **MEDIUM** | Ownerless manual relay duration ignored by `checkRelayMomentaryTimeout()` | Added `else if` branch for `RELAY_SRC_MANUAL` with `customDurationSec > 0` when `monIdx >= gConfig.monitorCount` |
| 4 | **MEDIUM** | Duplicate `setRelayState()` in `processRelayCommand()` | Removed bare `setRelayState(relayNum, state)` call — helpers already call it internally |
| 5 | **FALSE POSITIVE** | DFU CRC padding mismatch | No fix needed — readback uses `firmwareLength`, not page-aligned size |
| 6 | **LOW** | `b64Buf` allocated but never used in DFU decode | Removed `b64Buf` allocation, null-check, and all `free(b64Buf)` calls |
| 7 | **LOW** | Missing decoded length bounds check in DFU | Added bounds check: `if ((uint32_t)decoded > thisChunk \|\| (uint32_t)decoded > remaining)` aborts DFU |
| 8 | **LOW** | `clearAllRelayAlarms()` leaves stale `gRelayRuntime[]` | Fallback loop now also checks `gRelayRuntime[r].active` and zeros `gRelayRuntime[r] = {}` |
| 9 | **MEDIUM** | Overlap validation warns but doesn't reject | Design decision — warn-only preserved; `findMonitorForRelay()` uses first-match. Rejection would change user-facing behavior. |
| 10 | **LOW** | DFU erases before download (known) | Known Phase 2 limitation — deferred to A/B partitioning |

### Files Modified
- `TankAlarm-112025-Client-BluesOpta.ino` — Fixes 1, 3, 4, 8
- `TankAlarm-112025-Server-BluesOpta.ino` — Fix 2
- `TankAlarm-112025-Common/src/TankAlarm_DFU.h` — Fixes 6, 7
- `TankAlarm-112025-Common/src/TankAlarm_Common.h` — Version bump to 1.6.1
