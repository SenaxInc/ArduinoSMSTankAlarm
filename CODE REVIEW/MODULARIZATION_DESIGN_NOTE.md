# Modularization Design Note & Phased Split Plan

**Issue:** #250 (workstream 5)  
**Date:** 2026-02-26  
**Firmware:** TankAlarm 112025 v1.1.3  

## Current Architecture

### File Inventory

| Component | File | Lines | Role |
|-----------|------|------:|------|
| **Client** | `TankAlarm-112025-Client-BluesOpta.ino` | ~6,050 | Tank monitoring, alarms, telemetry, power mgmt |
| **Server** | `TankAlarm-112025-Server-BluesOpta.ino` | ~10,900 | Web UI, fleet management, historical data, SMS |
| **Viewer** | `TankAlarm-112025-Viewer-BluesOpta.ino` | ~710 | Read-only dashboard via Notecard relay |
| **I2C Utility** | `TankAlarm-112025-I2C_Utility.ino` | ~480 | Notecard troubleshooting tool |

### Shared Library (`TankAlarm-112025-Common/src/`)

| Header | Lines | Responsibility |
|--------|------:|----------------|
| `TankAlarm_Common.h` | 191 | Master include, notefile names, constants |
| `TankAlarm_Platform.h` | 242 | Platform detection, filesystem, watchdog, atomic writes |
| `TankAlarm_Config.h` | 55 | Default timing/scheduling constants |
| `TankAlarm_Utils.h` | 72 | Numeric utilities, time alignment |
| `TankAlarm_Notecard.h` | 79 | Notecard I2C helpers |
| `TankAlarm_Solar.h/cpp` | 655 | SunSaver MPPT Modbus integration |
| `TankAlarm_Battery.h` | 321 | Battery monitoring structs and configs |
| `TankAlarm_Diagnostics.h` | 82 | Shared heap diagnostics, health telemetry struct |

---

## Module Map

The following map identifies logical modules embedded within the monolithic
`.ino` files and their ownership of state/interfaces.

### Client Modules (embedded in ~6,050-line .ino)

| Module | Approx. Lines | State Owned | Interfaces |
|--------|--------------|-------------|------------|
| **Sensor Reading** | ~400 | `gMonitorState[]`, ADC/I2C state | `readTankSensor()`, `readCurrentLoopMilliamps()` |
| **Alarm Engine** | ~500 | `gAlarmState[]`, debounce counters | `evaluateAlarms()`, `checkAlarmRateLimit()`, `sendAlarm()` |
| **Unload Detection** | ~200 | Per-tank peak/trough tracking | `evaluateUnload()`, `sendUnloadEvent()` |
| **Notecard Comms** | ~600 | `notecard`, `gDeviceUID`, failure counters | `publishNote()`, `bufferNoteForRetry()`, `flushBufferedNotes()` |
| **Configuration** | ~800 | `gConfig`, dirty flag, flash persistence | `loadConfig()`, `saveConfigToFlash()`, `applyConfigUpdate()` |
| **Power State Machine** | ~300 | `gPowerState`, debounce, voltage state | `updatePowerState()`, `getEffectiveBatteryVoltage()` |
| **Relay Control** | ~250 | `gRelayState[]`, momentary timers | `setRelayState()`, `processRelayCommand()`, `checkRelayMomentaryTimeout()` |
| **Daily Reports** | ~250 | Schedule epoch, report parts | `sendDailyReport()`, `scheduleNextDailyReport()` |
| **Solar-Only Mode** | ~300 | Solar state, sunset protocol, debounce | `performStartupDebounce()`, `checkSolarOnlySunsetProtocol()` |
| **Serial Log Buffer** | ~150 | Circular buffer of log messages | `addSerialLog()`, `sendSerialLogs()` |
| **DFU** | ~80 | DFU state flags | `checkForFirmwareUpdate()`, `enableDfuMode()` |
| **Diagnostics** | ~100 | — | `safeSleep()`, `freeRam()`, `sendHealthTelemetry()` |

### Server Modules (embedded in ~10,900-line .ino)

| Module | Approx. Lines | State Owned | Interfaces |
|--------|--------------|-------------|------------|
| **Web Server + HTML** | ~5,000 | Embedded HTML, route handlers | HTTP endpoint handlers |
| **Fleet Management** | ~800 | `gTankRecords[]`, client metadata cache | `processTelemetry()`, `processAlarm()` |
| **Contact/SMS** | ~600 | Contact list, Twilio integration | `sendSmsAlarm()`, `saveContactsConfig()` |
| **Historical Data** | ~1,200 | Tiered ring buffers, hot/warm/cold tiers | `recordHistoryPoint()`, `queryHistory()` |
| **Calibration** | ~800 | Calibration log, per-tank entries | `saveCalibrationEntry()`, `applyCalibration()` |
| **Notecard Comms** | ~400 | Notecard, device UID | Same pattern as client |
| **Configuration** | ~600 | Server config, per-client config snapshots | `saveServerConfig()`, `loadServerConfig()` |
| **Viewer Summary** | ~200 | Summary cache | `publishViewerSummary()` |
| **Diagnostics** | ~100 | — | `safeSleep()`, `freeRam()` |

---

## Phased Extraction Plan

### Guiding Principles

1. **No behavioral changes** — each extraction is purely structural.
2. **One module per phase** — validate compilation after each.
3. **Arduino `.ino` multi-file model** — extracted modules become additional
   `.ino` files in the sketch folder (Arduino concatenates all `.ino` files)
   or `.h`/`.cpp` pairs in the shared library.
4. **Shared state via `extern`** — when a module needs globals from the main
   sketch, declare them `extern` in the module header.
5. **Test at each checkpoint** — compile + basic functional test after each phase.

### Phase 0 — Preparation (Low risk)

**Already completed** as part of #247 and #250:
- [x] Atomic file writes for all critical persistence paths
- [x] Shared `TankAlarm_Diagnostics.h` for `freeRam()`
- [x] `TankAlarm_Platform.h` for watchdog, filesystem, POSIX helpers
- [x] `TankAlarm_Solar.h/cpp` extracted to shared library
- [x] `TankAlarm_Battery.h` extracted to shared library

**Checkpoint:** All 4 sketches compile. CI green.

---

### Phase 1 — Extract Client Sensor Reading (~400 lines)

**Target:** Move `readTankSensor()`, `readCurrentLoopMilliamps()`,
`linearMap()`, conversion factor helpers, and sensor validation into a
dedicated module.

**Deliverable:** `TankAlarm_Sensors.h` in shared library (or
`client_sensors.ino` in sketch folder).

**State transferred:** Sensor config structs, ADC channel mappings.

**Risk:** Low — pure functions with minimal state coupling.

**Testing:** Sensor readings match before/after extraction.

---

### Phase 2 — Extract Client Alarm Engine (~500 lines)

**Target:** Move `evaluateAlarms()`, `checkAlarmRateLimit()`, alarm debounce
logic, and `sendAlarm()` wrapper.

**Deliverable:** `client_alarms.ino` in sketch folder.

**State transferred:** `gAlarmState[]`, `gAlarmHistory[]`, per-tank alarm
counters. These remain as `extern` globals referenced from the module.

**Risk:** Medium — alarm state is tightly coupled with sensor readings and
relay control. Needs careful interface definition.

**Testing:** Alarm trigger/clear/debounce behavior unchanged.

---

### Phase 3 — Extract Client Power State Machine (~300 lines)

**Target:** Move `updatePowerState()`, `getEffectiveBatteryVoltage()`,
`sendPowerStateChange()`, voltage thresholds, and debounce logic.

**Deliverable:** `client_power.ino` in sketch folder.

**State transferred:** `gPowerState`, `gPowerStateDebounce`,
`gEffectiveBatteryVoltage`, transition timestamps.

**Risk:** Medium — power state affects loop timing, relay safety, and
telemetry intervals. All callers of `gPowerState` need the header.

**Testing:** All 7 scenarios in `POWER_STATE_TEST_COVERAGE.md`.

---

### Phase 4 — Extract Client Notecard Communications (~600 lines)

**Target:** Move `publishNote()`, `bufferNoteForRetry()`,
`flushBufferedNotes()`, `pruneNoteBufferIfNeeded()`, and health check.

**Deliverable:** `client_notecard.ino` in sketch folder.

**State transferred:** Note buffer state, failure counters, `gNotecardAvailable`.

**Risk:** Medium — many functions call `publishNote()`. Interface must be
stable.

**Testing:** Telemetry delivery, buffered note retry, prune behavior.

---

### Phase 5 — Extract Server Web UI (~5,000 lines)

**Target:** Move all HTML generation, HTTP route handlers, and web API
endpoints out of the main sketch. This is the single largest extraction.

**Deliverable:** `server_web.ino` + `server_html.h` (PROGMEM strings).

**State transferred:** Read access to `gTankRecords[]`, config, history.
Write access for config endpoints.

**Risk:** High — largest block, most coupling. Consider splitting further:
  - Dashboard views
  - Configuration endpoints  
  - API endpoints
  - Static assets (CSS/JS)

**Testing:** All web pages render correctly. Config changes persist.

---

### Phase 6 — Extract Server Historical Data (~1,200 lines)

**Target:** Move tiered ring buffer implementation, tier management,
snapshot/restore, and query API.

**Deliverable:** `TankAlarm_History.h/cpp` in shared library (or
`server_history.ino` in sketch folder).

**State transferred:** Ring buffer arrays, tier config, write pointers.

**Risk:** Medium — ring buffer logic is self-contained but data structures
are large.

**Testing:** History recording, tier rollover, CSV export.

---

### Phase 7 — Extract Server Contacts/SMS (~600 lines)

**Target:** Move contact management, Twilio API integration, SMS formatting.

**Deliverable:** `server_contacts.ino` in sketch folder.

**State transferred:** Contact list, Twilio credentials, rate limiting.

**Risk:** Low — well-bounded external API integration.

**Testing:** SMS delivery, contact CRUD operations.

---

## Risk Summary

| Phase | Module | Risk | Lines | Dependencies |
|-------|--------|------|------:|-------------|
| 1 | Sensor Reading | Low | ~400 | ADC, I2C |
| 2 | Alarm Engine | Medium | ~500 | Sensors, Relays, Notecard |
| 3 | Power State | Medium | ~300 | Battery, Solar, Relays |
| 4 | Notecard Comms | Medium | ~600 | All modules (publisher) |
| 5 | Server Web UI | High | ~5,000 | Fleet, History, Config, Contacts |
| 6 | Server History | Medium | ~1,200 | Fleet data, Web UI |
| 7 | Server Contacts | Low | ~600 | Twilio API only |

---

## Out of Scope

- Full refactor to C++ class hierarchy (too invasive for Arduino `.ino` model)
- Reverting ArduinoJson to v6 static/dynamic sizing
- Behavioral changes to power-state or alarm logic
- Splitting the Viewer (only ~710 lines, not worth separate modularization)
- Splitting the I2C Utility (only ~480 lines, standalone tool)

---

## Recommended Execution Order

1. **Phase 1** (Sensors) — lowest risk, builds familiarity with extraction process
2. **Phase 4** (Notecard Comms) — high reuse value, many callers
3. **Phase 2** (Alarms) — benefits from Phase 1 and 4 being done
4. **Phase 3** (Power State) — self-contained after alarm extraction
5. **Phase 7** (Contacts) — low risk server-side start
6. **Phase 6** (History) — moderate complexity, self-contained
7. **Phase 5** (Web UI) — last and largest, benefits from all prior extractions
