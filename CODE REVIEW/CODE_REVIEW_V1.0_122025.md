# Code Review: v1.0 Release Readiness (December 2025)

**Date:** December 12, 2025
**Reviewer:** GitHub Copilot
**Scope:** Server, Client, and Viewer (Blues/Opta 112025 versions)

## Executive Summary
The codebase is well-structured and leverages the capabilities of the Arduino Opta and Blues Notecard effectively. However, there are critical optimizations required for energy efficiency (specifically for solar deployments) and data transmission overhead before the v1.0 release. The web interface can also be optimized to reduce memory footprint and load times.

---

## 1. Client Energy Efficiency (Critical)

### Findings
The Client `loop()` currently uses hardcoded polling intervals that ignore the `solarPowered` configuration. This results in the device waking up and checking for updates every 10 minutes, even when configured for solar operation (where a 60-minute interval is defined).

**Location:** `TankAlarm-112025-Client-BluesOpta.ino` (lines ~630-660)

### Issues
- `pollForConfigUpdates`, `pollForRelayCommands`, and `pollForSerialRequests` run every `600000UL` (10 minutes).
- `checkNotecardHealth` runs every `300000UL` (5 minutes).
- `SOLAR_INBOUND_INTERVAL_MINUTES` (60 mins) is defined but not utilized in the main loop logic.

### Recommendations
Modify the polling logic to respect the `gConfig.solarPowered` flag.

**Proposed Fix:**
```cpp
// Determine polling interval based on power source
unsigned long inboundInterval = gConfig.solarPowered ? 
    (unsigned long)SOLAR_INBOUND_INTERVAL_MINUTES * 60000UL : 
    600000UL; // 10 minutes for grid power

if (now - gLastConfigCheckMillis >= inboundInterval) {
    gLastConfigCheckMillis = now;
    pollForConfigUpdates();
}

// Apply similar logic to Relay and Serial polling
```

---

## 2. Data Transmission Optimization (KB Usage)

### Findings
The JSON payloads used for API responses and Notecard notes use verbose keys. While readable, this adds unnecessary overhead to every transmission, which accumulates significantly over time for cellular data plans.

**Location:** `TankAlarm-112025-Server-BluesOpta.ino` (`sendTankJson`, `sendClientDataJson`)

### Issues
- **Server API:** Keys like `"heightInches"`, `"levelInches"`, `"lastUpdateEpoch"` are long.
- **Client Telemetry:** (Inferred) If the client uses similar keys for Notecard notes, it wastes data credits.

### Recommendations
1. **Shorten JSON Keys:** Map verbose keys to short codes (1-2 chars) for transmission.
   - `heightInches` -> `h`
   - `levelInches` -> `l`
   - `percent` -> `p`
   - `alarm` -> `a`
   - `tank` -> `t`
   - `site` -> `s`
   
   *Note: This requires updating the frontend JavaScript to map these short keys back to readable values for display.*

2. **Data Aggregation:** Ensure `sampleTanks` aggregates data for all tanks into a single Note if possible, rather than sending one Note per tank, to save on packet overhead.

---

## 3. Website & Memory Optimization

### Findings
The HTML/CSS content is stored in `PROGMEM` as raw, formatted strings with comments and whitespace. This consumes unnecessary Flash memory and increases the transfer size.

**Location:** `TankAlarm-112025-Server-BluesOpta.ino` (`DASHBOARD_HTML`, etc.)

### Issues
- `DASHBOARD_HTML` contains full CSS with comments and whitespace.
- `VIEWER_DASHBOARD_HTML` in the Viewer is similarly unoptimized.
- Large string literals can impact compilation and upload times, and slightly affect runtime performance when reading from Flash.

### Recommendations
1. **Minify HTML/CSS:** Remove all unnecessary whitespace, newlines, and comments from the `PROGMEM` strings.
2. **Remove Redundant CSS:** Check for unused CSS classes.
3. **Gzip Compression (Advanced):** If the Ethernet library and browser support allows, serving pre-gzipped content would drastically reduce transfer size, though this adds complexity to the server code (handling `Content-Encoding`). For v1.0, minification is the low-hanging fruit.

**Example of Minification:**
*Before:*
```html
    body {
      margin: 0;
      min-height: 100vh;
      /* background color */
      background: var(--bg);
    }
```
*After:*
```html
body{margin:0;min-height:100vh;background:var(--bg)}
```

---

## 4. Viewer Component

### Findings
The Viewer correctly implements a "read-only" kiosk mode. The 6-hour fetch interval (`SUMMARY_FETCH_INTERVAL_SECONDS`) is appropriate for a low-bandwidth viewer.

### Recommendations
- Ensure the Viewer's `MAX_HTTP_BODY_BYTES` (1024) is sufficient for the summary file if the fleet grows. If the server compresses the summary or uses short keys (as recommended above), this limit will hold longer.

## Action Plan for v1.0
1. **[Client]** Implement solar-aware polling intervals in `loop()`.
2. **[Server/Client]** Shorten JSON keys in API and Telemetry.
3. **[Server/Viewer]** Minify all `PROGMEM` HTML strings.
4. **[All]** Verify `DEBUG_MODE` is disabled in all final builds.
