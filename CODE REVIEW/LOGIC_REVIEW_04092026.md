# Logic Review — April 9, 2026

> **Firmware Version:** 1.6.1  
> **Reviewer:** GitHub Copilot (Claude Opus 4.6)  
> **Scope:** End-to-end data flow, state machines, alarm/relay/power logic across Client, Server, and Viewer  

---

## 1. System Architecture Logic

### 1.1 Data Flow Summary

```
Client (sensors, relays)  --->  Blues Notecard  --->  Notehub Routes
    ^                                                       |
    |  (config push via notefiles)                         v
    +--- Server <--- Notehub <--- Server pushes .qo files
                  |
                  v
              Viewer (read-only kiosk, Notecard-bridged)
```

**Observation:** The architecture correctly separates concerns. The Client never directly communicates with the Server — all messages traverse Notehub via notefiles. This provides store-and-forward reliability (messages survive temporary cellular outages), but introduces latency (typically 15-60 seconds per hop).

### 1.2 Notefile Routing

| Notefile | Direction | Purpose |
|----------|-----------|---------|
| `telemetry.qo` | Client → Server | Sensor readings, periodic heartbeats |
| `alarm.qo` | Client → Server | Alarm events (high/low/clear/fault/power) |
| `unload.qo` | Client → Server | Unload delivery tracking events |
| `config.qi` | Server → Client | Remote configuration pushes |
| `relay_cmd.qi` | Server → Client | Relay ON/OFF commands (cross-device relay control) |
| `config_ack.qo` | Client → Server | Configuration acknowledgement |
| `viewer_summary.qi` | Server → Viewer | Dashboard summary snapshots |

**Verdict:** Correct. Each notefile has a clear ownership model. The `.qo` (outbound) / `.qi` (inbound) naming convention is properly followed. No notefile is shared between conflicting operations.

---

## 2. Client Logic Analysis

### 2.1 Power State Machine

The Client implements a 4-state power conservation machine:

```
NORMAL --> ECO --> LOW_POWER --> CRITICAL_HIBERNATE
                                        |
  <-- step-by-step recovery <-----------+
```

**State Transitions:**

| Current State | Enter Next (Falling) | Exit (Rising) |
|---------------|---------------------|---------------|
| NORMAL | V < ecoEnter → ECO | — |
| ECO | V < lowEnter → LOW_POWER | V >= ecoExit → NORMAL |
| LOW_POWER | V < criticalEnter → CRITICAL | V >= lowExit → ECO |
| CRITICAL_HIBERNATE | — | V >= criticalExit → LOW_POWER |

**Logic Review Findings:**

1. **Correct: Step-by-step recovery.** Recovery always goes one step up (CRITICAL→LOW_POWER→ECO→NORMAL). This prevents oscillation where a battery that briefly exceeds criticalExit but remains below ecoExit would bounce between CRITICAL and NORMAL. **SOUND**

2. **Correct: Hysteresis gap.** Enter thresholds are lower than exit thresholds. For example, if `ecoEnter=12.0V` and `ecoExit=12.5V`, a battery at 12.2V stays in ECO without bouncing. **SOUND**

3. **Correct: Debounce counter.** `POWER_STATE_DEBOUNCE_COUNT` consecutive readings must agree before transitioning. Resets to 0 if any reading disagrees. This eliminates single-sample noise spikes. **SOUND**

4. **Correct: Relay safety.** On entering CRITICAL, all relays are de-energized immediately (GPIO LOW). On recovery from CRITICAL, only UNTIL_CLEAR and MANUAL_RESET relays with latched alarms are restored. MOMENTARY relays are correctly skipped (their timeout expired during hibernate). **SOUND**

5. **Correct: Battery failure fallback.** Persistent CRITICAL states trigger solar-only mode after `batteryFailureThreshold` consecutive CRITICAL entries. A 24-hour decay timer prevents false activation from intermittent dips. **SOUND**

6. **Minor logic note:** When starting from NORMAL with a very low voltage (below `criticalEnter`), the state machine jumps directly to CRITICAL_HIBERNATE, bypassing ECO and LOW_POWER. This is intentionally correct — if the battery is already critical at boot, going through intermediate states would waste power on transitions and SMS notifications. However, the recovery path still goes step-by-step.

### 2.2 Alarm Evaluation Logic

The `evaluateAlarms()` function handles two distinct paths:

#### Path A: Digital Sensors (Float Switches)

```
digitalTrigger config  -->  shouldAlarm (bool)  -->  debounce counter
                                                         --> sendAlarm("triggered" | "not_triggered" | "clear")
```

**Logic Review:**
- Correctly supports both `"activated"` and `"not_activated"` triggers via `digitalTrigger` field.
- Falls back to legacy `highAlarm/lowAlarm` thresholds when `digitalTrigger` is empty.
- Debounce uses `ALARM_DEBOUNCE_COUNT` for both alarm entry and clear. **SOUND**
- The `highAlarmLatched` flag is reused for digital sensors (regardless of high/low direction). This is a naming quirk but functionally correct since digital sensors have only one alarm state.

#### Path B: Analog/Current Loop Sensors

```
reading >= highTrigger  -->  debounce  -->  sendAlarm("high")
reading < highClear     -->  debounce  -->  sendAlarm("clear")
reading <= lowTrigger   -->  debounce  -->  sendAlarm("low")
reading > lowClear      -->  debounce  -->  sendAlarm("clear")
```

**Logic Review:**
- `clearCondition` requires BOTH `< highClear` AND `> lowClear`. This means a reading between the low and high hysteresis bands is needed to clear an alarm. **SOUND** — prevents clearing on a hysteresis edge.
- **Minor issue:** If hysteresisValue is 0, `highClear == highTrigger` and `lowClear == lowTrigger`, making `clearCondition` equivalent to `< highTrigger && > lowTrigger`. This works but means the alarm clears as soon as the reading drops below the exact trigger point. Operators should always set a non-zero hysteresis to avoid flapping.
- When a high alarm latches, `lowAlarmLatched` is explicitly set to `false`, and vice versa. This prevents simultaneous high+low alarms on the same sensor. **SOUND**
- Debounce counters for the opposing direction are reset when a condition is active (e.g., `lowAlarmDebounceCount = 0` when high is detected). This prevents slow-accumulation bugs. **SOUND**

**LR-1 (FINDING): Clear counters for non-active state are not reset.** When a reading is in the normal range (not high, not low), both `highAlarmDebounceCount` and `lowAlarmDebounceCount` are set to 0. But `highClearDebounceCount` is only reset when highCondition is true (`if (highCondition) { state.highClearDebounceCount = 0; }`). If the reading oscillates between "slightly below highTrigger" and "back in the clear band" without staying consistently in either zone for `ALARM_DEBOUNCE_COUNT` cycles, the clear debounce can accumulate over many oscillations. This is a minor issue since `ALARM_DEBOUNCE_COUNT` is typically 3, and real sensors don't oscillate that precisely.

### 2.3 Relay Safety & Control Logic

The relay system has three operating modes:

| Mode | Behavior | Safety Timeout | Clear Behavior |
|------|----------|---------------|----------------|
| MOMENTARY (30 min) | Activates for 30 minutes | ✅ (30 min cap) | Auto-clears on timeout |
| UNTIL_CLEAR | Active while alarm latched | ✅ (`relayMaxOnMinutes`) | Clears when alarm clears |
| MANUAL_RESET | Active until operator clears | ✅ (`relayMaxOnMinutes`) | Requires web UI / SMS reset |

**Key Logic Paths Verified:**

1. **Relay activation executes BEFORE rate limiting** (line ~4808, bugfix CRITICAL-2). This is safety-critical — even if the alarm notification is rate-limited, the relay still actuates. **SOUND**

2. **`relay_timeout` does NOT re-activate relays** (line ~4811). When a relay safety timeout fires, the alarm notification carries type `relay_timeout`, and the relay activation path explicitly skips it via `isRelayTimeout`. **SOUND**

3. **UNTIL_CLEAR deactivation** (line ~4830): When `alarmType == "clear"`, the code checks `isMonitorRelayActive(idx)` and `relayMode == RELAY_MODE_UNTIL_CLEAR` to deactivate. This correctly handles the case where the alarm clearing threshold differs from the trigger threshold (hysteresis). **SOUND**

4. **Cross-device relay forwarding:** `triggerRemoteRelays()` sends a `relay_cmd.qo` note to a different Client device. The receiving Client processes it in `handleRelayCommand()`. If the Notecard I2C fails during transmission, the note is queued but not retried — the relay may not actuate on the remote device. This is a known limitation of the store-and-forward architecture.

**LR-2 (FINDING): Remote relay deactivation on alarm clear sends the original `relayMask` bits.** However, at line ~4838, `getMonitorActiveRelayMask(idx)` is used instead, which returns only the bits that are actually active for this monitor. If a relay was already deactivated by a safety timeout before the alarm cleared, this correctly avoids sending a redundant OFF command. **SOUND**

### 2.4 Stuck Sensor Detection

```
|current - lastValid| < 0.05 inches  -->  stuckReadingCount++
stuckReadingCount >= STUCK_THRESHOLD  -->  sensorFailed = true, send "sensor-stuck"
```

**Logic Review:**
- Uses a hard-coded tolerance of 0.05 inches. For low-resolution sensors (e.g., 0-10V into a tank with 200-inch range, ~0.5 inch resolution), this might trigger false positives.
- The `stuckReadingCount` is only reset when `|current - lastValid| >= 0.05`. A sensor that reads 50.02, 50.03, 50.02, 50.04 (all within ±0.02 of each other but > 0.05 from the first) would still accumulate. Actually, the comparison is against `lastValidReading`, which is updated each cycle — so consecutive readings within 0.05 of each other accumulate. **SOUND behavior for truly stuck sensors, but can false-positive on slow-fill tanks.**

**LR-3 (see also CR-M1):** Stuck detection and unload tracking can conflict — documented in code review as CR-M1.

### 2.5 Rate Limiting Logic (Client Side)

The `checkAlarmRateLimit()` function implements three layers:

1. **Per-alarm-type interval** (`lastHighAlarmMillis`, etc.) — 5-minute minimum between same-type alarms.
2. **Per-monitor hourly count** (`alarmTimestamps[]`) — max 3 per hour per monitor.
3. **Global hourly count** (`gGlobalAlarmTimestamps[]`) — max 10 per hour across all monitors.

**Logic Review:**
- Layer 1 is `millis()`-based and handles wraparound correctly (unsigned subtraction). **SOUND** (except first-boot edge case per CR-M2).
- Layers 2 and 3 both suffer from the unsigned underflow bug documented in CR-H1. During the first hour after boot, the hourly windows are effectively disabled.
- `relay_timeout` shares the `sensor-fault` rate bucket (documented in CR-H2).
- `"clear"` alarms are rate-limited separately from `"high"` and `"low"`. This is important — a rapidly toggling sensor can generate as many clears as alarms, and limiting them independently prevents clear-alarm ping-pong from exhausting the global budget.

---

## 3. Server Logic Analysis

### 3.1 Sensor Registry & Hash Table

The Server uses a djb2 hash table for O(1) sensor lookups:

```
hashIndex = djb2(clientUid + sensorIndex) % MAX_SENSOR_RECORDS
```

**Logic Review:**
- Linear probing handles collisions correctly — the probe wraps around the array.
- `upsertSensorRecord()` validates UIDs via `isValidClientUid()` before insertion. **SOUND**
- `deduplicateSensorRecordsLinear()` runs periodically as a safety net, removing any duplicates that might appear from hash table corruption or config migration. **SOUND (defense-in-depth)**

### 3.2 Alarm Processing Pipeline (Server)

```
Client alarm note  -->  processNotefile()  -->  handleAlarm()
                                                    |
                                    +---------------+--------------+
                                    |               |              |
                              System alarm    Sensor alarm    Relay timeout
                                    |               |              |
                              Direct SMS       Store state     Store event
                              (no rate limit)    + rate-limited SMS
```

**LR-4 (FINDING):** System alarms (solar/battery/power) bypass the `checkSmsRateLimit()` function entirely — they call `sendSmsAlert()` directly when `se=true`. This is documented in CR-H3 as a code issue. From a **logic** perspective, this is especially risky for power state alarms: if a battery oscillates near a threshold, the Client's power state debounce (3 consecutive readings) triggers a transition, which sends a power alarm with `se=true`, which triggers an immediate SMS on the Server. If the voltage then recovers and drops again rapidly, multiple SMS are sent.

**Mitigation already present:** The Client's debounce means at minimum 3 loop iterations (~90-180 seconds depending on power state sleep interval) must pass before a state transition. This naturally limits the rate to ~1 SMS per 2-3 minutes, which is aggressive but not catastrophic.

### 3.3 Telemetry-to-Calibration Pipeline

```
handleTelemetry()  -->  recordTelemetrySnapshot()  -->  updateCalibrationFromTelemetry()
                                                              |
                                                    Regress mA vs known-good level
                                                              |
                                                    Store calibration coefficients
```

**Logic Review:**
- Calibration uses linear regression on mA-to-level pairs. The regression requires `R² >= 0.95` before updating. This prevents noisy data from corrupting calibration. **SOUND**
- Temperature compensation is applied when temperature data is available from weather integration or Notecard internal sensor.
- Calibration data persists to LittleFS and survives reboots.

### 3.4 Historical Data Tiering

```
Raw snapshots (5-min, 24h window)  -->  Hourly averages (7-day window)  -->  Daily averages (90-day)
```

**Logic Review:**
- Snapshots older than 24 hours are compacted into hourly averages.
- Hourly averages older than 7 days are compacted into daily averages.
- Compaction runs in `checkStaleClients()` and `maintenanceTasks()`.
- **SOUND** — standard time-series data retention pattern.

### 3.5 Stale Client Detection & Auto-Pruning

```
Client silent for STALE_CLIENT_THRESHOLD_SECONDS  -->  Send SMS alert (once)
Client silent for STALE_CLIENT_PRUNE_SECONDS      -->  Archive to FTP, then remove
```

**Logic Review:**
- Phase 2 (orphan sensor pruning): If some sensors from a client are fresh but others are stale beyond 72 hours, the stale ones are pruned. This handles monitor reconfiguration gracefully. **SOUND**
- Phase 4 (full client removal): Uses deferred removal to avoid modifying the metadata array during iteration. The `clientsToRemove` array is processed after the scan completes. **SOUND**
- `staleAlertSent` flag is reset when the client resumes reporting. **SOUND**

### 3.6 SMS Rate Limiting (Server Side)

```
checkSmsRateLimit(rec)  -->  Check MIN_SMS_ALERT_INTERVAL_SECONDS
                         -->  Prune timestamps older than 1 hour (epoch-based)
                         -->  Check MAX_SMS_ALERTS_PER_HOUR
                         -->  Record timestamp
```

**Logic Review:**
- Uses `double` epoch timestamps (not `millis()`), so the first-hour underflow bug (CR-H1) does **not** apply to the Server's rate limiter. **SOUND**
- The Server receives epoch time from `currentEpoch()` (Notecard-synced RTC). If time sync is not available (`now <= 0.0`), all SMS are denied. **SOUND — fail-safe**
- `countToCheck` is capped at 10 (array size), preventing out-of-bounds access even if `smsAlertsInLastHour` is corrupted. **SOUND**
- Each `SensorRecord` has its own rate-limit state (`smsAlertTimestamps[10]`, `smsAlertsInLastHour`). This means sensor A's SMS rate doesn't affect sensor B's. **SOUND**

---

## 4. Viewer Logic Analysis

### 4.1 Summary Fetch Cycle

```
fetchViewerSummary()  -->  note.get (viewer_summary.qi)  -->  Parse JSON
                      -->  note.delete (if successful parse)
                      --> safeSleep(pollInterval)
```

**Logic Review:**
- Two-step get-then-delete prevents data loss: the note is only deleted after successful parsing. If parsing fails, the note remains for the next poll cycle. **SOUND**
- Schema version check (`"sv"`) rejects notes from older/newer firmware. **SOUND**
- `safeSleep()` breaks long delays into short chunks with watchdog kicks. **SOUND**

### 4.2 Dashboard Decision Logic

The Viewer displays sensor data with color-coded states:

```
alarmActive  -->  Red background + alarm type label
sensorFailed -->  Orange background + "FAULT" label
else         -->  Normal display with green/gray
```

**Logic Review:**
- Alarm state is delivered as a boolean `alarmActive` in the summary JSON. The Viewer does not independently evaluate alarm conditions — it trusts the Server's determination. **SOUND — single source of truth**
- If the Viewer loses contact with the Server (no new summaries for several poll cycles), it continues displaying the last known state and adds a "Data may be stale" banner. **SOUND**

---

## 5. Cross-Component Logic Interaction

### 5.1 Alarm End-to-End Flow

```
Client sensor read
    --> evaluateAlarms() [debounce, latch check]
    --> sendAlarm() [relay actuation first, then rate-limit check, then note.add]
    --> Blues Notecard queues alarm.qo
    --> Notehub Route delivers to Server's alarm.qi
    --> Server processNotefile("alarm.qi", handleAlarm)
    --> handleAlarm() [upsert sensor record, SMS rate limit, send SMS]
```

**Total latency:** 30s (Client cellular sync) + 5-15s (Notehub routing) + 30s (Server Notecard poll) = **~60-75 seconds typical**

**Logic gap: None.** Each stage has appropriate error handling and the message is persisted at each hop.

### 5.2 Config Push End-to-End Flow

```
Server web UI config change
    --> sendConfigViaNotecard() [note.add to config.qi, routed to Client]
    --> Client receives config.qi
    --> Validates, saves to flash, applies
    --> Sends config_ack.qo back to Server
    --> Server marks config as acknowledged
```

**Logic Review:**
- Config dispatch uses retry logic (`MAX_CONFIG_DISPATCH_RETRIES`) with auto-cancel after 5 attempts. **SOUND**
- Auto-retry runs in `retryPendingConfigDispatches()` with a watchdog kick before each attempt. **SOUND — prevents watchdog timeout during multi-client config pushes**

### 5.3 Relay Cross-Device Flow

```
Client A alarm triggers relay for Client B
    --> Client A: triggerRemoteRelays() sends relay_fwd.qo
    --> Server receives relay_fwd.qi
    --> Server: handleRelayForward() sends relay_cmd.qo to Client B
    --> Client B receives relay_cmd.qi
    --> Client B: actuates relay GPIO
```

**Logic Review:**
- The relay command traverses THREE hops (Client A → Notehub → Server → Notehub → Client B). Total latency: ~2-4 minutes.
- If Client B's Notecard is offline, the command queues at Notehub and delivers when Client B reconnects. There is no timeout or expiry on queued relay commands.
- **LR-5 (FINDING):** If a relay ON command gets delayed and the alarm clears before it arrives, Client B will still actuate the relay. The subsequent clear → relay OFF command will also be queued, but may arrive much later. During the gap, the relay is ON without an active alarm condition. The relay safety timeout (`relayMaxOnMinutes`) is the backstop. This is an inherent limitation of the store-and-forward architecture and cannot be fully resolved without adding timestamps to relay commands and checking freshness on the receiving end.

---

## 6. Summary of Logic Findings

| ID | Severity | Component | Description |
|----|----------|-----------|-------------|
| LR-1 | LOW | Client | Clear debounce counter can accumulate across non-consecutive oscillation cycles |
| LR-2 | INFO | Client | Relay deactivation mask correctly uses active bits, not config mask — verified SOUND |
| LR-3 | MEDIUM | Client | Stuck detection interferes with unload tracking (cross-ref CR-M1) |
| LR-4 | HIGH | Server | System alarm SMS bypass rate limiter (cross-ref CR-H3) |
| LR-5 | MEDIUM | Client/Server | Delayed relay commands may actuate after alarm clears (architecture limitation) |

### Logic Strengths

- **Power state machine** is well-designed with hysteresis, debounce, and step-by-step recovery
- **Relay safety** correctly separates actuation from notification, with safety timeouts as a backstop
- **Alarm debouncing** prevents false positives from sensor noise
- **Server rate limiting** uses epoch timestamps (not millis), avoiding the clock-wraparound class of bugs
- **Deferred modification pattern** in `checkStaleClients()` prevents iterator invalidation
- **Two-step note processing** (peek → process → delete) prevents data loss on parsing failures
- **Defense-in-depth** deduplication runs periodically as a safety net for hash table integrity
