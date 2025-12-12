# Code Review Update: v1.0 Release Readiness

**Date:** December 12, 2025
**Reviewer:** GitHub Copilot
**Scope:** Server, Client, and Viewer (Blues/Opta 112025 versions)

## Executive Summary
This review follows up on the initial v1.0 readiness review. Several critical issues have been addressed, but a new **critical bug** has been identified in the Viewer implementation that will prevent it from displaying data. The website optimization tasks remain outstanding.

---

## 1. Status of Previous Findings

| Finding | Status | Notes |
| :--- | :--- | :--- |
| **Client Energy Efficiency** | ✅ **Fixed** | `loop()` now correctly uses `SOLAR_INBOUND_INTERVAL_MINUTES` when `gConfig.solarPowered` is true. |
| **Data Transmission Optimization** | ✅ **Fixed** | Server now uses short JSON keys (`h`, `l`, `p`, etc.) for API and summary notes. |
| **Website & Memory Optimization** | ❌ **Open** | HTML/CSS in `DASHBOARD_HTML` (Server) and `VIEWER_DASHBOARD_HTML` (Viewer) is still unminified. |

---

## 2. New Critical Findings

### 🔴 Viewer Data Parsing Failure (Critical)

**Location:** `TankAlarm-112025-Viewer-BluesOpta.ino` (`handleViewerSummary`)

**Issue:**
The Server has been updated to send "short" JSON keys in the viewer summary note (e.g., `"h"` for height, `"l"` for level), but the Viewer code still expects verbose keys (e.g., `"heightInches"`, `"levelInches"`).

**Impact:**
The Viewer will fail to parse any tank data from the server. The dashboard will show "No tank data available" or empty values.

**Code Mismatch:**
*Server (`sendTankJson` / `performViewerSummary`):*
```cpp
obj["h"] = gTankRecords[i].heightInches;
obj["l"] = gTankRecords[i].levelInches;
```

*Viewer (`handleViewerSummary`):*
```cpp
rec.heightInches = item["heightInches"].as<float>(); // Fails! Key is "h"
rec.levelInches = item["levelInches"].as<float>();   // Fails! Key is "l"
```

**Recommendation:**
Update `handleViewerSummary` in `TankAlarm-112025-Viewer-BluesOpta.ino` to use the short keys:
- `"client"` -> `"c"`
- `"site"` -> `"s"`
- `"label"` -> `"n"`
- `"tank"` -> `"k"`
- `"heightInches"` -> `"h"`
- `"levelInches"` -> `"l"`
- `"percent"` -> `"p"`
- `"alarm"` -> `"a"`
- `"alarmType"` -> `"at"`
- `"lastUpdate"` -> `"u"`
- `"vinVoltage"` -> `"v"`

---

## 3. Other Findings

### ⚠️ Hardcoded IP Addresses in Viewer
**Location:** `TankAlarm-112025-Viewer-BluesOpta.ino`
The Viewer code contains hardcoded static IP addresses:
```cpp
static IPAddress gStaticIp(192, 168, 1, 210);
```
**Recommendation:** Ensure this is intended for the specific deployment environment. Ideally, this should be configurable or default to DHCP.

### ⚠️ Code Duplication (TankRecord)
**Location:** Server and Viewer
The `TankRecord` struct is defined in both Server and Viewer. Any changes to one must be manually replicated to the other. The recent desynchronization of JSON keys illustrates the risk here.
**Recommendation:** Add a comment in both files referencing the other to warn developers to keep them in sync.

### ⚠️ Unminified HTML/CSS
**Location:** Server and Viewer
As noted in the previous review, the HTML strings are large and contain comments/whitespace.
**Recommendation:** Minify the HTML/CSS to save Flash memory and reduce transfer time.

---

## 4. Action Plan

1.  **Fix Viewer Parsing:** Update `handleViewerSummary` to use short keys immediately.
2.  **Minify HTML:** Run the HTML content through a minifier and update the `PROGMEM` strings.
3.  **Verify IPs:** Confirm if static IPs are required for the target deployment.
