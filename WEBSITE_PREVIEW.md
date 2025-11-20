# Arduino SMS Tank Alarm - Config Generator Website Preview

This document provides a preview mockup of the Config Generator web interface for the Arduino SMS Tank Alarm system.

## Overview

The Config Generator is a web-based tool that allows users to easily create configuration files for their tank monitoring systems without manually editing text files. It provides an intuitive interface for configuring client and server devices.

---

## Config Generator Page Mockup

### Header Section
```
┌─────────────────────────────────────────────────────────────────┐
│ Arduino SMS Tank Alarm - Configuration Generator                │
│ Generate configuration files for your tank monitoring system    │
└─────────────────────────────────────────────────────────────────┘
```

---

### Configuration Type Selection

**Select Configuration Type:**
- [ ] Client Configuration (Tank Monitor)
- [ ] Server Configuration (Data Aggregator)

---

### Client Configuration Form

#### Site & Tank Identification
```
┌─────────────────────────────────────────────────────────────────┐
│ Site Name:          [________________________]                   │
│ Tank Number:        [_______]                                    │
│                                                                   │
│ Example: "North Farm", Tank #101                                │
└─────────────────────────────────────────────────────────────────┘
```

#### Physical Tank Dimensions
```
┌─────────────────────────────────────────────────────────────────┐
│ Tank Height (inches): [_______]                                  │
│                                                                   │
│ Note: Total height of the tank in inches                        │
└─────────────────────────────────────────────────────────────────┘
```

#### Alarm Thresholds
```
┌─────────────────────────────────────────────────────────────────┐
│ High Alarm Level (inches): [_______]                            │
│ Low Alarm Level (inches):  [_______]                            │
│                                                                   │
│ Note: Level in inches at which to trigger alarms                │
└─────────────────────────────────────────────────────────────────┘
```

#### Sensor Configuration
```
┌─────────────────────────────────────────────────────────────────┐
│ Select Sensor Type:                                              │
│   ( ) Digital Float Switch                                       │
│       └─ Digital Pin: [___]                                      │
│                                                                   │
│   ( ) Analog Voltage Sensor (0.5V - 4.5V)                       │
│       └─ Analog Pin: [___] (e.g., A0, A1, A2)                  │
│                                                                   │
│   (•) 4-20mA Current Loop Sensor (via I2C)                      │
│       └─ Channel Number: [___] (0-3)                            │
│                                                                   │
│ Note: Choose ONE sensor type for your tank                      │
└─────────────────────────────────────────────────────────────────┘
```

#### Contact Information
```
┌─────────────────────────────────────────────────────────────────┐
│ Primary Alarm Phone:    [+1______________]                       │
│ Secondary Alarm Phone:  [+1______________]                       │
│ Daily Report Phone:     [+1______________]                       │
│                                                                   │
│ Note: Include country code (e.g., +1 for USA)                   │
└─────────────────────────────────────────────────────────────────┘
```

#### Hologram.io Configuration
```
┌─────────────────────────────────────────────────────────────────┐
│ Hologram Device Key:    [________________________________]       │
│ Server Device Key:      [________________________________]       │
│                                                                   │
│ Note: Get these keys from your Hologram.io dashboard           │
└─────────────────────────────────────────────────────────────────┘
```

#### Timing and Reporting
```
┌─────────────────────────────────────────────────────────────────┐
│ Daily Report Interval (hours): [____] (e.g., 24)               │
│ Daily Report Time (24hr):      [__:__] (e.g., 05:00)           │
│                                                                   │
│ Large Decrease Threshold (inches): [____]                       │
│ Large Decrease Wait Time (hours):  [____]                       │
└─────────────────────────────────────────────────────────────────┘
```

#### Power Management
```
┌─────────────────────────────────────────────────────────────────┐
│ [ ] Enable Deep Sleep Mode                                       │
│                                                                   │
│ Normal Sleep Duration (hours):   [____]                         │
│ Short Sleep Duration (minutes):  [____]                         │
│                                                                   │
│ Note: Deep sleep saves maximum power but requires hardware wake │
└─────────────────────────────────────────────────────────────────┘
```

#### Calibration Points (Optional)
```
┌─────────────────────────────────────────────────────────────────┐
│ Add Calibration Points (Optional but Recommended):              │
│                                                                   │
│ Point 1: Sensor Reading [_______] = Height [_______] inches    │
│ Point 2: Sensor Reading [_______] = Height [_______] inches    │
│ Point 3: Sensor Reading [_______] = Height [_______] inches    │
│                                                                   │
│ [+ Add Another Point]                                            │
│                                                                   │
│ Examples:                                                         │
│ - Analog: 0.50V = 0.0", 4.50V = 120.0"                         │
│ - Current Loop: 4.0mA = 0.0", 20.0mA = 120.0"                  │
└─────────────────────────────────────────────────────────────────┘
```

---

### Action Buttons

```
┌─────────────────────────────────────────────────────────────────┐
│  [Generate Config File]  [Preview]  [Reset Form]  [Load...]    │
└─────────────────────────────────────────────────────────────────┘
```

---

### Generated Output Preview

```
┌─────────────────────────────────────────────────────────────────┐
│ Generated Configuration File: tank_config.cfg                   │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│ # Tank Alarm Configuration File (Per-Tank)                      │
│ # Generated: 2025-11-20 16:45:00                                │
│                                                                   │
│ # -- Required Settings --                                        │
│ SITE_NAME = North Farm                                           │
│ TANK_NUMBER = 101                                                │
│ TANK_HEIGHT_INCHES = 120                                         │
│ HIGH_ALARM_INCHES = 108                                          │
│ LOW_ALARM_INCHES = 12                                            │
│                                                                   │
│ # -- Sensor Configuration --                                     │
│ CURRENT_LOOP_CHANNEL = 0                                         │
│                                                                   │
│ # -- Contact Information --                                      │
│ ALARM_PHONE_PRIMARY = +12223334444                              │
│ ALARM_PHONE_SECONDARY = +15556667777                            │
│ DAILY_REPORT_PHONE = +18889990000                               │
│                                                                   │
│ # -- Hologram.io Keys --                                         │
│ HOLOGRAM_DEVICE_KEY = your_device_key_here                      │
│ SERVER_DEVICE_KEY = server_device_key_here                      │
│                                                                   │
│ # -- Timing and Reporting --                                     │
│ DAILY_REPORT_HOURS = 24                                          │
│ DAILY_REPORT_TIME = 05:00                                        │
│ LARGE_DECREASE_THRESHOLD = 24.0                                  │
│ LARGE_DECREASE_WAIT_HOURS = 2                                    │
│                                                                   │
│ # -- Power Management --                                         │
│ DEEP_SLEEP_MODE = false                                          │
│ NORMAL_SLEEP_HOURS = 1                                           │
│ SHORT_SLEEP_MINUTES = 10                                         │
│                                                                   │
│ # -- Calibration Points --                                       │
│ CAL_POINT = 4.0,0.0                                             │
│ CAL_POINT = 20.0,120.0                                          │
│                                                                   │
├─────────────────────────────────────────────────────────────────┤
│  [Download Config File]  [Copy to Clipboard]  [Email to Me]    │
└─────────────────────────────────────────────────────────────────┘
```

---

## Server Configuration Form (Alternative View)

When "Server Configuration" is selected, the form adapts to show server-specific options:

```
┌─────────────────────────────────────────────────────────────────┐
│ Server Configuration Generator                                   │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│ Server Device Information:                                       │
│   Hologram Device Key: [________________________________]        │
│                                                                   │
│ Data Aggregation Settings:                                       │
│   Log File Name:        [________________________]               │
│   Storage Location:     ( ) SD Card  ( ) Internal Memory        │
│                                                                   │
│ Network Configuration:                                            │
│   Server Port:          [_______]                                │
│   Max Connections:      [___]                                    │
│                                                                   │
│ Reporting Settings:                                               │
│   Summary Report Phone: [+1______________]                       │
│   Report Frequency:     [____] hours                             │
│                                                                   │
└─────────────────────────────────────────────────────────────────┘
```

---

## Features

### Input Validation
- Real-time validation of phone numbers (format: +1XXXXXXXXXX)
- Range validation for numeric inputs
- Required field indicators
- Helpful tooltips and examples

### Templates & Presets
- Load from existing config file
- Save as template for future use
- Quick setup presets for common configurations:
  - Diesel Tank (standard)
  - Water Tank (standard)
  - Custom configuration

### Export Options
- Download as .cfg file
- Download as .txt file
- Copy to clipboard
- Email configuration file
- Generate QR code for mobile setup

### Help & Documentation
- Context-sensitive help icons
- Link to full documentation
- Example configurations
- Troubleshooting tips

---

## Technical Implementation Notes

### Frontend Technologies
- HTML5 forms with validation
- CSS3 for responsive design
- JavaScript for dynamic form updates
- No server-side processing required (client-side generation)

### File Format
- Generates standard .cfg files compatible with existing SD card readers
- Preserves comment structure from template
- Validates configuration before generation

### Browser Compatibility
- Works in all modern browsers
- Mobile-responsive design
- Offline-capable (PWA potential)

---

## Usage Flow

1. **Select Configuration Type** (Client or Server)
2. **Fill in Required Fields** (marked with *)
3. **Configure Optional Settings** (expandable sections)
4. **Preview Generated Config** (live preview pane)
5. **Validate Configuration** (automatic checks)
6. **Download or Copy** (multiple export options)
7. **Copy to SD Card** (follow installation guide)

---

## Future Enhancements

- [ ] Multi-tank configuration (generate multiple configs at once)
- [ ] Bulk import from CSV/Excel
- [ ] Configuration comparison tool
- [ ] Firmware version compatibility checker
- [ ] Hardware compatibility validator
- [ ] Integration with Hologram.io API (auto-fetch device keys)
- [ ] Configuration history and versioning
- [ ] Team collaboration features

---

## Related Documentation

- [Client Installation Guide](TankAlarm-092025-Client-Hologram/INSTALLATION.md)
- [Server Installation Guide](TankAlarm-092025-Server-Hologram/INSTALLATION.md)
- [Calibration Guide](TankAlarm-092025-Client-Hologram/CALIBRATION_GUIDE.md)
- [Config Template (Client)](TankAlarm-092025-Client-Hologram/config.template.cfg)
- [Config Template (Server)](TankAlarm-092025-Server-Hologram/server_config.txt)

---

## Support

For questions or issues with the Config Generator:
- Check the [Installation Guide](TankAlarm-092025-Client-Hologram/INSTALLATION.md)
- Review the [Configuration Template](TankAlarm-092025-Client-Hologram/config.template.cfg)
- Open an issue on GitHub

---

*This is a preview/mockup document. The actual web-based config generator implementation would be built using standard web technologies (HTML/CSS/JavaScript) and hosted on GitHub Pages or a similar platform.*
