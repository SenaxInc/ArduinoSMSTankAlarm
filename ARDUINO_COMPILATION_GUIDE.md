# Arduino Compilation Guide

This guide helps ensure Arduino sketches compile successfully in the CI workflow and locally.

## Quick Verification

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

### Client Configuration

- **Template:** `TankAlarm-092025-Client-Hologram/config_template.h`
- **Active:** Include `config_template.h` for compilation (users create their own `config.h`)
- **Tracked:** Only `config_template.h` is in git (config.h is .gitignored)

### Server Configuration  

- **Hardware Config:** `TankAlarm-092025-Server-Hologram/server_config.h` (compile-time hardware constants)
- **SD Card Config:** `TankAlarm-092025-Server-Hologram/server_config.txt` (runtime user configuration)
- **Tracked:** Hardware config.h is in git; SD card .txt template is tracked

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

## Best Practices

1. **Always test locally** before pushing
2. **Use correct type names** from library documentation
3. **Define configuration defaults** for all constants
4. **Disable test files** with `.disabled` extension
5. **Check resource usage** after adding features
6. **Verify closing braces** match opening ones
7. **Keep sketches modular** with clear function separation

## Additional Resources

- [Arduino CLI Documentation](https://arduino.github.io/arduino-cli/)
- [MKRNB Library Reference](https://www.arduino.cc/reference/en/libraries/mkrnb/)
- [Arduino MKR NB 1500 Documentation](https://docs.arduino.cc/hardware/mkr-nb-1500)
