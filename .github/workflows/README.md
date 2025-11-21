# GitHub Actions Workflows

This directory contains automated CI/CD workflows for the ArduinoSMSTankAlarm project.

## Arduino CI Workflow

**File:** `arduino-ci.yml`

### Purpose
Automatically compiles the Arduino code for both TankAlarm-092025 and TankAlarm-112025 projects
to ensure code quality and catch compilation errors early.

### Triggers
The workflow runs on:
- **Push events** to `main` or `master` branches when changes are made to:
  - `TankAlarm-092025-Client-Hologram/` directory
  - `TankAlarm-092025-Server-Hologram/` directory
  - `TankAlarm-112025-Client-BluesOpta/` directory
  - `TankAlarm-112025-Server-BluesOpta/` directory
  - `TankAlarm-112025-Viewer-BluesOpta/` directory
  - The workflow file itself
- **Pull requests** targeting `main` or `master` branches
- **Manual trigger** via workflow_dispatch

### What It Does

1. **Sets up the environment**
   - Checks out the repository code
   - Installs Arduino CLI
   - Installs Arduino SAMD core for MKR boards
   - Installs Arduino Mbed OS Opta Boards core for Arduino Opta

2. **Installs required libraries**
   - MKRNB (cellular connectivity)
   - SD (SD card operations)
   - ArduinoLowPower (power management)
   - RTCZero (real-time clock)
   - Ethernet (network connectivity for server)
   - ArduinoJson (JSON parsing for configuration)
   - Blues Wireless Notecard (cellular connectivity via Blues)

3. **Compiles the sketches**
   
   **TankAlarm-092025 (Arduino MKR NB 1500):**
   - Compiles `TankAlarm-092025-Client-Hologram.ino`
   - Compiles `TankAlarm-092025-Server-Hologram.ino`
   - Target board: Arduino MKR NB 1500 (`arduino:samd:mkrnb1500`)
   
   **TankAlarm-112025 (Arduino Opta):**
   - Compiles `TankAlarm-112025-Client-BluesOpta.ino`
   - Compiles `TankAlarm-112025-Server-BluesOpta.ino`
   - Compiles `TankAlarm-112025-Viewer-BluesOpta.ino`
   - Target board: Arduino Opta (`arduino:mbed_opta:opta`)

4. **Handles compilation failures**
   - If any compilation fails, automatically creates a GitHub issue
   - Assigns the issue to the copilot user
   - Labels the issue with: `arduino`, `compilation-error`, `bug`
   - Mentions @copilot in the issue body for notifications
   - Shows which sketch(es) failed with ❌ and which passed with ✅
   - Prevents duplicate issues by checking for existing open issues
   - Adds comments to existing issues if they're already open

### Issue Management

When a compilation error occurs, the workflow:
- **Creates a new issue** if no similar open issue exists
- **Updates existing issue** if a similar issue is already open
- **Issue is automatically assigned** to the copilot user
- **Issue content includes**:
  - Link to the failed workflow run
  - Commit SHA and branch information
  - Details about the sketch and board
  - Next steps for resolution
  - Mentions @copilot in the body for notifications

### Viewing Results

After the workflow runs, you can:
1. View the workflow run in the **Actions** tab
2. Check the compilation output in the workflow logs
3. Review any created issues in the **Issues** tab

### Local Testing

You can test Arduino compilation locally using Arduino CLI:

```bash
# Install Arduino CLI
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# Install cores
arduino-cli core update-index
arduino-cli core install arduino:samd
arduino-cli core install arduino:mbed_opta

# Install required libraries
arduino-cli lib update-index
arduino-cli lib install "MKRNB"
arduino-cli lib install "SD"
arduino-cli lib install "ArduinoLowPower"
arduino-cli lib install "RTCZero"
arduino-cli lib install "Ethernet"
arduino-cli lib install "ArduinoJson"
arduino-cli lib install "Blues Wireless Notecard"

# Compile TankAlarm-092025 sketches
arduino-cli compile --fqbn arduino:samd:mkrnb1500 \
  TankAlarm-092025-Client-Hologram/TankAlarm-092025-Client-Hologram.ino

arduino-cli compile --fqbn arduino:samd:mkrnb1500 \
  TankAlarm-092025-Server-Hologram/TankAlarm-092025-Server-Hologram.ino

# Compile TankAlarm-112025 sketches
arduino-cli compile --fqbn arduino:mbed_opta:opta \
  TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino

arduino-cli compile --fqbn arduino:mbed_opta:opta \
  TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino

arduino-cli compile --fqbn arduino:mbed_opta:opta \
  TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino
```

### Troubleshooting

**Workflow fails to create issue:**
- Check that the GITHUB_TOKEN has appropriate permissions
- Verify that Issues are enabled in the repository settings

**Compilation fails:**
- Review the workflow logs for detailed error messages
- Ensure all required libraries are installed
- Verify the sketch syntax and includes

**Duplicate issues:**
- The workflow automatically prevents duplicates by searching for open issues
  with the label `arduino` and `compilation-error`
- Close resolved issues to prevent future updates to them

### Maintenance

To modify the workflow:
1. Edit `.github/workflows/arduino-ci.yml`
2. Test changes in a branch before merging
3. Monitor the Actions tab after deployment
