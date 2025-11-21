# Arduino Compilation Guide

This guide helps ensure Arduino sketches compile successfully in the CI workflow and locally.

## Quick Verification - 092025 (MKR NB 1500)

To verify your changes compile correctly before committing:

```bash
# Install Arduino CLI (if not already installed)
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
export PATH="$PWD/bin:$PATH"

# Setup cores and libraries
arduino-cli core update-index
arduino-cli core install arduino:samd
arduino-cli lib install "MKRNB" "SD" "Arduino Low Power" "RTCZero" "Ethernet"

# Compile Client sketch
arduino-cli compile --fqbn arduino:samd:mkrnb1500 \
  TankAlarm-092025-Client-Hologram/TankAlarm-092025-Client-Hologram.ino

# Compile Server sketch
arduino-cli compile --fqbn arduino:samd:mkrnb1500 \
  TankAlarm-092025-Server-Hologram/TankAlarm-092025-Server-Hologram.ino
```

## Quick Verification - 112025 (Arduino Opta)

To verify the 112025 sketches compile:

```bash
# Install Arduino CLI (if not already installed)
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
export PATH="$PWD/bin:$PATH"

# Setup cores and libraries for Arduino Opta
arduino-cli core update-index
arduino-cli core install arduino:mbed_opta
arduino-cli lib install "ArduinoJson@7.2.0"
arduino-cli lib install "Blues Wireless Notecard"
arduino-cli lib install "Ethernet"

# Compile Client sketch
arduino-cli compile --fqbn arduino:mbed_opta:opta \
  TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino

# Compile Server sketch
arduino-cli compile --fqbn arduino:mbed_opta:opta \
  TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino
```

**Note:** LittleFS and Wire libraries are built into the Arduino Mbed OS core and don't need separate installation.

## Common Compilation Issues

### 1. Type Name Errors

**Problem:** `'NBSMS' does not name a type; did you mean 'NB_SMS'?`

**Solution:** Use the correct type from the MKRNB library:
- ✅ Correct: `NB_SMS sms;`
- ❌ Wrong: `NBSMS sms;`

### 2. Undefined Configuration Constants

**Problem:** `'USE_HOLOGRAM_EMAIL' was not declared in this scope`

**Solution:** Ensure all configuration constants have defaults in config headers:
```cpp
// In server_config.h
#ifndef USE_HOLOGRAM_EMAIL
#define USE_HOLOGRAM_EMAIL false
#endif

#ifndef HOLOGRAM_EMAIL_RECIPIENT
#define HOLOGRAM_EMAIL_RECIPIENT ""
#endif
```

### 3. Test File Conflicts

**Problem:** `redefinition of 'void setup()'` from test files

**Solution:** Disable test files by renaming with `.disabled` extension:
- ✅ Correct: `TankAlarmServer092025-Test.ino.disabled`
- ❌ Wrong: `TankAlarmServer092025-Test.ino`

The Arduino compiler will include ALL `.ino` files in a sketch folder, causing conflicts if multiple files define the same functions.

### 4. Missing Closing Braces

**Problem:** `expected declaration before '}' token`

**Solution:** Ensure all functions and code blocks are properly closed:
```cpp
void myFunction() {
  // code here
}  // ← Don't forget this closing brace!
```

## CI Workflow

The GitHub Actions workflow (`.github/workflows/arduino-ci.yml`) automatically compiles both sketches on:
- Push to `main` or `master` branches
- Pull requests targeting `main` or `master` branches
- Changes to the Arduino sketches or the workflow file itself

### Workflow Steps

1. Install Arduino CLI
2. Install arduino:samd core
3. Install required libraries (MKRNB, SD, Arduino Low Power, RTCZero, Ethernet)
4. Compile Client sketch
5. Compile Server sketch
6. Create issue if compilation fails

## Board Specifications

- **Board:** Arduino MKR NB 1500
- **FQBN:** `arduino:samd:mkrnb1500`
- **Flash:** 262,144 bytes
- **RAM:** 32,768 bytes

### Resource Usage Guidelines

Keep resource usage reasonable:
- Flash: Stay below 80% (209,715 bytes)
- RAM: Stay below 70% (22,937 bytes) to allow runtime allocation

## Configuration Files

### 092025 Configuration (MKR NB 1500)

**Client Configuration:**
- **Template:** `TankAlarm-092025-Client-Hologram/config_template.h`
- **Active:** Include `config_template.h` for compilation (users create their own `config.h`)
- **Tracked:** Only `config_template.h` is in git (config.h is .gitignored)

**Server Configuration:**
- **Hardware Config:** `TankAlarm-092025-Server-Hologram/server_config.h` (compile-time hardware constants)
- **SD Card Config:** `TankAlarm-092025-Server-Hologram/server_config.txt` (runtime user configuration)
- **Tracked:** Hardware config.h is in git; SD card .txt template is tracked

### 112025 Configuration (Arduino Opta)

**Client Configuration:**
- **Runtime Config:** `/client_config.json` stored in LittleFS (internal flash)
- **Default config created automatically** on first boot
- **Product UID:** Update `PRODUCT_UID` define in `.ino` file to match Blues Notehub project
- **No SD card required**

**Server Configuration:**
- **Runtime Config:** `/server_config.json` stored in LittleFS (internal flash)
- **Default config created automatically** on first boot
- **Product UID:** Update `SERVER_PRODUCT_UID` define in `.ino` file to match Blues Notehub project
- **No SD card required**

## Troubleshooting

### Local Compilation Works but CI Fails

1. Check you're using the same Arduino CLI version
2. Verify library versions match (see workflow file)
3. Ensure all includes use correct paths
4. Check for platform-specific code

### CI Passes but Board Compilation Fails

1. Library version mismatch - CI uses latest, board may have older
2. Missing hardware-specific libraries
3. Different board variant (check FQBN)

## Common Compilation Issues - 112025 (Arduino Opta)

### 1. ArduinoJson Version Mismatch

**Problem:** `no matching function for call to 'JsonDocument::JsonDocument(int)'`

**Solution:** Ensure you have ArduinoJson version 7.x or later:
- ArduinoJson 7.x uses `JsonDocument` instead of `DynamicJsonDocument`
- Install via Library Manager: Search "ArduinoJson" and install version 7.2.0 or later
- Version 6.x will not work with the 112025 code

### 2. Notecard Library Missing

**Problem:** `Notecard.h: No such file or directory`

**Solution:** Install Blues Wireless Notecard library:
- Open Library Manager
- Search: "Notecard"
- Install: "Blues Wireless Notecard by Blues Inc."

### 3. LittleFS Not Found

**Problem:** `LittleFS.h: No such file or directory`

**Solution:** LittleFS is built into Arduino Mbed OS core:
- Go to **Tools → Board → Boards Manager**
- Search: "Arduino Mbed OS Opta Boards"
- Install the package
- LittleFS will be automatically available

### 4. Wrong Board Selected

**Problem:** `#error "This sketch is designed for Arduino Opta"`

**Solution:** Ensure correct board is selected:
- Go to **Tools → Board → Arduino Mbed OS Opta Boards**
- Select **Arduino Opta**
- FQBN should be: `arduino:mbed_opta:opta`

### 5. Ethernet Library Issues

**Problem:** Ethernet-related compilation errors (server only)

**Solution:** 
- Ethernet library should be built-in
- If missing, install via Library Manager: "Ethernet by Arduino"
- Ensure you're compiling the server, not the client (client doesn't use Ethernet)

## Best Practices

1. **Always test locally** before pushing
2. **Use correct type names** from library documentation
3. **Define configuration defaults** for all constants
4. **Disable test files** with `.disabled` extension
5. **Check resource usage** after adding features
6. **Verify closing braces** match opening ones
7. **Keep sketches modular** with clear function separation
8. **For 112025:** Ensure ArduinoJson 7.x is installed (not 6.x)
9. **For 112025:** Verify Mbed OS Opta core is installed for LittleFS support

## Additional Resources

### 092025 (MKR NB 1500)
- [Arduino CLI Documentation](https://arduino.github.io/arduino-cli/)
- [MKRNB Library Reference](https://www.arduino.cc/reference/en/libraries/mkrnb/)
- [Arduino MKR NB 1500 Documentation](https://docs.arduino.cc/hardware/mkr-nb-1500)

### 112025 (Arduino Opta)
- [Arduino Opta Documentation](https://docs.arduino.cc/hardware/opta)
- [Blues Wireless Notecard Library](https://dev.blues.io/tools-and-sdks/firmware-libraries/arduino-library/)
- [ArduinoJson v7 Documentation](https://arduinojson.org/v7/)
- [Arduino Mbed OS Documentation](https://docs.arduino.cc/learn/programming/mbed-os-basics)
- [Client Installation Guide](../TankAlarm-112025-Client-BluesOpta/INSTALLATION.md)
- [Server Installation Guide](../TankAlarm-112025-Server-BluesOpta/INSTALLATION.md)
