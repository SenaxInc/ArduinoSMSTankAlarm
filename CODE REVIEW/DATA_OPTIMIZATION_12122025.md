# Data Transmission Optimization Review - 12/12/2025

## Summary
Reviewed data transmission code in both Server and Client firmware to identify opportunities for data savings.
The primary optimization applied is rounding floating-point values to reduce JSON payload size.

## Changes Applied

### Server (TankAlarm-112025-Server-BluesOpta)
1.  **Added `roundTo` helper function**: Rounds floating-point numbers to a specified number of decimal places.
2.  **`publishViewerSummary`**:
    *   `levelInches` rounded to 1 decimal place.
    *   `percent` rounded to 1 decimal place.
    *   `sensorMa` rounded to 2 decimal places.
    *   `vinVoltage` rounded to 2 decimal places.
    *   `heightInches` rounded to 1 decimal place.
3.  **`sendDailyEmail`**:
    *   `levelInches` rounded to 1 decimal place.
    *   `percent` rounded to 1 decimal place.
    *   `sensorMa` rounded to 2 decimal places.

### Client (TankAlarm-112025-Client-BluesOpta)
1.  **Added `roundTo` helper function**.
2.  **`sendTelemetry`**:
    *   `currentInches` rounded to 1 decimal place.
    *   `currentSensorMa` rounded to 2 decimal places.
3.  **`sendAlarm`**:
    *   `inches` rounded to 1 decimal place.
    *   `highAlarmThreshold` rounded to 1 decimal place.
    *   `lowAlarmThreshold` rounded to 1 decimal place.

## Further Recommendations (Future)
1.  **Note Templates**: For high-frequency telemetry, using `note.template` could save significant data by omitting JSON keys. This would require refactoring `publishViewerSummary` to send individual notes per tank or using fixed-size arrays.
2.  **Delta Updates**: The Viewer summary sends full state every 6 hours. If the state hasn't changed, this could be skipped or reduced to a heartbeat. However, the current interval (6 hours) is already infrequent.
3.  **Field Shortening**: Some keys in `sendDailyEmail` are long ("levelInches", "sensorMa"). Shortening them (e.g., "l", "ma") would save bytes, but requires updating the email template/route.

## Impact
These changes reduce the size of JSON payloads, especially for the Viewer summary which contains an array of tank records. By trimming 4-6 decimal places per float, we save ~4-6 bytes per value, which adds up across multiple fields and records.
