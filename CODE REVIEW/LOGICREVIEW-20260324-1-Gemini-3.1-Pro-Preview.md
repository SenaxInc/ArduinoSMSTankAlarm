# Architecture & Decision-Making Function Review: TankAlarm Ecosystem

This review evaluates the programmatic decisions and thresholds established to regulate logic, state management, alerts, and feedback loops across the sensor client, centralized server, viewer, and shared libraries within the TankAlarm ecosystem.

## 1. TankAlarm-112025-Common (Sensors & Solar Modbus)
The core intelligence for localized resource handling resides in shared resources like `TankAlarm_Solar.cpp`, which evaluates the health and state of a SunSaver Modbus Solar Controller.

### Analysis of `poll()` and Health Evaluations
* **Logic Foundation:** 
  The function `readRegisters()` sequences calls to fetch Modbus data. It updates the global `success` boolean iteratively. 
* **Pitfall - Ambiguous Returns:** 
  The `poll(unsigned long nowMillis)` function returns `false` if `!_config.enabled` or if `nowMillis - _lastPollMillis < intervalMs`. However, it also returns the result of `readRegisters()`, which yields `false` on a Modbus timeout. This creates a logical gap for the caller: a `false` return does not distinguish between "It is not time to poll" and "A critical communication error occurred."
* **Pitfall - Flapping Feedback Loop in Error Counting:**
  The logic tracks `consecutiveErrors` and trips communication failure at `SOLAR_COMM_FAILURE_THRESHOLD`. However, a *single* successful read immediately resets `_data.consecutiveErrors` to `0`. On highly degraded lines where 1 in 5 packets succeeds, the threshold might never be reached, hiding severe network degradation from higher-level alert systems. *Recommendation:* Implement a leaky bucket or moving average for error counting rather than absolute consecutive resets.
* **Incorrect Assumptions in Scaling:** 
  The scaling equations (e.g., `(raw * SS_SCALE_VOLTAGE_12V) / SS_SCALE_DIVISOR`) hardcode scaling toward a 12V schema. If the SunSaver device is autonomously switched to a 24V array, the logic makes a dangerous assumption that triggers premature low-battery alerts (`SOLAR_ALERT_BATTERY_CRITICAL`).

### Alert Threshold Decision Strategy
The `checkAlerts()` logic evaluates conditions linearly from most critical to least:
1. `SOLAR_ALERT_BATTERY_CRITICAL` -> 2. `SOLAR_ALERT_FAULT` -> 3. `SOLAR_ALERT_COMM_FAILURE`
* **Logical Gap:** If `_data.communicationOk == false`, the function first checks `_data.batteryVoltage < _config.batteryCriticalVoltage`. If communication just failed *after* a low-voltage reading, the system will permanently output `SOLAR_ALERT_BATTERY_CRITICAL` rather than realizing communication is down. Stale data drives high-priority alarms incorrectly.

## 2. TankAlarm-112025-Server-BluesOpta
The Server manages data warehousing, authentication, and client routing. The logic dictates rate limiting, hashing, and PIN enforcement.

### Authentication Lock-out Loop
Analysis of the `isValidPin()` and lockout implementations:
* **Pitfall - Time Foundation Dependency:** 
  The lockout strictly relies on the difference from when the failure occurred versus `millis()` or an RTC epoch. In autonomous IoT environments using RTC/NTP sync loops, an NTP correction could cause time to shift backward. If `now` mathematically precedes `timeSinceFail`, wrap-arounds or negative bounds testing may perpetually lock out valid clients or bypass the lockout duration entirely.
* **Logic Gap - Hash Collision Handing:** 
  The server indexing checks `if (recordIdx >= MAX_TANK_RECORDS)` and validates uniqueness via `strcmp(rec.clientUid, clientUid) == 0`. The reliance on fixed 4-character PIN validation and raw string comparisons under extreme request volumes has limited elasticity. Without backoff delay on arbitrary failed hashes, the server could succumb to rapid brute-force collisions. 

## 3. TankAlarm-112025-Client-BluesOpta
The Client handles physical telemetry from standard float sensors and feeds data over Notehub. 

* **Pitfall - Synchronization Bottleneck:** 
  Client systems typically aggregate states and then sync. If an intermediate alert condition fires (e.g., Tank Low → Tank OK) inside the sync window, the server only receives the localized terminal state. High-frequency physical wave sloshing inside the tank will trigger state thrashing without proper debounce logic, exhausting payload constraints and flooding Notehub with hysteresis events. 

## 4. TankAlarm-112025-Viewer-BluesOpta
The Viewer provides local/remote visual display of server-held data. 

* **Feedback Loop:**
  The Viewer updates visual outputs by polling the Server. If the Server encounters a `gAuthFailureCount` restriction, the Viewer often falls back to displaying "stale" data rather than explicitly indicating a "Server Unreachable" topology. The logic relies on caching previous valid states, producing a false positive for the user—believing the tank is fine while the server link has been dead for hours. 

---

### Executive Summary & Recommendations
1. **Segregate Return Signatures:** Adopt `enum PollStatus { POLL_NOT_READY, POLL_SUCCESS, POLL_CABLE_FAULT }` across hardware abstractions so upper-level decision engines don't misinterpret "waiting" as "successful execution."
2. **Implement Staleness Decorators:** Do not allow critical alerts (like `SOLAR_ALERT_BATTERY_CRITICAL`) to be triggered off a variable that was populated prior to a subsequent `SOLAR_ALERT_COMM_FAILURE` event. Add a freshness constraint (`now - lastReadMillis`).
3. **Decouple Hysteresis:** Tank sloshing and Modbus packet loss should run through separate mathematical smoothing windows (e.g., Exponential Moving Averages for battery analog scaling) before a discrete decision node pushes an alert boundary.
4. **Time Wrap/Drift Handling:** Incorporate monotonic counters or time validation that accounts for NTP sync shifts in the authentication and lock-out modules.