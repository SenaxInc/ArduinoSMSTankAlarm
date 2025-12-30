# TankAlarm-112025-Common

Shared library for TankAlarm 112025 Server, Client, and Viewer components.

## Overview

This library provides common constants, utility functions, platform abstractions, and Notecard helpers that are shared across all TankAlarm 112025 components.

## Installation

### Option 1: Symlink (Recommended for Development)

Create a symlink in your Arduino libraries folder:

**Windows (PowerShell as Admin):**
```powershell
New-Item -ItemType Junction -Path "$env:USERPROFILE\Documents\Arduino\libraries\TankAlarm-112025-Common" -Target "C:\path\to\ArduinoSMSTankAlarm\TankAlarm-112025-Common"
```

**Linux/macOS:**
```bash
ln -s /path/to/ArduinoSMSTankAlarm/TankAlarm-112025-Common ~/Arduino/libraries/TankAlarm-112025-Common
```

### Option 2: Copy

Copy the entire `TankAlarm-112025-Common` folder to your Arduino libraries folder:
- Windows: `%USERPROFILE%\Documents\Arduino\libraries\`
- Linux: `~/Arduino/libraries/`
- macOS: `~/Documents/Arduino/libraries/`

## Usage

Include the main header in your sketch:

```cpp
#include <TankAlarm_Common.h>
```

Or include specific headers:

```cpp
#include <TankAlarm_Platform.h>   // Platform detection and abstractions
#include <TankAlarm_Utils.h>      // Utility functions
#include <TankAlarm_Notecard.h>   // Notecard helper functions
```

## Components

### TankAlarm_Common.h
Main header that includes all other headers. Also defines:
- `FIRMWARE_VERSION` - Version string
- `FIRMWARE_BUILD_DATE` - Build date
- `NOTECARD_I2C_ADDRESS` - Default 0x17
- `NOTECARD_I2C_FREQUENCY` - Default 400kHz
- `MAX_TANK_RECORDS` - Default 64
- `WATCHDOG_TIMEOUT_SECONDS` - Default 30
- Notefile names (TELEMETRY_FILE, ALARM_FILE, etc.)

### TankAlarm_Platform.h
Platform detection and abstractions:
- PROGMEM compatibility for non-AVR platforms
- Filesystem availability detection
- Watchdog macros (`TANKALARM_WATCHDOG_KICK`, `TANKALARM_WATCHDOG_START`)
- POSIX file I/O helpers (Mbed OS only)
- `MbedWatchdogHelper` class

### TankAlarm_Utils.h
Utility functions:
- `strlcpy()` - Safe string copy (for platforms without it)
- `tankalarm_roundTo()` - Round float to decimal places
- `tankalarm_computeNextAlignedEpoch()` - Schedule aligned tasks
- `tankalarm_isValidEpoch()` - Validate epoch timestamp
- `tankalarm_streq()` - Null-safe string comparison
- `tankalarm_isEmptyString()` - Check for null/empty string

### TankAlarm_Notecard.h
Notecard helper functions:
- `tankalarm_ensureTimeSync()` - Sync time from Notecard
- `tankalarm_currentEpoch()` - Get current epoch from last sync
- `tankalarm_setNotecardI2CSpeed()` - Configure I2C speed
- `tankalarm_getNotecardUUID()` - Get device UUID
- `tankalarm_configureHub()` - Configure hub.set
- `tankalarm_getNotecardStatus()` - Get connectivity status

## Example

```cpp
#include <TankAlarm_Common.h>
#include <Wire.h>
#include <Notecard.h>

Notecard notecard;
double gLastSyncedEpoch = 0.0;
unsigned long gLastSyncMillis = 0;

#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
MbedWatchdogHelper mbedWatchdog;
#endif

void setup() {
  Serial.begin(115200);
  
  Wire.begin();
  Wire.setClock(NOTECARD_I2C_FREQUENCY);
  
  notecard.begin(NOTECARD_I2C_ADDRESS);
  tankalarm_setNotecardI2CSpeed(notecard, NOTECARD_I2C_FREQUENCY);
  
  // Sync time
  tankalarm_ensureTimeSync(notecard, gLastSyncedEpoch, gLastSyncMillis, true);
  
  // Get device UUID
  char uuid[48];
  if (tankalarm_getNotecardUUID(notecard, uuid, sizeof(uuid))) {
    Serial.print("Device UUID: ");
    Serial.println(uuid);
  }
  
  // Start watchdog
  #ifdef TANKALARM_WATCHDOG_AVAILABLE
    TANKALARM_WATCHDOG_START(mbedWatchdog, WATCHDOG_TIMEOUT_SECONDS * 1000);
  #endif
}

void loop() {
  // Periodic time sync
  tankalarm_ensureTimeSync(notecard, gLastSyncedEpoch, gLastSyncMillis);
  
  // Get current time
  double now = tankalarm_currentEpoch(gLastSyncedEpoch, gLastSyncMillis);
  
  // Kick watchdog
  #ifdef TANKALARM_WATCHDOG_AVAILABLE
    TANKALARM_WATCHDOG_KICK(mbedWatchdog);
  #endif
  
  delay(1000);
}
```

## Dependencies

- [Blues Wireless Notecard](https://github.com/blues/note-arduino) - Notecard Arduino library

## License

Copyright (c) 2025 Senax Inc. All rights reserved.
