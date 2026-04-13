# Logic Review — April 13, 2026

> **Version Reviewed:** 1.6.2 (post June 9, 2026 bugfix pass)  
> **Reviewer:** GitHub Copilot (Claude Opus 4.6)  
> **Scope:** End-to-end logic across Client, Server, Viewer — alarm flow, relay control, configuration lifecycle, power management, data pipeline integrity  
> **Method:** Trace-based analysis following data from sensor → Client → Notecard → Server → SMS/Email/Dashboard/Viewer

---

## Table of Contents

- [Summary](#summary)
- [Alarm Pipeline Logic](#alarm-pipeline-logic)
- [Relay Control Logic](#relay-control-logic)
- [Configuration Lifecycle](#configuration-lifecycle)
- [Power State Machine](#power-state-machine)
- [Data Pipeline Integrity](#data-pipeline-integrity)
- [Viewer Data Flow](#viewer-data-flow)
- [FTP / Archival Logic](#ftp--archival-logic)
- [Authentication & Session Logic](#authentication--session-logic)
- [Recommendations](#recommendations)

---

## Summary

| Category | Findings | Critical | High | Moderate |
|----------|----------|----------|------|----------|
| Alarm Pipeline | 4 | 0 | 1 | 3 |
| Relay Control | 3 | 1 | 1 | 1 |
| Config Lifecycle | 2 | 0 | 0 | 2 |
| Power State Machine | 2 | 0 | 1 | 1 |
| Data Pipeline | 3 | 1 | 1 | 1 |
| Viewer Data Flow | 2 | 0 | 0 | 2 |
| FTP / Archival | 2 | 0 | 1 | 1 |
| Authentication | 2 | 1 | 1 | 0 |
| **Total** | **20** | **3** | **6** | **11** |

---

## Alarm Pipeline Logic

### LR-1: Alarm Debounce ↔ Rate Limit Interaction Gap **(C)** — HIGH

**Scenario:** A tank level hovers near the HIGH threshold, crossing it every other sample.

**Trace:**
1. Sample 1: level = 100.1" (above HIGH=100.0") → `highAlarmDebounceCount = 1`
2. Sample 2: level = 99.9" (below HIGH) → `highAlarmDebounceCount = 0` (reset)
3. Sample 3: level = 100.1" → `highAlarmDebounceCount = 1`
4. ...

**Outcome:** Alarm never latches (debounce requires 3 consecutive readings). This is correct behavior — debounce filters noise.

**BUT:** If the hysteresis band is too narrow (e.g., `hysteresisValue = 0.01`), then once the alarm latches after 3 stable readings, the clear threshold is `highClear = 100.0 - 0.01 = 99.99`. A reading of 99.99 clears the alarm. The next reading at 100.01 re-triggers the 3-sample debounce, and the alarm re-latches at 100.01. The rate limiter then suppresses the second alarm for 5 minutes.

**Net result:** The server sees: `HIGH alarm` → `clear` (5 min later) → `HIGH alarm` (another 5 min later) → rapid oscillation alarm/clear pairs, but now SMS rate-limited to 10/hour. The operator sees a flood of emails (no per-type email rate limit documented).

**Fix:** Document minimum hysteresis value recommendations in the config generator UI. Warn if `hysteresisValue < 1.0` for analog sensors.

---

### LR-2: Sensor Recovery Debounce Resets on Any Failure **(C)** — MODERATE

**File:** Client `validateSensorReading()`

**Scenario:** A flaky I2C sensor alternates between valid and invalid readings:
1. Valid → `recoveryCount = 1`
2. Invalid → `recoveryCount = 0` (reset)
3. Valid → `recoveryCount = 1`
4. ...

**Outcome:** Sensor never recovers (needs 3 consecutive valid readings). This is correct — it prevents premature recovery declarations. However, if the sensor failure is intermittent (50% success rate), the sensor stays permanently in "failed" state even though it provides good data half the time.

**Recommendation:** Consider a windowed approach: 3 valid out of any 5 consecutive readings = recovered. Or implement exponential backoff on the recovery count (first recovery needs 3, second needs 6, etc.) to prevent oscillation.

---

### LR-3: Clear Events Rate-Limited May Delay SMS Recovery Notice **(C)(S)** — MODERATE

**Trace:**
1. Client detects HIGH alarm → sends alarm note with `se: true`
2. Server receives alarm → sends SMS "HIGH ALARM: Tank 1 at 100.5 inches"
3. Tank drops below clear threshold within 5 minutes
4. Client sends "clear" alarm note
5. Server receives clear → checks SMS rate limit → suppressed (within MIN_SMS_ALERT_INTERVAL_SECONDS)
6. Operator receives HIGH alarm SMS but never receives recovery SMS

**Impact:** Operator sees alarm, drives to site, finds tank already normal. Wasted trip.

**Fix:** Consider exempting "clear" / recovery SMS from per-type rate limiting. A recovery is time-critical information, and the cost of one extra SMS is justified.

---

### LR-4: Stale Battery Data in Solar Alarm Reports **(C)** — MODERATE

**Scenario:** Solar Modbus communication fails. `SolarManager::poll()` returns `false`. The `SolarData` struct retains the last successful read values.

**Trace:**
1. Battery voltage was 12.8V at last successful Modbus read (2 hours ago)
2. Modbus now fails → `communicationOk = false`, `consecutiveErrors++`
3. After threshold → sends `SOLAR_ALERT_COMM_FAILURE` alarm
4. Alarm payload includes `state.batteryVoltage` = 12.8V (stale)
5. Operator sees "COMM FAILURE: battery at 12.8V" — thinks battery is fine
6. Actual battery may be at 10V (below CRITICAL)

**Fix:** When reporting a communication failure alarm, flag the battery voltage as stale (e.g., prepend "~" or add a `"stale": true` field in the alarm payload). The Server can then display "(stale)" in the SMS/email.

---

## Relay Control Logic

### LR-5: UNTIL_CLEAR Relay After Reboot — No Re-Latch **(C)** — CRITICAL

**Scenario:** Tank HIGH alarm triggers pump relay in UNTIL_CLEAR mode. Device reboots.

**Trace:**
1. HIGH alarm latched → relay activated (UNTIL_CLEAR mode)
2. Device reboots (brown-out / watchdog / DFU)
3. `gRelayRuntime` zeroed on boot → all relays OFF
4. `gMonitorState` zeroed on boot → `highAlarmLatched = false`
5. First sensor read: level still above threshold
6. `evaluateAlarms()` → `highAlarmDebounceCount = 1`
7. Sample 2: `highAlarmDebounceCount = 2`
8. Sample 3: `highAlarmDebounceCount = 3` → alarm latches → relay re-activates
9. If sample interval = 30 min (ECO mode): relay is OFF for **90 minutes** post-reboot

**Impact:** Tank overflows for 90 minutes because the pump stopped during reboot and relay re-activation requires 3 debounce cycles.

**Existing mitigation (v1.6.1):** Relay state IS restored when exiting CRITICAL_HIBERNATE if alarms are still latched (lines 6064–6075). But this only covers the CRITICAL→ECO transition, not a full reboot.

**Fix (two-part):**
1. **Short-term:** On boot, if a sensor reads above the HIGH threshold on the FIRST sample, skip debounce and immediately latch the alarm + activate the relay. This is a "known-bad state on boot" fast-path.
2. **Long-term:** Persist relay state to flash (as noted in CR-H5).

---

### LR-6: Remote Relay Command Delivery Not Confirmed **(C)(S)** — HIGH

**Trace:**
1. Client A detects alarm → sends relay command for Client B via `relay_forward.qo`
2. Notecard queues note → syncs to cloud (1–3 min)
3. Cloud routes note to Server
4. Server receives → validates → forwards to Client B via `relay_command.qo` (another 1–3 min)
5. Client B receives → activates relay

**Total roundtrip: 2–6 minutes.** No acknowledgement at any step.

**Failure modes:**
- If step 2 fails (cell connectivity), Client A believes relay was triggered
- If step 4 fails (Server offline), no retry by the cloud
- If step 5 fails (Client B offline), relay never activates

**Impact:** Operator believes remote relay is active; it isn't. No alerting.

**Fix (already partially tracked as M-17):** At minimum, log when a remote relay command is sent but no ACK received within 10 minutes. Consider a relay_ack notefile from Client B → Server → Client A.

---

### LR-7: Relay Momentary Timeout Precision During Long Operations **(C)** — MODERATE

**Scenario:** Relay has 60-second momentary timeout. A telemetry note publish takes 5–10 seconds (Notecard I2C latency).

**Trace:**
1. Relay activated at T=0, timeout = 60s
2. At T=55, `loop()` calls `publishNote()` for telemetry
3. `publishNote()` blocks for 8 seconds (Notecard I2C)
4. At T=63, `loop()` resumes, calls `checkRelayMomentaryTimeout()`
5. Relay deactivated at T=63 instead of T=60

**Impact:** 3-second overshoot. Not critical for most applications, but for chemical dosing pumps, 3 seconds of extra flow may matter.

**Recommendation:** Document that relay timeout precision is ±10 seconds due to single-threaded loop. If sub-second precision is needed, recommend a hardware timer ISR.

---

## Configuration Lifecycle

### LR-8: Config Dispatch Without ACK Timeout **(S)** — MODERATE

**Trace:**
1. Server dispatches config to Client C via Notecard
2. `pendingDispatch = true`, `dispatchAttempts = 1`
3. Client C is offline (no cell coverage for 2 weeks)
4. Server's `pendingDispatch` flag remains `true` indefinitely
5. Operator changes config again → Server updates config and re-dispatches
6. Client C comes online → receives **second** config, applies it → sends ACK
7. Server clears `pendingDispatch` based on ACK

**Issue:** If the Client only receives the second config (first was superseded), the first dispatch attempt is wasted but the outcome is correct. However, the Server's dispatch status shows "pending" for 2 weeks, confusing the operator.

**Fix:** Add a `dispatchTimeout` (e.g., 72 hours). After timeout, mark dispatch as "timed out" (not failed, not succeeded). Operator can then manually re-dispatch.

---

### LR-9: Rapid Config Changes Coalesce Without Audit Trail **(S)** — MODERATE

**Trace:**
1. Operator changes HIGH alarm threshold from 100" to 110" → `gConfigDirty = true`
2. Before save, operator changes LOW alarm threshold from 20" to 30" → `gConfigDirty = true`
3. `persistConfig()` runs → both changes saved atomically
4. Config version bumped once

**Issue:** No audit log of which settings were changed. If a misconfiguration causes a missed alarm, forensic analysis cannot determine which specific change was responsible.

**Recommendation:** Log each setting change with old and new values to the serial log. Not a code bug, but a forensic gap.

---

## Power State Machine

### LR-10: CRITICAL_HIBERNATE Relay De-Energize Without Warning **(C)** — HIGH

**File:** Client lines 6064–6075

**Trace:**
1. Device is in NORMAL power state, relay active (pump running)
2. Battery voltage drops below CRITICAL threshold (11.8V)
3. Debounce counter reaches threshold → state transitions to CRITICAL_HIBERNATE
4. All relays immediately de-energized (lines 6064–6067)
5. No SMS/note sent before de-energize (Notecard may not have time)

**Impact:** If a pump is controlling tank overflow, it stops immediately. The server is NOT notified about the relay deactivation — only about the power state change (if the telemetry note queued before shutdown).

**Fix:**
1. Send a "relay_deactivated:power_critical" alarm BEFORE de-energizing relays
2. Queue the note with `sync:true` and a brief delay (100ms) before cutting relay power
3. On the Server side, display power-critical relay deactivation as a distinct alarm type

---

### LR-11: Power State Debounce Counter Not Time-Based **(C)** — MODERATE

**File:** Client `updatePowerState()`

**Issue:** Power state transitions use a loop-count debounce (`POWER_STATE_DEBOUNCE_COUNT`). In NORMAL mode (100ms sleep), 5 debounce counts = 500ms. In ECO mode (30-min sleep), 5 counts = 2.5 hours. The debounce behavior varies dramatically with power state.

This is the same class of issue as M-4 (I2C thresholds loop-count-based).

**Fix:** Use elapsed-time debounce: require voltage below threshold for N seconds, not N loop iterations.

---

## Data Pipeline Integrity

### LR-12: processNotefile() Consumes Notes on Parse Failure **(S)** — CRITICAL

*(Same as CR-C2 in Code Review — included here for logic review completeness)*

**Trace:**
1. Client sends telemetry note with 50 sensor readings (large JSON)
2. Server receives note → `JConvertToJSONString()` succeeds 
3. `deserializeJson()` fails (ArduinoJson heap fragmentation after 10 hours uptime)
4. Handler NOT called — telemetry NOT recorded
5. Note consumed (deleted from Notecard)
6. Telemetry data from that cycle is permanently lost
7. No error visible to operator (only Serial log)

**Impact:** Under heap pressure, the Server silently drops data without alerting anyone.

**Fix:** Do not delete notes on parse failure. Implement poison note counter per file (as the Viewer does). After 3 consecutive parse failures for the same note, delete it and log a warning.

---

### LR-13: Sensor Registry Growth Not Bounded by Active Sensors **(S)** — HIGH

**File:** Server `upsertSensorRecord()`

**Trace:**
1. Client A registers 8 sensors
2. Client A removed from fleet
3. Sensors enter stale detection → pruned after 7 days
4. Client B registers 8 sensors with same monitor indices
5. Over time, sensor UIDs accumulate (different Client UIDs with same indices)
6. Hash table load factor increases → linear probing degrades to O(n)
7. At 50%+ load factor, each telemetry poll iteration becomes slow

**Issue:** The `MAX_SENSOR_RECORDS = 64` limit is hard-capped. The hash table rebuild on collision is expensive but bounded. The real issue is that stale sensor pruning (7-day timeout) can be defeated by intermittent connectivity: a Client that connects once every 6 days never gets pruned but rarely contributes useful data.

**Recommendation:** Add a minimum-activity threshold: if a sensor provides < 3 readings in 7 days, mark it as "semi-stale" and deprioritize in the hash table.

---

### LR-14: Daily Summary Deduplication Relies on Epoch Comparison **(S)** — MODERATE

**Issue:** If two daily summary notes arrive with slightly different epochs (e.g., 1 second apart due to clock drift), the deduplication logic treats them as distinct events. The same day's data may be counted twice.

**Recommendation:** Deduplication should compare the date (year/month/day) not the exact epoch.

---

## Viewer Data Flow

### LR-15: Viewer Summary Fetch Misaligned After Time Jump **(V)** — MODERATE

**Trace:**
1. Viewer boots at T=0, NTP not yet synced → `currentEpoch()` returns 0
2. `ensureTimeSync()` called → syncs to real time (e.g., epoch = 1712000000)
3. `scheduleNextSummaryFetch()` computes next aligned fetch (e.g., 6:00 AM UTC)
4. 2 hours later, Notecard resync corrects time by -30 seconds (clock was fast)
5. `gNextSummaryFetchEpoch` is NOT recalculated
6. Fetch fires 30 seconds late — minor and acceptable

**But:** If the time correction is large (e.g., +24 hours from a timezone fix), the scheduled fetch could be 24 hours in the past. The Viewer would immediately fetch, then schedule the NEXT fetch 24 hours later (correct).

**Impact:** Minimal. One extra fetch at a wrong time after a large time correction.

**Recommendation:** After each time sync, validate that `gNextSummaryFetchEpoch` is still in the future. If not, reschedule.

---

### LR-16: Viewer Poison Note Counter Not Persisted **(V)** — MODERATE

**File:** Viewer `fetchViewerSummary()`

**Issue:** If a malformed note is stuck in `viewer_summary.qi`, the Viewer increments a RAM-based failure counter. After 3 failures, it deletes the note. But if the Viewer reboots (watchdog) between failure 2 and 3, the counter resets to 0, and the poison note survives indefinitely — causing an infinite boot→fail→reboot loop.

**Fix:** Persist the poison note failure count to a file, or delete after the first parse failure (since the note won't magically become valid).

---

## FTP / Archival Logic

### LR-17: FTP PASV Parser Corrupted by Status Line Digits **(S)** — HIGH

*(Same root cause as CR-H2 — analyzed here from a logic perspective)*

**Trace:**
1. Server sends PASV command
2. FTP server responds: `227 Entering Passive Mode (192,168,1,50,4,1)`
3. Parser starts scanning from `msg[0]` which is `'2'` in "227"
4. `parts[0]` accumulates: `2`, `22`, `227`
5. Space after "227" → `idx++` → `parts[0] = 227`, move to `parts[1]`
6. At `'E'` in "Entering" → no digit, no comma → skip
7. At `'(' → no digit, no comma → skip
8. At `'1'` in "192" → `parts[1]` starts accumulating: `1`, `19`, `192`
9. At `','` → `idx++` → `parts[1] = 192`, move to `parts[2]`
10. ... continue parsing correctly for parts[2..5]

**Actual outcome:** `parts[0] = 227, parts[1] = 192, parts[2] = 168, parts[3] = 1, parts[4] = 50, parts[5] = 4`  
But `idx` at the end is 6 (or 7 with trailing `)`) so the check `idx < 6` passes.

**IP assembled:** `IPAddress(227, 192, 168, 1)` — WRONG  
**Port assembled:** `(50 << 8) | 4 = 12804` — also shifted

**Impact:** FTP data connection attempts to wrong IP/port. FTP backup/restore silently fails.

**Fix:** Start parsing from after the `(` character. See CR-H2 for code fix.

---

### LR-18: FTP Archive Manifest Can Lose Entry on Write Failure **(S)** — MODERATE

**Trace:**
1. Server archives Client X → adds entry to `archived_clients.json`
2. Atomic write: write-to-temp → rename
3. If rename fails (LittleFS full, filesystem error):
   - `.tmp` file exists with new entry
   - Original `archived_clients.json` unchanged
   - On next boot, `.tmp` cleanup finds the orphan → renames it to `.json`
   - Original entries from `.json` are lost, replaced by `.tmp` (which only has the latest state)

Wait — actually the atomic write writes the FULL manifest to .tmp, then renames. So the .tmp should have all entries. If rename fails, the old file survives. On boot, the .tmp (with the new entry) is recovered. This is correct.

**Revised assessment:** The atomic write pattern is sound. **No issue.** The boot-time recovery (`tankalarm_posix_cleanup_tmp_files()`) correctly handles this scenario.

---

## Authentication & Session Logic

### LR-19: Login Open Redirect — Phishing Vector **(S)** — CRITICAL

*(Same as CR-C1 — analyzed here for logic flow)*

**Trace:**
1. Attacker sends link to operator: `https://192.168.1.10/login?redirect=https://evil.com/dashboard`
2. Operator clicks link → sees legitimate TankAlarm login page served from the real server
3. Operator enters PIN → POST `/api/login` succeeds → session token stored in localStorage
4. `window.location.href = params.get("redirect")` → browser navigates to `https://evil.com/dashboard`
5. Attacker's page reads `document.referrer` (contains the server IP)
6. Attacker's page mimics dashboard, shows "session expired, please re-enter PIN"
7. Operator re-enters PIN on attacker's page → credential captured

**Additional concern:** All embedded HTML pages redirect to `/login?redirect=...` when no session exists. This pattern is repeated in ~12 pages, meaning ANY page's URL can be crafted with the redirect parameter.

**Fix:** Sanitize redirect to only allow relative paths starting with `/` and not containing `//` or `://`. Apply in the login page JavaScript.

---

### LR-20: Session Stored in localStorage — XSS-Accessible **(S)** — HIGH

**File:** All embedded HTML pages

**Code pattern:**
```javascript
localStorage.setItem("tankalarm_session", data.session);
```

**Issue:** `localStorage` is accessible to any JavaScript running on the same origin. If any XSS vulnerability exists (e.g., user-controlled data rendered without escaping in the dashboard), an attacker can read `localStorage.getItem('tankalarm_session')` and hijack the session.

**Mitigations already in place:**
- Only one session allowed at a time (single-session enforcement)
- `X-Content-Type-Options: nosniff` and `X-Frame-Options: DENY` headers
- Session token sent via `X-Session` header (not cookies — immune to CSRF)

**Residual risk:** If there's any reflected XSS in the Server's web responses (e.g., client site names rendered unsanitized in HTML), session theft is possible.

**Recommendation:**
1. Audit all templates for XSS: ensure all user-controlled data (site names, client UIDs, alarm messages) is HTML-escaped before rendering
2. Consider using `httpOnly` cookies instead of localStorage for session storage (requires CORS changes)
3. Add `Content-Security-Policy: default-src 'self'` header to prevent inline script injection

---

## Recommendations

### Priority 1 — Fix Immediately
1. **LR-19 / CR-C1:** Open redirect in login — sanitize redirect parameter
2. **LR-12 / CR-C2:** processNotefile() — don't delete notes on parse failure
3. **LR-5:** UNTIL_CLEAR relay re-latch on boot — fast-path first sample

### Priority 2 — Fix Soon
4. **LR-17 / CR-H2:** FTP PASV parser — skip to `(` before parsing octets
5. **LR-10:** Power-critical relay deactivation — send alarm before cutting power
6. **LR-6:** Remote relay delivery — add timeout/warning for missing ACK
7. **LR-20:** XSS audit of all user-controlled data in web templates

### Priority 3 — Improvement
8. **LR-1:** Document minimum hysteresis recommendations
9. **LR-3:** Exempt clear/recovery SMS from per-type rate limiting
10. **LR-8:** Config dispatch timeout (72 hours)
11. **LR-15:** Reschedule Viewer summary fetch after time correction
12. **LR-16:** Viewer poison note — delete after first failure (or persist counter)
