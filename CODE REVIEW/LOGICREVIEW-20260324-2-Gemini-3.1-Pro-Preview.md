# Scholarly Review of System Decision Logic: Arduino Tank Alarm (v112025)

## 1. Client Telemetry & Alarm Evaluation (`TankAlarm-112025-Client-BluesOpta.ino`)

The client encapsulates the highest concentration of localized decision-making, predominantly handled within the `evaluateAlarms(uint8_t idx)` and `checkRelayMomentaryTimeout(unsigned long now)` functions.

### 1.1 Analog/Loop Evaluation & Hysteresis Logic
The system evaluates alarms via standard hysteresis to prevent state toggling (bouncing) on minor signal noise:
```cpp
float highTrigger = cfg.highAlarmThreshold;
float highClear = cfg.highAlarmThreshold - cfg.hysteresisValue;

bool highCondition = state.currentInches >= highTrigger;
bool clearCondition = (state.currentInches < highClear) && (state.currentInches > lowClear);
```
**Feedback & Pitfalls:**
*   **Solid Foundation:** The threshold math correctly creates a "deadband" zone, ensuring localized sloshing in tanks won't spam Notecard sync operations.
*   **Logic Pitfall - Debounce Starvation:** The debounce counters (`state.highAlarmDebounceCount`, `state.clearDebounceCount`) mandate a *pure continuous state* for `ALARM_DEBOUNCE_COUNT` cycles to trigger a change. 
    ```cpp
    // Reset clear counter if we're back in alarm territory
    if (highCondition || lowCondition) {
      state.clearDebounceCount = 0;
    }
    ```
    *Issue:* If fluid dynamically rests exactly on the threshold, random noise causing a singular cycle "breach" backwards will reset the counter entirely. A leaky bucket or moving average algorithm on the sensor noise floor rather than raw counter-resets would provide substantially higher theoretical resilience.

### 1.2 Digital Sensor Evaluation
For `SENSOR_DIGITAL` interface models (e.g., simple float switches), the client skips analog hysteresis and evaluates string-based triggers:
```cpp
if (strcmp(cfg.digitalTrigger, "activated") == 0) {
    shouldAlarm = isActivated;  
}
```
**Feedback & Pitfalls:**
*   **Logical Gap:** The digital switch evaluation hardcodes `state.currentInches > DIGITAL_SWITCH_THRESHOLD` but leans on the same `ALARM_DEBOUNCE_COUNT` constant used for analog loop polling. Digital contacts (like mechanical float switches) frequently experience micro-second mechanical bounce. Software debouncing mapped to a slower analog update loop loop might miss rapid bounces or artificially delay reaction times unexpectedly depending on the frequency of the poll timer.

### 1.3 Relay Execution & State Management
The `checkRelayMomentaryTimeout(unsigned long now)` controls automated off/pump constraints.
**Feedback & Pitfalls:**
*   **Implementation Strength:** The routine properly isolates independent relay timers mapped under a specific bitmask `(gRelayActiveMaskForMonitor[i] & (1 << r))` and correctly handles internal clock rollovers via `now - gRelayActivationTime[r] >= durationMs` using purely unsigned math.
*   **Missing Feedback Loop (Blind Firing):** The timeout execution does not verify hardware status. It shuts off the relay logic and updates internal registers but has no feedback loop directly confirming via secondary inputs (like current-draw down) that the relay successfully disengaged physically, leaving potential desync between physical state and reported logic state if the SSR fuses shut. 

## 2. Server Coordination & Handling (`TankAlarm-112025-Server-BluesOpta.ino`)

The Server's C++ code primarily operates as a deterministic relay multiplexer, routing requests from Notehub logic to its internal webview or logging mechanisms, rather than acting as a discrete standalone decision engine.

### 2.1 Health Check Backoff Loop
The Server relies on an exponential I2C Notecard polling backoff:
```cpp
if (ncHealthInterval < NOTECARD_HEALTH_CHECK_MAX_INTERVAL_MS) {       
  ncHealthInterval *= 2;
  // ... capped at MAX ...
}
```
**Feedback & Pitfalls:**
*   **Solid Foundation:** Exponential backoffs prevent blocking processes when hardware is non-responsive. The `NotecardFailureCount` correctly triggers a bus recovery fallback. 
*   **Network Isolation:** `handleWebRequests()` combined with static HTML and JSON packaging indicates that the server pushes "stale client" logic *downstream to the JavaScript GUI clients* rather than acting upon it locally.
*   **The "Silent Client" Pitfall:**  Because the dashboard GUI calculates staleness (e.g., `isStale(cl.lastUpdate)` rendered in JS), if a client physically catastrophically fails or loses antenna, and no human is actively reviewing the viewer application, no "Alarm" is technically tripped by the Server payload to the Notecard. The server functions reactively to alarms sent *from* the Client, neglecting deterministic dead-man-switching locally out of the C++ codebase.

## 3. Peripheral Health Evaluations (`TankAlarm_Solar.cpp` and Commons)

The `updateHealthStatus()` mathematically constructs the health identity of attached off-grid mechanisms.

```cpp
// Overall solar system health
_data.solarHealthy = _data.batteryHealthy &&
                     _data.communicationOk &&
                     !_data.hasFault &&
                     !_data.hasAlarm;
```

**Feedback & Pitfalls:**
*   **False Positive Logic Constraint:** Standard operations classify a low-battery event as a physical alert. However, if `_data.communicationOk` maps to false (e.g., broken UART wire), `solarHealthy` resolves to `false` automatically. Since `checkAlerts()` assesses battery voltage limits to dispatch `SOLAR_ALERT_BATTERY_CRITICAL`, a broken comms line retains the *last read register*. This could permanently trap the system in an emergency alert due to stale registers combined with a failed comms link, or inversely fail to log an alert because the registers aren't updating to critical limits while `communicationOk` has failed. A timeout-invalidation rule is completely missing inside this logic block.

## Summary Conclusion

The logic architectures evaluated show strong maturity, specifically in localized hardware debouncing and unsigned-integer timekeeping for asynchronous operations without delay blocking.

**Critique & Recommendations:**
1. **Move "Stale / Offline" Evaluation to the Master Server C++ Code:** Relying on UI JavaScript to flag a client as offline obscures a primary failure loop. Ensure the `TankAlarm-112025-Server-BluesOpta.ino` can identify and push a `dead-client` webhook out via Notehub so a user is texted if a client battery dies.
2. **Abstract Debounce Variables:** A unified `ALARM_DEBOUNCE_COUNT` is ill-fitted for simultaneously managing instantaneous digital float-switch bounces and slow-reading 4-20mA fluid dynamics. Separating these ensures faster logic reactions for specific hardware triggers. 
3. **Invalidate Stale Solar Registers:** Introduce a caching decay inside `SolarManager::poll()`. If communication fails continuously over a certain duration, zero out floating indicators such as `voltage` to prevent the alert logic from continually referencing a ghost value.