# Website Preview - TankAlarm 112025 Server

This document contains information about the web interface served by the TankAlarm-112025-Server-BluesOpta.

## Web Pages

All web pages have been optimized for reduced resource usage on Arduino Opta devices using minified CSS and JavaScript.

### Dashboard (`/`)
Main fleet telemetry dashboard showing:
- Server metadata (UID, fleet name, sync status)
- Statistics (total clients, active tanks, alarms, stale tanks)
- Fleet telemetry table with relay controls

### Client Console (`/client-console`)
Configuration management interface for remote clients:
- Client selection and status overview
- Configuration form (site, device label, sample settings)
- Tank configuration table
- FTP backup/restore settings
- PIN-protected controls

### Config Generator (`/config-generator`)
Create new client configurations:
- Site and device settings
- Power configuration (solar vs grid-tied)
- Sensor definitions (digital, analog, current loop, RPM)
- Alarm thresholds and relay control
- SMS alert configuration

### Serial Monitor (`/serial-monitor`)
Debug log viewer:
- Server serial output (auto-refreshing)
- Client serial logs with panel pinning
- Site and client filtering

### Calibration (`/calibration`)
Calibration learning system:
- Manual level reading submission
- Calibration status tracking
- Drift analysis
- Linear regression learning

### Contacts Manager (`/contacts`)
Contact and notification management:
- Contact list with alarm associations
- Daily report recipient configuration
- Site and alarm filtering

## Optimization Notes

All HTML pages use:
- Minified inline CSS (single-line)
- Minified JavaScript (whitespace removed)
- Consistent simple styling across all pages
- CSS variables for theming (light/dark mode support)
- Responsive design patterns

---
*Last updated: 2024-12*
