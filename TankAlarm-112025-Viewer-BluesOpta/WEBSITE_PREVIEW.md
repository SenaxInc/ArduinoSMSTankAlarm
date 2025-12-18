# Website Preview - TankAlarm 112025 Viewer

This document contains information about the web interface served by the TankAlarm-112025-Viewer-BluesOpta.

## Web Pages

All web pages have been optimized for reduced resource usage on Arduino Opta devices using minified CSS and JavaScript.

### Viewer Dashboard (`/`)
Read-only fleet telemetry dashboard showing:
- Server metadata (UID, fleet name)
- Statistics (total clients, active tanks, alarms, stale tanks)
- Fleet telemetry table (level, status, last update)

The viewer dashboard is a lightweight, read-only version of the server dashboard designed for monitoring without administrative controls.

## Optimization Notes

The HTML page uses:
- Minified inline CSS (single-line)
- Minified JavaScript (whitespace removed)
- Consistent simple styling matching the server dashboard
- CSS variables for theming (light/dark mode support)
- Responsive design patterns

---
*Last updated: 2024-12*
