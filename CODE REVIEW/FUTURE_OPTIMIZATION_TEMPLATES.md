# Future Optimization: Notecard Templates (`note.template`)

**Date:** December 12, 2025
**Status:** Proposed / Deferred
**Target:** Future firmware release (post-v1.0)

## Overview
This document outlines the strategy for implementing Blues Notecard Templates to significantly reduce cellular data consumption. This optimization is deferred until the data models (Telemetry, Alarm, Summary) are finalized and stable.

## The Problem
Currently, every JSON note sent via the Notecard includes both the **keys** and the **values**.
Example `telemetry.qi` payload:
```json
{
  "l": 45.5,
  "ma": 12.24,
  "st": "currentLoop",
  "c": "dev:123456789",
  "t": 1702384500
}
```
Even with shortened keys (`"l"` instead of `"levelInches"`), the overhead of transmitting the keys (`"l"`, `"ma"`, `"st"`, `"c"`, `"t"`) and JSON syntax (`{`, `:`, `,`) repeats for *every single note*.

## The Solution: Note Templates
The `note.template` request allows the device to register a schema for a specific Notefile. Once registered, the Notecard switches to "headless" mode for that file.

### How it Works
1.  **Registration**: The firmware sends a template request **once** (e.g., in `setup()`). This tells the Notecard what the data structure looks like.
2.  **Transmission**: When `note.add` is called, the Notecard strips all keys and sends only the values in a compact binary or array format.
3.  **Reconstruction**: Blues Notehub (cloud) uses the stored template to reconstruct the full JSON object before routing it to the final destination (Server/HTTP/MQTT).

**Data Savings:** Typically **30-50%** reduction in payload size.

## Implementation Strategy

### 1. Defining the Schema
The schema must be **strict**. Every note added to the file must contain exactly the same fields. Optional fields are not allowed; missing data must be represented by a placeholder (e.g., `0`, `-1`, or `null`).

**Proposed Telemetry Template:**
```cpp
J *req = notecard.newRequest("note.template");
JAddStringToObject(req, "file", "telemetry.qi");

J *body = JCreateObject();
JAddNumberToObject(body, "l", 14);   // 14 = 4-byte float
JAddNumberToObject(body, "ma", 14);  // 14 = 4-byte float
JAddStringToObject(body, "st", 12);  // 12 = string
JAddStringToObject(body, "c", 12);   // 12 = string
JAddNumberToObject(body, "t", 14);   // 14 = 4-byte float (timestamp)
JAddStringToObject(body, "r", 12);   // 12 = string (reason)

JAddItemToObject(req, "body", body);
notecard.sendRequest(req);
```

### 2. Sending Data
The code for sending data (`note.add`) remains largely the same, but we must ensure **every field defined in the template is present**.

```cpp
J *req = notecard.newRequest("note.add");
JAddStringToObject(req, "file", "telemetry.qi");
J *body = JCreateObject();

// ALL fields must be added, even if 0 or empty
JAddNumberToObject(body, "l", state.currentInches);
JAddNumberToObject(body, "ma", state.currentSensorMa); 
JAddStringToObject(body, "st", "currentLoop");
JAddStringToObject(body, "c", gDeviceUID);
JAddNumberToObject(body, "t", currentEpoch());
JAddStringToObject(body, "r", "sample");

JAddItemToObject(req, "body", body);
notecard.sendRequest(req);
```

## Challenges & Constraints

### 1. Fixed Structure
If we add a new field (e.g., `batteryLevel`) in the future, we must update the template. If a device with an old firmware sends data to a project with a new template (or vice versa), data corruption or loss can occur.
*   **Mitigation:** Version the Notefiles (e.g., `telemetry_v2.qi`) if the structure changes significantly.

### 2. Viewer Summary Complexity
The Viewer Summary (`viewer_summary.qi`) currently contains a dynamic array of tanks (`"tanks": [...]`). Templates do not support variable-length arrays well.
*   **Solution A:** Flatten the structure (e.g., `tank1_lvl`, `tank2_lvl`...) - limits max tanks.
*   **Solution B:** Split the summary into individual notes (one note per tank). This increases the number of transactions but makes each transaction highly efficient.

### 3. "Optional" Fields
Current code conditionally adds `vinVoltage` only if it's valid.
*   **Requirement:** With templates, we must always send `vinVoltage`, perhaps using `0.0` to indicate "invalid".

## Roadmap for Adoption

1.  **Freeze Data Model:** Wait until v1.0 is deployed and we are certain no new telemetry fields are needed immediately.
2.  **Refactor Client:** Update `sendTelemetry` and `sendAlarm` to always populate all fields.
3.  **Refactor Server:** Update `publishViewerSummary` to potentially use a "one note per tank" model if the array structure is too heavy.
4.  **Deploy:** Push firmware update. The `setup()` routine will register the new template, and immediate data savings will be realized.
