# Code Review 112025 Implementation

This document tracks the execution of the code review plan for the 112025 Server, Client, and Viewer components.

## Status
- [x] 1. Review Common Library
- [x] 2. Analyze Server Monolith
- [x] 3. Audit Memory Usage
- [x] 4. Check HTML Handling
- [x] 5. Security Review
- [x] 6. Client Reliability

## Findings

### 1. Common Library Review
- **Status:** Completed
- **Files Reviewed:** `TankAlarm_Common.h`, `TankAlarm_Utils.h`
- **Findings:**
    - `MAX_TANK_RECORDS` is confirmed at 64.
    - Notefile names are standardized (`telemetry.qi`, `alarm.qi`, etc.).
    - `strlcpy` is polyfilled for non-Mbed platforms.
    - `tankalarm_computeNextAlignedEpoch` logic appears sound.
    - **Recommendation:** Ensure `MAX_TANK_RECORDS` is consistent with Server buffers.

### 2. Server Monolith Analysis
- **Status:** Completed
- **File:** `TankAlarm-112025-Server-BluesOpta.ino` (6628 lines)
- **Findings:**
    - **Structure:** Single massive file containing HTTP server, Notecard sync, Business Logic, and Utilities.
    - **Dependencies:** `ArduinoJson`, `PortentaEthernet`, `LittleFS`.
    - **Recommendation:** Split into:
        - `Server_HTTP.cpp`: Web server and API endpoints.
        - `Server_Notecard.cpp`: Notecard sync and data ingestion.
        - `Server_Logic.cpp`: Alarm processing, email/SMS dispatch.
        - `Server_Storage.cpp`: Config persistence and LittleFS wrappers.

### 3. Memory Audit
- **Status:** Completed
- **Findings:**
    - `MAX_EMAIL_BUFFER`: 16KB
    - `CLIENT_JSON_CAPACITY`: 32KB
    - `MAX_HTTP_BODY_BYTES`: 16KB
    - **Analysis:** The Arduino Opta (STM32H7) has 1MB of RAM. These static/global allocations (~64KB total) are well within limits.
    - **Recommendation:** Verify these are global or heap-allocated, not stack-allocated.

### 4. HTML Handling
- **Status:** Completed
- **Findings:**
    - Large HTML strings found in `TankAlarm-112025-Server-BluesOpta.ino`:
        - `SERIAL_MONITOR_HTML` (Line 686)
        - `CALIBRATION_HTML` (Line 688)
        - `HISTORICAL_DATA_HTML` (Line 690)
        - `CONTACTS_MANAGER_HTML` (Line 692)
        - `DASHBOARD_HTML` (Line 694)
        - `CLIENT_CONSOLE_HTML` (Line 696)
    - **Recommendation:** Extract these to external files (e.g., `dashboard.html`) and serve them from LittleFS. This will reduce code size and make frontend development easier.

### 5. Security Review
- **Status:** Completed
- **Findings:**
    - **Credentials:** FTP password and Admin PIN are stored in `ServerConfig` (memory) and `server_config.json` (LittleFS).
    - **Exposure:** Not exposed via API (only boolean flags `pset`, `pc` are sent).
    - **Transport:** Plain HTTP (risk of interception).
    - **File Access:** No generic file handler, so `server_config.json` is safe from direct web access.
    - **Recommendation:** Document the HTTP risk. Ensure `server_config.json` remains protected.

### 6. Client Reliability
- **Status:** Completed
- **File:** `TankAlarm-112025-Client-BluesOpta.ino`
- **Findings:**
    - **Error Handling:** `SENSOR_STUCK_THRESHOLD` (10) and `SENSOR_FAILURE_THRESHOLD` (5) are defined.
    - **Power Management:** `SOLAR_OUTBOUND_INTERVAL_MINUTES` (360) and `SOLAR_INBOUND_INTERVAL_MINUTES` (60) suggest power-aware design.
    - **Buffering:** `NOTE_BUFFER_MAX_BYTES` (16KB) for offline buffering.
    - **Max Tanks:** `MAX_TANKS` is 8.
    - **Recommendation:** Ensure `MAX_TANKS` matches the hardware capabilities (Opta has limited analog inputs, expansion might be needed for 8 tanks).

## Next Steps
1.  **Refactor Server:** Split `TankAlarm-112025-Server-BluesOpta.ino` into modules.
2.  **Extract HTML:** Move HTML strings to LittleFS.
3.  **Verify Hardware:** Confirm Opta expansion module support for 8 tanks on Client.
