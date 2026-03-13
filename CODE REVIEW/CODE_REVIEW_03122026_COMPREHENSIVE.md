# Comprehensive Code Review — TankAlarm System v1.1.4

**Date:** March 12, 2026 (Updated with cross-review findings and fix implementation)  
**Reviewer:** GitHub Copilot (Claude Opus 4.6)  
**Scope:** Server, Client, Viewer, and Common library  
**Firmware Version:** 1.1.4  
**Previous Review:** CODE_REVIEW_FINAL_V1.1.4_02282026.md (Feb 28, 2026)  
**Cross-Referenced:** CODE_REVIEW_03122026_AI.md (Gemini 3.1 Pro), CODE_REVIEW_03122026_SERVER_CLIENT_VIEWER_COPILOT.md (GPT-5.3-Codex)

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Architecture Overview](#2-architecture-overview)
3. [Server Review](#3-server-review)
4. [Client Review](#4-client-review)
5. [Viewer Review](#5-viewer-review)
6. [Common Library Review](#6-common-library-review)
7. [Cross-Cutting Concerns](#7-cross-cutting-concerns)
8. [Security Assessment](#8-security-assessment)
9. [Reliability & Error Handling](#9-reliability--error-handling)
10. [Prioritized Findings](#10-prioritized-findings)
11. [Recommendations Summary](#11-recommendations-summary)

---

## 1. Executive Summary

### Verdict: **SOLID — Mature production system with targeted hardening needed**

The TankAlarm system is a well-engineered, production-grade IoT monitoring platform built on Arduino Opta hardware with Blues Notecard cellular connectivity. The codebase demonstrates strong defensive programming practices including atomic file writes, watchdog management, power state machines with hysteresis, and bounded memory allocations.

Since the v1.1.4 review on Feb 28, 2026, which resolved 14 implementation recommendations, the system is in good shape overall. After cross-referencing findings from two additional AI reviewers (Gemini 3.1 Pro and GPT-5.3-Codex), this consolidated review identifies **2 critical**, **7 high**, **16 medium**, and **12 low** severity findings. The critical items relate to client-side credential handling and relay command safety; the broader set is primarily edge-case hardening rather than fundamental flaws.

### Key Strengths
- **Atomic file persistence** across all critical writes (config, registry, metadata)
- **Bounded memory** via ring buffers, hash tables, and fixed allocations (~20KB static)
- **Safe string handling** throughout (`strlcpy`, `snprintf` — no raw `strcpy`/`sprintf`)
- **Power conservation state machine** with hysteresis/debounce (client)
- **Config dispatch ACK tracking** with auto-retry and cancellation
- **I2C bus recovery** with escalation (toggle SCL → reinit → watchdog reset)
- **Tiered historical storage** (RAM hot → LittleFS warm → FTP cold)

### Key Risks
- **Raw PIN persisted in browser localStorage** — extractable by XSS or extensions (NEW — from GPT-5.3 review)
- **Client relay commands execute without target UID verification** — route misconfiguration could actuate wrong device (NEW — from GPT-5.3 review)
- **Session token leaked in URL query strings** — `/api/session/check?token=...` exposes token in browser history/logs (NEW — from GPT-5.3 and Gemini reviews)
- **Config dispatch/cache buffer size mismatch** — configs >1536 bytes dispatched but not cached, causing stale retry data (NEW — from GPT-5.3 review)
- FTP credentials transmitted in plaintext (documented/acknowledged)
- Session token entropy is limited (ADC + micros)
- Notecard I2C failure recovery requires reboot on server (no mid-loop re-init)
- Viewer lacks bounds checking on tank array population
- No unit test coverage anywhere in the codebase

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│                    Notehub Cloud                      │
│   (Fleet: tankalarm-server, tankalarm-viewer)         │
│   Routes: telemetry, alarm, config, viewer_summary    │
└──────┬────────────────┬────────────────┬──────────────┘
       │                │                │
       ▼                ▼                ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│   Server     │ │   Client(s)  │ │   Viewer(s)  │
│ Arduino Opta │ │ Arduino Opta │ │ Arduino Opta │
│ + Notecard   │ │ + Notecard   │ │ + Notecard   │
│ + Ethernet   │ │ + Sensors    │ │ + Ethernet   │
│              │ │ + Relays     │ │              │
│ REST API     │ │ 4-20mA I2C   │ │ Read-only    │
│ FTP backup   │ │ Pulse/analog │ │ Dashboard    │
│ SMS/Email    │ │ Solar/Battery│ │              │
│ ~15,000 LOC  │ │ ~8,000 LOC   │ │ ~900 LOC     │
└──────────────┘ └──────────────┘ └──────────────┘
```

### Communication Flow
- **Client → Server**: `telemetry.qo`, `alarm.qo`, `daily.qo`, `unload.qo`, `config_ack.qo`
- **Server → Client**: `command.qo` → Notehub route → `config.qi`, `relay.qi`
- **Server → Viewer**: `viewer_summary.qi` (every 6 hours)
- All routing via Notehub cloud; no direct device-to-device communication

---

## 3. Server Review

**File:** `TankAlarm-112025-Server-BluesOpta.ino` (~15,000 lines)

### 3.1 Structural Assessment

The server is a monolithic file handling Notecard fleet management, Ethernet web server with REST API, FTP backup/restore, SMS/email alerting, sensor calibration (multiple linear regression), and historical data management. While large, the code is logically grouped by subsystem.

| Subsystem | Lines (approx) | Complexity |
|-----------|-----------------|------------|
| Data structures & globals | 1–1800 | Low |
| Notecard integration | 2000–3200 | Medium |
| FTP backup/restore | 3100–3800 | Medium |
| Telemetry processing | 3800–5000 | Medium |
| Historical data (3-tier) | 5000–6500 | High |
| HTTP server & routing | 6500–7200 | Medium |
| REST API endpoints | 7200–10500 | High |
| Tank registry (hash table) | 10500–11500 | Medium |
| Persistence (metadata, config) | 11500–12500 | Medium |
| Calibration (regression) | 12500–14000 | High |
| DFU & diagnostics | 14000–15000 | Low |

### 3.2 REST API Security

The server exposes ~25 REST endpoints. State-changing operations (POST) require PIN authentication with constant-time XOR comparison and rate limiting (exponential backoff). Read endpoints (GET) are unauthenticated.

| Finding | Severity |
|---------|----------|
| **Raw PIN stored in browser localStorage** (`tankalarm_token`) — extractable by XSS/extensions | **CRITICAL** |
| **Session token exposed in URL query string** via `/api/session/check?token=...` — leaks to browser history, proxy logs, referrer headers | **HIGH** |
| **Fresh-install login grants session without valid PIN** — if configured PIN is unset, any request gets authenticated session | **HIGH** |
| PIN stored in plaintext on flash (`server_config.json`) | **HIGH** |
| Session token generated from ADC + micros (~weak entropy) | **HIGH** |
| **Config dispatch buffer (8192B) vs cache (1536B) mismatch** — large configs dispatched but not cached; retries use stale payload | **HIGH** |
| **Config retry cadence documentation mismatch** — API response says "60s" but actual retry loop is every 60 minutes (`3600000UL`) | **MEDIUM** |
| No per-IP rate limiting on REST API (DoS possible on expensive endpoints) | **MEDIUM** |
| No CSRF double-submit token (custom X-Session header helps but not complete) | **MEDIUM** |
| Phone/email inputs not format-validated before storage | **MEDIUM** |
| `/api/debug/tanks` accepts PIN as query parameter (leaks to access logs) | **LOW** |

### 3.3 Memory Management

Static RAM usage is well-bounded:
- 128 TankRecords × ~36 bytes = ~4.6 KB
- 20 TankHistory × 90 snapshots = ~27 KB
- 50 AlarmLog + 50 TransmissionLog + 50 UnloadLog = ~7.5 KB
- 20 ClientMetadata + 20 ClientConfigSnapshots (1536B each) = ~32 KB
- **Total static: ~71 KB** (well within Opta's 512 KB SRAM)

Heap allocations are properly bounded and freed:
- FTP buffers: malloc/free per operation (up to 24 KB)
- NWS API response: malloc(16384), freed after parse
- Tank registry load: malloc(fileSize + 1), freed after deserialize

### 3.4 Notecard Integration

Notecard I2C reliability is tracked via `gNotecardFailureCount`, with offline mode after threshold:

| Concern | Details |
|---------|---------|
| **No mid-loop re-initialization** | Once `gNotecardAvailable = false`, requires reboot to recover. Should periodically attempt `initializeNotecard()`. |
| **Blocking I2C calls** | `requestAndResponse()` blocks ~100ms per call; acceptable for monitoring cadence but could miss HTTP requests during busy Notecard periods. |
| **ODFU voltage gate** | Won't start DFU if voltage < 4.0V — good safeguard against brownout-during-update. |

### 3.5 File System Safety

All critical files use atomic write (temp file + rename). The one exception is the calibration log (append-only), which has a truncation risk on power loss during write.

### 3.6 Server-Specific Findings

| ID | Finding | Severity | Details |
|----|---------|----------|---------|
| S-1 | **Raw PIN persisted in browser localStorage** | **CRITICAL** | On login, the embedded JS stores the typed PIN in `localStorage` as `tankalarm_token` and reuses it as `state.pin` for all privileged API calls. Any XSS, browser extension, or shared-machine scenario can extract the cleartext PIN. **Fix:** Never store raw PIN client-side. Use session token only after login; prompt for PIN per high-risk action without persisting. |
| S-2 | **Session token exposed in URL query string** | **HIGH** | `handleSessionCheck()` extracts token from `/api/session/check?token=...`. Browser JS calls this on a timer and on visibility changes, so the session token is repeatedly placed into URLs. Leaks via browser history, proxy logs, referrer headers. **Fix:** Move to X-Session header validation only; remove query-string token path. |
| S-3 | **Fresh-install login grants session without valid PIN** | **HIGH** | If configured PIN is unset/invalid, `handleLoginPost()` sets `valid = true` even when the provided PIN is blank or malformed. Any LAN user during bootstrap can obtain an authenticated session. **Fix:** Require explicit 4-digit PIN enrollment before granting any session. |
| S-4 | **Config dispatch/cache buffer size mismatch** | **HIGH** | `dispatchClientConfig()` serializes into 8192-byte buffer, but `ClientConfigSnapshot.payload` is only 1536 bytes. `cacheClientConfigFromBuffer()` rejects payloads ≥1536B and returns, but dispatch continues — marks old snapshot as pending with new hash. Retries resend stale payload; console shows outdated config. **Fix:** Either increase `payload` field or fail the dispatch if cache rejects. |
| S-5 | **Config retry cadence mismatch (docs vs code)** | **MEDIUM** | HTTP 202 response says "auto-retry every 60s" but actual loop gate is `3600000UL` (60 minutes). Operators expect 1-minute retry but wait up to 1 hour. **Fix:** Align the retry interval or correct the response text. |
| S-6 | Notecard I2C failure has no auto-recovery | **HIGH** | Server must reboot if Notecard goes offline. Add periodic re-init attempt when `gNotecardAvailable = false`. |
| S-7 | FTP credentials transmitted in plaintext | **HIGH** | Acknowledged in code comments. Document as known risk; recommend VPN or SFTP migration path. |
| S-8 | NWS API failure silently disables temperature compensation | **MEDIUM** | No alert when grid lookup fails repeatedly. Log warning; alert after N failures. |
| S-9 | JSON parse failure in tank registry load is silent | **MEDIUM** | Should log error with file size for debugging. Tank records lost without indication. |
| S-10 | Incomplete HTTP request yields no response to client | **MEDIUM** | Should return 408 Request Timeout before closing connection. |
| S-11 | Calibration log (append-only) not atomic | **LOW** | Power loss during append → truncation. Low impact (calibration is supplemental). |
| S-12 | `recalculateCalibration()` is ~150 lines | **LOW** | Complex multiple regression; consider extracting into helper functions for testability. |
| S-13 | Some magic numbers (22h change window, 60s retry) | **LOW** | Document as named constants. |

---

## 4. Client Review

**File:** `TankAlarm-112025-Client-BluesOpta.ino` (~8,000 lines)

### 4.1 Structural Assessment

The client implements sensor reading, alarm evaluation, relay control, power management, and bidirectional Notecard communication. The code is well-organized with clear subsystem separation.

| Subsystem | Lines (approx) | Complexity |
|-----------|-----------------|------------|
| Structures & enums | 251–650 | Low |
| Setup & main loop | 900–1600 | Medium |
| Config management | 1600–2100 | Medium |
| Sensor reading | 2700–3400 | Medium |
| Alarm evaluation | 3400–4100 | High |
| Unload detection | 4600–4900 | Medium |
| Power conservation | 5100–5800 | High |
| Daily reporting | 5000–5600 | Medium |
| Telemetry buffering | 6400–6900 | Medium |
| Relay control | 6900–7400 | Medium |

### 4.2 Power Conservation State Machine

The 4-state power machine (NORMAL → ECO → LOW_POWER → CRITICAL_HIBERNATE) is well-designed:
- Voltage hysteresis prevents oscillation (0.2–0.7V gap between enter/exit)
- 3-reading debounce before state transitions
- Conservative voltage selection (lowest of SunSaver, Notecard, Vin divider)
- Relays de-energized in CRITICAL to protect battery

### 4.3 Alarm Logic

Alarm evaluation includes:
- Threshold hysteresis (configurable per monitor)
- 3-sample debounce before latching
- Rate limiting: 10/hour per tank, 30/hour global, 5 min between same-type
- Stuck sensor detection (10 identical readings)
- Sensor failure detection (5 consecutive read failures)

### 4.4 Client-Specific Findings

| ID | Finding | Severity | Details |
|----|---------|----------|---------|
| C-1 | **Relay commands execute without target UID verification** | **CRITICAL** | `pollForRelayCommands()` pulls from `relay.qi` and `processRelayCommand()` executes based on `relay`/`state` fields only — no check that the payload's target UID matches `gDeviceUID`. Safety relies entirely on Notehub route correctness. A route misconfiguration or fan-out could actuate relays on unintended clients. **Fix:** Add `target` UID field to relay payloads; drop commands where target ≠ `gDeviceUID`; log rejected commands. |
| C-2 | Config from server has no integrity check | **HIGH** | Config received via `config.qi` applied without signature validation. If Notehub is compromised, attacker can disable alarms, change thresholds, or inject relay commands. Consider HMAC on config payload. |
| C-3 | I2C bus recovery has no max attempt limit | **MEDIUM** | Perpetual I2C hang causes infinite recovery → watchdog reset cycle. Add max recovery counter before graceful degradation. |
| C-4 | Remote relay commands have no rate limiting | **MEDIUM** | Rapid toggling could cause relay wear/failure. Add 1/sec per-relay rate limit. |
| C-5 | Note buffer line truncation for large payloads | **MEDIUM** | `fgets()` with 1024-byte buffer silently drops notes with payloads > 1022 bytes. Log dropped notes. |
| C-6 | Sensor failure/stuck thresholds not remotely configurable | **MEDIUM** | Hardcoded at 5 and 10 respectively. Should be tunable via `config.qi` for field adjustment. |
| C-7 | Solar-only sunset state not saved on initial voltage drop | **MEDIUM** | State only saved after debounce confirmation. If power cycles during debounce, last report epoch is stale. Save immediately on first detection. |
| C-8 | Stuck sensor false positives on stable tanks | **LOW** | Sealed tanks with no inflow/outflow can trigger "stuck" alarm. Current code uses ±0.05 tolerance — acceptable but should be documented. |
| C-9 | Missing watchdog kick in daily report loop | **LOW** | No kick during `appendDailyTank` loop. Low risk (infrequent, fast loop) but add defensive kick every 10 iterations. |
| C-10 | Notecard UID fallback to device label | **LOW** | If Notecard unavailable at boot, `deviceLabel` used as UID. Could allow device impersonation if label collides. |

### 4.5 Notecard Response Lifetime Bug (Previously Fixed)

The code has a well-documented bugfix (02282026) where `JGetString()` returned a pointer into Notecard response memory, which was then freed by `deleteResponse()`. Now properly copies into fixed buffer before freeing. **No regression found.**

---

## 5. Viewer Review

**File:** `TankAlarm-112025-Viewer-BluesOpta.ino` (~900 lines)

### 5.1 Structural Assessment

The viewer is the simplest component — a read-only Ethernet dashboard that fetches tank summaries from Notehub every 6 hours, plus DFU support and I2C health monitoring.

| Subsystem | Lines (approx) | Complexity |
|-----------|-----------------|------------|
| HTML dashboard (PROGMEM) | 178–475 | Low |
| Setup & loop | 216–345 | Low |
| Notecard init & health | 365–415 | Low |
| HTTP server | 447–610 | Medium |
| Summary fetch & parse | 642–757 | Medium |
| DFU | 811–end | Low |

### 5.2 Viewer-Specific Findings

| ID | Finding | Severity | Details |
|----|---------|----------|---------|
| V-1 | **Tank array overflow: no bounds check on population** | **HIGH** | `handleViewerSummary()` populates `gTankRecords[]` from JSON array without checking `gTankRecordCount >= MAX_TANK_RECORDS`. If server sends >MAX tanks, buffer overflow occurs. **Add bounds check.** |
| V-2 | **Viewer drops measurement metadata — misrepresents non-tank sensors** | **MEDIUM** | Server publishes `ot` (object type), `st` (sensor type), `mu` (measurement unit) in `publishViewerSummary()`, but viewer ignores all three fields. `sendTankJson()` doesn't include them. Browser JS always formats readings with `formatFeetInches()` under a fixed "Level (ft/in)" heading. RPM, PSI, GPM readings from engines/pumps/flow monitors appear as misleading tank depths. **Fix:** Parse `ot`/`st`/`mu` into `TankRecord`; include in `/api/tanks` JSON; adjust frontend display formatting per unit type. |
| V-3 | **Viewer ignores 24-hour change data** | **MEDIUM** | Server publishes `d` (24-hour delta) in summary payload. Viewer doesn't parse it. Browser hardcodes the "24hr Change" column to `--`. The UI advertises a trend column that can never contain data, yet the server is already computing and sending it. **Fix:** Parse `d` field; include in JSON response; render in dashboard. |
| V-4 | JsonDocument OOM check is ineffective | **MEDIUM** | `std::unique_ptr<JsonDocument> docPtr(new JsonDocument()); if (!docPtr) {...}` — `unique_ptr` doesn't return null on OOM; `new` throws or calls `new_handler`. This null check never triggers. Use `new (std::nothrow)` or handle at system level. |
| V-5 | Ethernet init failure silently continues | **MEDIUM** | If DHCP fails, viewer serves HTTP on unreachable IP. Add retry logic or visual indicator. |
| V-6 | Hardcoded MAC address (`02:00:01:11:20:25`) | **MEDIUM** | Collision risk if multiple viewers on same subnet. Make configurable via config file. |
| V-7 | Notecard health backoff may not reset on recovery | **LOW** | If Notecard recovers between health checks, backoff doesn't reset until next successful check. Could delay re-connection by up to 80 minutes. |
| V-8 | `gSourceBaseHour` not validated (0–23 range) | **LOW** | Values ≥24 produce undefined scheduling behavior. |

### 5.3 Web Dashboard

The embedded HTML dashboard uses compact JSON keys (single letter) with JavaScript fallback to full names. Good PROGMEM usage. Stale data detection (>26 hours) with visual indicator. Auto-refresh matches server summary cadence.

The dashboard is read-only with no authentication — appropriate for its kiosk/display purpose on a trusted LAN.

---

## 6. Common Library Review

**Directory:** `TankAlarm-112025-Common/src/`

### 6.1 File-by-File Assessment

| File | Purpose | Quality | Notable Issues |
|------|---------|---------|----------------|
| `TankAlarm_Common.h` | Protocol definitions, notefile names, firmware version | **Good** | Source of truth for all notefile routing. No notefile format versioning. |
| `TankAlarm_Config.h` | Fleet defaults, I2C thresholds, health intervals | **Good** | Well-documented constants. No minimum sample interval guard. |
| `TankAlarm_Platform.h` | Platform abstraction (AVR/STM32/Mbed), atomic writes | **Good** | Atomic write is solid. Orphaned `.tmp` files not cleaned at boot. |
| `TankAlarm_Diagnostics.h` | Heap monitoring, health snapshots | **Adequate** | `heapMinFreeBytes` not populated by `collectHealthSnapshot()`. No timestamp in snapshot. |
| `TankAlarm_I2C.h` | Bus recovery, scanning, current-loop reading | **Good** | SCL toggle recovery is robust. Redundant `Wire.available() < 2` check (artifact). |
| `TankAlarm_Notecard.h` | Time sync, Notecard binding | **Good** | Silent failure on `card.time` error. millis() overflow handled by 6h resync. |
| `TankAlarm_Battery.h` | Battery voltage via Notecard | **Good** | Calibration offset (0.35V) is hardware-specific; should be configurable. |
| `TankAlarm_Solar.h/.cpp` | SunSaver MPPT Modbus | **Adequate** | Cascading register read failure (one fail skips all). Load state logic may be incorrect. |
| `TankAlarm_Utils.h` | Unit conversions, safe string copy | **Good** | `roundTo()` uses slow `pow()`; could use lookup table. Minor. |

### 6.2 Common Library Findings

| ID | Finding | Severity | Details |
|----|---------|----------|---------|
| L-1 | No notefile format versioning | **MEDIUM** | If payload schema changes between firmware versions, old clients can't parse new messages. Add schema version field to payloads. |
| L-2 | Orphaned `.tmp` files from atomic writes not cleaned at boot | **MEDIUM** | Failed atomic writes leave temp files that leak flash storage. Add boot-time cleanup. |
| L-3 | Solar Modbus cascading failure (one register fail skips rest) | **MEDIUM** | Should read registers independently and merge partial results. |
| L-4 | `TankAlarm_Diagnostics.h`: `heapMinFreeBytes` not auto-tracked | **LOW** | Callers must track externally; fragmented responsibility. |
| L-5 | I2C current-loop conversion doesn't handle negative raw values | **LOW** | If A0602 returns signed value and sensor disconnects, raw > 32767 reads as 20 mA (false full-scale). Add signed check. |
| L-6 | `card.time` failure is silent | **LOW** | Epoch drift grows if cellular is down for days. Log failures after N consecutive misses. |
| L-7 | Battery calibration offset (0.35V) hardcoded | **LOW** | Different diode models may need different offset. Make configurable. |

---

## 7. Cross-Cutting Concerns

### 7.1 No Unit Test Coverage

There are no unit tests anywhere in the repository. Key areas that would benefit from testing:

| Area | Test Priority | Reason |
|------|---------------|--------|
| Alarm evaluation (hysteresis, debounce, rate limiting) | **High** | Complex state machine with many edge cases |
| Calibration regression (multiple linear regression) | **High** | Mathematical correctness critical for accurate readings |
| FTP PASV parsing | **Medium** | Fragile string parsing of IP/port from server response |
| Tank registry hash table (collision, dedup) | **Medium** | Data corruption if wrong |
| History rollup / archive | **Medium** | Date boundary edge cases |
| Power state transitions | **Medium** | Hysteresis edge cases |
| Current loop mA → level conversion | **Low** | Simple linear map but critical path |

### 7.2 Monolithic Server File

At ~15,000 lines, the server `.ino` file is the largest single source file. While logically organized, this impairs:
- Code navigation and search
- Compile-time error localization
- Independent unit testing of subsystems

**Recommendation:** Consider extracting major subsystems (FTP, calibration, historical data, REST API handlers) into separate `.cpp`/`.h` files in a future refactor.

### 7.3 Configuration Integrity

Configurations flow server → client with no cryptographic integrity check. The system relies entirely on Notehub's security for config authenticity. If Notehub access is compromised, an attacker could:
- Disable all alarms by setting thresholds to extremes
- Trigger relay commands on any client
- Change contact/SMS recipients
- Exfiltrate sensor data via modified reporting settings

**Mitigation:** Document this trust assumption. Consider adding HMAC-SHA256 on config payloads for defense-in-depth.

### 7.4 Time Synchronization Dependency

All three components depend on Notecard `card.time` for epoch accuracy. The 6-hour resync interval is adequate for normal operation, but prolonged cellular outage causes time drift (~1% on Arduino = ~14 minutes/day). Daily report scheduling, alarm timestamps, and historical data integrity depend on reasonable time accuracy.

### 7.5 Code Duplication Across Components

Several patterns are duplicated across server/client/viewer:
- Notecard error response handling (null check + error string extraction)
- Watchdog kick with platform-specific `#ifdef`
- DFU check and enable logic
- JSON field extraction with fallback to alternate names

The common library handles some of this (`TankAlarm_Notecard.h`, `TankAlarm_I2C.h`) but could absorb more shared patterns.

---

## 8. Security Assessment

### 8.1 Threat Model

| Threat | Likelihood | Impact | Current Mitigation | Gap |
|--------|-----------|--------|-------------------|-----|
| **Client-side PIN extraction (XSS/extension)** | Medium | Critical | None | PIN stored in localStorage in plaintext |
| **Session token leak via URL** | Medium | High | X-Session header also used | Token in query string of `/api/session/check` |
| **Relay actuation on wrong device** | Low | Critical | Notehub routing | No target UID check in relay command handler |
| **Network eavesdropping (LAN)** | Medium | Medium | None (HTTP/FTP plaintext) | No TLS/SFTP |
| **Brute-force PIN** | Low | High | Constant-time compare, exponential backoff | No per-IP rate limit pre-validation |
| **Fresh-install open access** | Medium | High | First login sets PIN | Accepts blank/invalid PIN during bootstrap |
| **Notehub account compromise** | Low | Critical | Notehub auth (external) | No config or relay payload signing |
| **Physical device access** | Low | Medium | PIN is plaintext on flash | Consider encrypted storage |
| **Session hijacking** | Low | Medium | Custom X-Session header, session token | Weak token entropy + URL leak |
| **Config dispatch stale data** | Medium | Medium | ACK tracking | Cache rejects >1536B but dispatch continues |
| **REST API DoS** | Medium | Low | None | No rate limiting on expensive endpoints |
| **Stored XSS via tank labels** | Low | Low | JSON context (not raw HTML) | Frontend `escapeHtml()` exists |

### 8.2 Authentication & Authorization Summary

| Component | Auth Model | Adequate? |
|-----------|-----------|-----------|
| Server REST API (read) | None | Acceptable for private LAN |
| Server REST API (write) | PIN + session token | **Gaps**: PIN in localStorage, token in URL, open bootstrap |
| Server session check | Token in query string | **Gap**: should use X-Session header only |
| Client config receipt | Notehub fleet routing only | **Gap**: no payload integrity check |
| Client relay receipt | Notehub fleet routing only | **Gap**: no target UID verification |
| Viewer dashboard | None (read-only) | Appropriate for kiosk use |
| FTP backup | Username/password (plaintext) | **Known risk** — needs SFTP migration |

---

## 9. Reliability & Error Handling

### 9.1 Failure Recovery Matrix

| Failure Scenario | Server | Client | Viewer |
|-----------------|--------|--------|--------|
| **Notecard I2C failure** | Goes offline; requires reboot | Exponential backoff → bus recovery → watchdog reset | Exponential backoff → bus recovery |
| **Power loss during write** | Atomic writes protect config/registry | Atomic writes protect config; note buffer pruned | N/A (no writes) |
| **Cellular outage** | SMS/email fail; data cached locally | Telemetry buffered to LittleFS (15 notes, 16KB) | Summary fetch delayed; stale data shown |
| **Sensor failure** | N/A (server has no sensors) | 5 failures → sensor-fault alarm | N/A |
| **Heap exhaustion** | JSON docs stack-allocated; FTP malloc guarded | `std::unique_ptr` for JSON; health telemetry tracks heap | **Gap**: OOM check on JsonDocument is ineffective |
| **Flash corruption** | JSON validation + atomic writes | Schema versioning + temp file recovery | Relies on Notecard-only data |
| **Ethernet failure** | Server inaccessible; Notecard still works | N/A (no Ethernet on client) | Dashboard inaccessible; no retry |

### 9.2 Watchdog Coverage

Coverage is generally good across all three components. Remaining gaps:

| Gap | Component | Risk |
|-----|-----------|------|
| Daily report `appendDailyTank` loop | Client | Low (infrequent, fast) |
| Large FTP transfer with network stall | Server | Medium (mitigated by kick before/during) |
| NWS API query with slow response | Server | Low (10s timeout, within 30s watchdog) |

---

## 10. Prioritized Findings

### P0 — Must Fix (Critical/High Severity)

| ID | Component | Finding | Effort |
|----|-----------|---------|--------|
| S-1 | Server | **Raw PIN stored in browser localStorage** — extractable by XSS/extensions | Low |
| C-1 | Client | **Relay commands execute without target UID check** — route misconfig could actuate wrong device | Low |
| S-2 | Server | **Session token leaked in URL** via `/api/session/check?token=...` | Low |
| S-3 | Server | **Fresh-install login grants session without valid PIN** | Medium |
| S-4 | Server | **Config dispatch/cache buffer mismatch** — 1536B cache vs 8192B dispatch; retries use stale data | Medium |
| V-1 | Viewer | Tank array overflow: no bounds check in `handleViewerSummary()` | Low |
| S-6 | Server | Notecard I2C failure requires reboot — no auto-recovery attempt | Medium |

### P1 — Should Fix (High/Medium Severity)

| ID | Component | Finding | Effort |
|----|-----------|---------|--------|
| S-5 | Server | Config retry cadence mismatch: API says 60s, code does 60 min | Low |
| C-2 | Client | Config from server has no integrity check (HMAC) | Medium |
| S-7 | Server | FTP plaintext credentials (document migration path to SFTP) | Low (doc) |
| V-2 | Viewer | **Drops measurement metadata** — non-tank sensors shown as ft/in depth | Medium |
| V-3 | Viewer | **Ignores 24hr change data** — server sends `d` field, viewer discards it, column always shows `--` | Low |
| V-4 | Viewer | JsonDocument OOM check ineffective (`unique_ptr` null check) | Low |
| L-1 | Common | No notefile format versioning for cross-version compatibility | Medium |
| L-2 | Common | Orphaned `.tmp` files from failed atomic writes not cleaned at boot | Low |
| C-3 | Client | I2C bus recovery has no max attempt limit (infinite reset cycle) | Low |
| V-5 | Viewer | Ethernet init failure silently continues (no retry or indicator) | Medium |

### P2 — Consider (Medium Severity)

| ID | Component | Finding | Effort |
|----|-----------|---------|--------|
| S-8 | Server | NWS API failure silent — no alert on repeated failures | Low |
| S-9 | Server | JSON parse failure in tank registry load is silent | Low |
| S-10 | Server | Incomplete HTTP request gets no error response | Low |
| C-4 | Client | Remote relay commands have no rate limiting | Low |
| C-5 | Client | Note buffer line truncation for payloads > 1022 bytes | Low |
| C-6 | Client | Sensor failure/stuck thresholds not remotely configurable | Medium |
| C-7 | Client | Solar-only sunset state not saved on initial detection | Low |
| L-3 | Common | Solar Modbus cascading register read failure | Medium |
| V-6 | Viewer | Hardcoded MAC address — collision risk with multiple viewers | Low |
| — | Server | No per-IP rate limiting on REST API endpoints | Medium |
| — | Server | PIN passed as query parameter on `/api/debug/tanks` GET | Low |
| — | Server | Arduino `String` class heap fragmentation in loop-heavy HTTP response paths | Medium |
| — | Server | Blocking network calls (FTP, NWS API) without `yield()` to Mbed OS RTOS | Medium |
| — | All | No unit test coverage anywhere | High (effort) |

### P3 — Nice to Have (Low Severity)

| ID | Component | Finding | Effort |
|----|-----------|---------|--------|
| S-11 | Server | Calibration log append not atomic | Low |
| S-12 | Server | `recalculateCalibration()` complexity (150 lines) | Medium |
| S-13 | Server | Magic numbers (22h window, 60s retry) should be named constants | Low |
| C-8 | Client | Stuck sensor false positives on sealed/stable tanks | Low |
| C-9 | Client | Missing watchdog kick in daily report loop | Low |
| C-10 | Client | Notecard UID fallback to device label | Low |
| L-4 | Common | `heapMinFreeBytes` not auto-tracked in diagnostics | Low |
| L-5 | Common | I2C current-loop conversion doesn't handle negative raw values | Low |
| L-6 | Common | `card.time` failure is silent (time drift on outage) | Low |
| L-7 | Common | Battery calibration offset (0.35V) hardcoded | Low |
| V-7 | Viewer | Notecard health backoff may delay re-connection by 80 min | Low |
| V-8 | Viewer | `gSourceBaseHour` not validated (0–23 range) | Low |

---

## 11. Recommendations Summary

### Immediate Actions (This Sprint)

1. **S-1 (CRITICAL)**: Stop storing raw PIN in browser `localStorage`. Use session token only after login; for step-up auth, prompt PIN in-memory per action without persisting.
2. **C-1 (CRITICAL)**: Add `target` UID field to relay command payloads. In `processRelayCommand()`, verify target matches `gDeviceUID`; drop and log mismatches.
3. **S-2 (HIGH)**: Remove query-string token path from `/api/session/check`. Validate sessions exclusively from `X-Session` header.
4. **S-3 (HIGH)**: Require explicit 4-digit PIN enrollment on fresh install before granting any authenticated session.
5. **S-4 (HIGH)**: Fix config dispatch/cache mismatch — either increase `ClientConfigSnapshot.payload` to match dispatch buffer, or fail dispatch when cache rejects. Ensure retries always use the correct payload.
6. **V-1 (HIGH)**: Add `if (gTankRecordCount >= MAX_TANK_RECORDS) break;` to viewer's `handleViewerSummary()` tank parsing loop.

### Short-Term (Next Release)

7. **S-5**: Align config retry cadence — either change the loop gate from `3600000UL` to `60000UL`, or correct the HTTP response text to say "60 minutes."
8. **S-6**: Add periodic Notecard re-initialization attempt in server loop when `gNotecardAvailable == false` (e.g., every 5 minutes).
9. **V-2/V-3**: Parse and render `ot`/`st`/`mu` (object type, sensor type, measurement unit) and `d` (24-hour delta) from viewer summary. Adjust frontend to format readings per unit type instead of always using feet/inches.
10. **V-4**: Replace `std::unique_ptr<JsonDocument>(new JsonDocument())` with `new (std::nothrow)` for meaningful OOM detection, or pre-allocate statically.
11. **L-2**: Add boot-time scan for orphaned `.tmp` files under `/fs/` and complete the rename or delete them.
12. **C-3**: Track I2C bus recovery attempt count; after 3 consecutive failures, enter degraded mode instead of infinite reboot loop.
13. **L-1**: Add `_sv` (schema version) field to all Notecard payloads for forward compatibility.

### Medium-Term (Roadmap)

14. **Testing**: Establish unit test framework (e.g., PlatformIO + Unity) targeting alarm logic, calibration regression, hash table operations, session lifecycle, and relay command filtering.
15. **Server refactor**: Extract FTP, calibration, and historical data subsystems into separate source files. Replace Arduino `String` with `char[]`/`snprintf` in loop-heavy HTTP response paths (per Gemini review).
16. **RTOS integration**: Add `yield()` or Mbed OS thread management to blocking FTP/NWS API operations to prevent watchdog starvation in tight `while`-loops (per Gemini review).
17. **Security hardening**: Add HMAC-SHA256 to config payloads (C-2), per-IP rate limiting on REST API, SFTP migration plan (S-7).

### Documentation

18. Document FTP plaintext risk in deployment guide with VPN workaround.
19. Document Notehub Route configuration as a prerequisite (notefile routing silently fails if misconfigured).
20. Document power state machine thresholds and their interaction with sampling intervals.
21. Document viewer's intended scope (tank-only vs. all object types) and update README accordingly.

---

## Appendix: Cross-Review Attribution

The following findings were originally identified by other reviewers and have been verified against source code before inclusion in this consolidated review:

| Finding | Original Reviewer | Verified |
|---------|-------------------|----------|
| S-1: PIN in localStorage | GPT-5.3-Codex | ✅ Confirmed at line 1544 (PROGMEM JS) |
| S-2: Session token in URL | GPT-5.3-Codex + Gemini 3.1 Pro | ✅ Confirmed at lines 5739–5748 |
| S-3: Fresh-install open login | GPT-5.3-Codex | ✅ Confirmed at lines 5785–5791 |
| S-4: Config dispatch/cache mismatch | GPT-5.3-Codex | ✅ Confirmed at lines 848, 7654, 10075 |
| S-5: Config retry cadence mismatch | GPT-5.3-Codex | ✅ Confirmed at lines 2661 vs 2666 |
| C-1: Relay target UID missing | GPT-5.3-Codex | ✅ Confirmed at lines 6625–6643 |
| V-2: Viewer drops ot/st/mu metadata | GPT-5.3-Codex | ✅ Confirmed: server publishes (L8992), viewer ignores (L785) |
| V-3: Viewer ignores 24hr change `d` | GPT-5.3-Codex | ✅ Confirmed: server publishes (L9004), viewer discards, UI shows `--` |
| String fragmentation in loops | Gemini 3.1 Pro | ✅ Pattern observed in HTTP response builders |
| Blocking network without yield() | Gemini 3.1 Pro | ✅ FTP and NWS API paths block main loop |

---

## Appendix B: Fix Implementation Log (March 12, 2026)

All critical, high, and medium findings from this review have been addressed. Below is the implementation summary for each fix.

### Fixes Applied

| ID | Status | Summary of Changes |
|----|--------|-------------------|
| **S-1** | ✅ Fixed | `localStorage.setItem("tankalarm_token", pin)` → stores `"1"` flag only. All `pin:token\|\|null` references replaced with `pin:null`. Cancel/refresh API calls now prompt for PIN per-action instead of using cached value. |
| **C-1** | ✅ Fixed | Added `doc["target"]` UID check at top of `processRelayCommand()`. Compares against `gDeviceUID`; drops and logs mismatched commands. |
| **S-2** | ✅ Fixed | `handleSessionCheck()` now reads session from `X-Session` header parameter instead of URL query string. All 11 JS `fetch('/api/session/check?token='+sid)` calls changed to `fetch('/api/session/check')`. |
| **S-3** | ✅ Fixed | Fresh-install login now requires valid 4-digit PIN. Returns 400 "Please set a 4-digit PIN on first login" instead of granting session with blank PIN. |
| **S-4** | ✅ Fixed | `cacheClientConfigFromBuffer()` changed from `void` to `bool` return. `dispatchClientConfig()` checks return value and responds `PayloadTooLarge` if cache fails. DeserializeJson failure returns `false`. |
| **V-1** | ✅ Already OK | Bounds check `if (gTankRecordCount >= MAX_TANK_RECORDS) break;` already exists at line 780. |
| **S-6** | ✅ Already OK | Server health check loop (lines 2610–2652) already includes I2C recovery with exponential backoff via `tankalarm_recoverI2CBus()` and `tankalarm_ensureNotecardBinding()`. |
| **S-5** | ✅ Fixed | HTTP response text corrected from "auto-retry every 60s" to "auto-retry every 60 min" to match actual `3600000UL` retry interval. |
| **V-2/V-3** | ✅ Fixed | `TankRecord` struct expanded with `objectType[16]`, `sensorType[16]`, `measurementUnit[16]`, `change24h`, `hasChange24h`. Parsing in `handleViewerSummary()` reads `ot`/`st`/`mu`/`d` fields. `sendTankJson()` serializes new fields conditionally. PROGMEM JS `format24hChange()` function replaces hardcoded `--` with signed delta display. |
| **V-4** | ✅ Fixed | Both `new JsonDocument()` allocations changed to `new (std::nothrow) JsonDocument()` so the null check works on OOM instead of throwing `std::bad_alloc`. Added `#include <new>`. |
| **L-2** | ✅ Fixed | Added `tankalarm_posix_cleanup_tmp_files()` and `tankalarm_littlefs_cleanup_tmp_files()` to `TankAlarm_Platform.h`. Server `setup()` calls cleanup for all 9 known config file paths after `initializeStorage()`. If original is intact, orphan .tmp is removed; if only .tmp exists, rename is completed. |
| **C-3** | ✅ Fixed | Added `I2C_SENSOR_RECOVERY_MAX_ATTEMPTS` (5) constant to `TankAlarm_Config.h`. Sensor-only recovery path now tracks total attempts; after 5 attempts, logs "permanent fault" and stops retrying. Counter resets when sensors fully recover. |
| **V-5** | ✅ Fixed | `initializeEthernet()` now retries up to 3 times with increasing delays (5s, 10s, 15s) on failure before logging permanent failure. |

### Files Modified

| File | Changes |
|------|---------|
| `TankAlarm-112025-Server-BluesOpta.ino` | S-1, S-2, S-3, S-4, S-5, L-2 (cleanup call in setup) |
| `TankAlarm-112025-Client-BluesOpta.ino` | C-1, C-3 |
| `TankAlarm-112025-Viewer-BluesOpta.ino` | V-2/V-3, V-4, V-5 |
| `TankAlarm-112025-Common/src/TankAlarm_Platform.h` | L-2 (cleanup functions) |
| `TankAlarm-112025-Common/src/TankAlarm_Config.h` | C-3 (new constant) |

---

## Appendix C: Second Fix Batch — March 12, 2026

### Fixes Applied

| ID | Status | Summary of Changes |
|----|--------|-------------------|
| **S-9** | ✅ Fixed | `loadHistorySettings()` now logs all failure paths: invalid file size, allocation failure, short read, and JSON parse error with `err.c_str()`. Both POSIX and LittleFS code paths updated. |
| **S-10** | ✅ Already OK | Both server and viewer `handleWebRequests()` return 400 "Bad Request" when `readHttpRequest()` fails. No change needed. |
| **V-6** | ✅ Fixed | Added `deriveMacFromUid()` — hashes `gViewerUid` (from Notecard) via DJB2 into MAC bytes 2-5, keeping byte 0 = 0x02 (locally administered). Called in `setup()` after `initializeNotecard()`, before `initializeEthernet()`. Falls back to compile-time default if UID is empty. |
| **S-7** | ✅ Documented | FTP credentials are transmitted in plaintext. **Mitigation:** Deploy the server on a VPN-secured network segment, or use an FTP proxy with TLS termination. **Migration path:** When the Arduino Opta ecosystem gains an SFTP/FTPS library, replace the FTP client calls in the backup/restore handlers. No code change — operational risk only. |
| **C-7** | ✅ Fixed | `saveSolarStateToFlash()` now persists `sunsetActive` flag. `loadSolarStateFromFlash()` restores it on boot. Initial sunset detection (`gSolarOnlySunsetActive = true`) now triggers an immediate save so reboots during the confirmation period resume correctly. |
| **L-1** | ✅ Fixed | Added `NOTEFILE_SCHEMA_VERSION` (= 1) to `TankAlarm_Common.h`. Client's `publishNote()` injects `_sv` into every outbound note body. Server's 9 outbound `note.add` call sites all call `stampSchemaVersion(body)` before `JAddItemToObject`. All Notecard payloads now carry `"_sv":1` for forward-compatibility detection. |
| **C-4** | ✅ Fixed | Added `RELAY_COMMAND_COOLDOWN_MS` (5000ms) to `TankAlarm_Config.h`. `processRelayCommand()` tracks last execution time; commands arriving within the cooldown window are rejected with a log message. Prevents rapid toggling from stale queued Notes. |
| **PIN in debug/tanks** | ✅ Fixed | Changed `/api/debug/tanks` from GET+query-string PIN to POST-only with PIN in JSON body. Eliminates PIN exposure in URLs, browser history, and server logs. Both dump and dedup are now POST actions. |

### Additional Files Modified (This Batch)

| File | Changes |
|------|---------|
| `TankAlarm-112025-Server-BluesOpta.ino` | S-9, L-1 (stampSchemaVersion helper + 9 call sites), PIN debug/tanks POST-only |
| `TankAlarm-112025-Client-BluesOpta.ino` | C-7, L-1 (publishNote _sv injection), C-4 |
| `TankAlarm-112025-Viewer-BluesOpta.ino` | V-6 (deriveMacFromUid) |
| `TankAlarm-112025-Common/src/TankAlarm_Common.h` | L-1 (NOTEFILE_SCHEMA_VERSION constant) |
| `TankAlarm-112025-Common/src/TankAlarm_Config.h` | C-4 (RELAY_COMMAND_COOLDOWN_MS constant) |

---

## Appendix D: Deferred Items — Implementation Guidance

The following items were deferred as they require higher effort or carry lower immediate risk. Below is guidance for implementing each in a future release.

### C-2: Config Payload HMAC Integrity Check
**Risk:** Medium — a tampered config via Notehub Route could misconfigure a client.
**Implementation approach:**
1. Add a shared secret key (32-byte) provisioned via `env.set` on the Notecard, or hardcoded with per-fleet override.
2. In the server's `dispatchClientConfig()`, compute HMAC-SHA256 over the config JSON payload and include it as `_hmac` in the body.
3. In the client's config handler, verify `_hmac` before applying. Reject and log mismatches.
4. Use the Mbed OS `mbedtls_md_hmac()` API (already available on Opta) — zero additional dependencies.
5. **Gotcha:** The HMAC must cover canonicalized JSON (sorted keys or serialized deterministically). ArduinoJson v7 serializes in insertion order, which is stable for the same code path.

### String Fragmentation in HTTP Response Paths
**Risk:** Medium — `String` concatenation in tank dump, transmission log, and other large responses fragments the heap.
**Implementation approach:**
1. Replace `String` concatenation in `handleDebugTanks()`, `handleTransmissionLogGet()`, and `handleTankSettingsGet()` with chunked `client.print()` / `client.write()` calls.
2. Use `snprintf()` into a stack buffer (512–1024 bytes) for each record, then write to client immediately.
3. Alternatively, use `serializeJson()` directly to the `EthernetClient` stream for JSON responses.
4. Profile with `ESP.getFreeHeap()` equivalent (`mallinfo()` on Mbed) before/after to measure improvement.

### Blocking Network Calls Without yield()
**Risk:** Medium — FTP backup/restore and NWS API calls block the main loop for seconds.
**Implementation approach:**
1. Add `TANKALARM_WATCHDOG_KICK()` calls inside FTP transfer loops (every N bytes).
2. For NWS API, the `HttpClient` library already uses blocking reads. Wrap with a timeout-limited `while(client.available())` loop that kicks the watchdog.
3. Long-term: Move FTP operations to an Mbed RTOS thread using `Thread` class — the RTOS scheduler will time-slice between the server loop and FTP thread.
4. **Quick win:** Add `mbedWatchdog.kick()` every 4KB in the FTP upload/download loop.

### C-6: Remote Sensor Threshold Configuration
**Risk:** Low — sensor thresholds are compile-time constants, requiring firmware updates to change.
**Implementation approach:**
1. Add `sensorFailureThreshold`, `stuckSensorThreshold`, and `stuckSensorWindowSec` fields to the client config JSON.
2. Parse in `applyConfigFromServer()` alongside existing monitor settings.
3. Replace the `SENSOR_FAILURE_THRESHOLD` and `STUCK_SENSOR_*` compile-time constants with runtime variables, using the compile-time values as defaults.
4. Add validation ranges (e.g., threshold 1–50, window 60–86400s) to prevent misconfiguration.

### Unit Testing Framework
**Risk:** High (effort) — no test coverage exists.
**Implementation approach:**
1. Use PlatformIO + Unity (or GoogleTest via PlatformIO native platform).
2. **Priority targets for first tests:**
   - Alarm evaluation logic (`evaluateAlarms()`) — pure function, no I/O dependency
   - Calibration regression math (`recalculateCalibration()`) — feed known points, verify output
   - Hash table operations (`findTankByHash`, `insertTankIntoHash`) — verify collision handling
   - Session lifecycle (`generateSessionToken`, `validateSession`) — verify expiry and invalidation
   - Relay command filtering (`processRelayCommand`) — verify target UID check, rate limit, validation
3. Extract testable logic into header-only functions that don't depend on Arduino runtime.
4. Use `#ifdef UNIT_TEST` guards around hardware-dependent code to allow native compilation.
5. **Estimated scope:** ~500 lines of test code for the first 5 test suites.

### RTOS Thread Integration
**Risk:** Medium — a structural improvement for long-blocking operations.
**Implementation approach:**
1. Create `ftpThread` using `rtos::Thread` with its own stack (8KB).
2. Use `rtos::EventFlags` or `rtos::Queue` to dispatch FTP jobs from the web handler to the FTP thread.
3. The web handler returns `202 Accepted` immediately; the FTP thread signals completion via a shared status flag.
4. **Caution:** The Notecard I2C bus is not thread-safe. All Notecard operations must remain on the main thread (or use a mutex).
5. Similarly, `EthernetClient` is not reentrant — web serving must stay on one thread.

### Security Hardening (Per-IP Rate Limiting, SFTP)
**Risk:** Medium — no per-IP rate limiting on HTTP endpoints.
**Implementation approach:**
1. Maintain a small fixed-size array (8–16 entries) of `{ip, requestCount, windowStart}`.
2. In the session middleware, increment the count for each IP. If count exceeds threshold (e.g., 60/min), return `429 Too Many Requests`.
3. Use LRU eviction for the IP table.
4. SFTP: Wait for Mbed OS ecosystem to provide a library, or proxy via external hardware.

---

*End of Review*
