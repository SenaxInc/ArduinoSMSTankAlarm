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

## Build Firmware Binaries Workflow

**File:** `build-firmware-112025.yml`

### Purpose
Automatically builds firmware binary files (.bin) for the TankAlarm-112025 project and commits them to the repository for easy deployment.

### Triggers
The workflow runs on:
- **Push events** to `main` or `master` branches when changes are made to:
  - `TankAlarm-112025-Client-BluesOpta/` directory
  - `TankAlarm-112025-Server-BluesOpta/` directory
  - `TankAlarm-112025-Common/` directory
  - The workflow file itself (`.github/workflows/build-firmware-112025.yml`)

### What It Does

1. **Sets up the build environment**
   - Checks out the repository with full history (`fetch-depth: 0`)
   - Installs Arduino CLI
   - Installs Arduino Mbed OS Opta Boards core

2. **Installs required libraries for 112025**
   - Ethernet
   - ArduinoJson
   - Blues Wireless Notecard
   - ArduinoRS485
   - ArduinoModbus

3. **Builds firmware binaries**
   - **Client firmware:** `firmware/112025/client/TankAlarm-112025-Client-BluesOpta.ino.bin`
   - **Server firmware:** `firmware/112025/server/TankAlarm-112025-Server-BluesOpta.ino.bin`
   - Also generates bootloader versions: `*.ino.with_bootloader.bin`

4. **Commits and pushes binaries**
   - Automatically commits new firmware binaries if changes detected
   - Uses rebase strategy with conflict resolution for binary files
   - Pushes changes back to the branch that triggered the workflow

### Binary File Conflict Resolution

The workflow uses the `-X theirs` merge strategy option during rebase to automatically resolve conflicts in binary firmware files. This prevents build failures when multiple builds update firmware concurrently.

**How it works:**
- During `git pull --rebase -X theirs`, git applies the merge strategy with the `-X theirs` option so it keeps the newly built firmware (from the rebased commit) instead of the older version
- This is the correct behavior since we always want the latest build
- No manual intervention required for binary file conflicts

**Why this is needed:**
- Binary files cannot be automatically merged by git like text files
- When two builds run in quick succession, both try to update the same `.bin` files
- Without this strategy option, the rebase would fail with a merge conflict
- The `-X theirs` strategy option tells git to prefer the version from the commit being rebased (the new build) when conflicts occur

### Concurrency Control

The workflow uses concurrency settings to serialize builds:
- **Concurrency group:** `firmware-build-${{ github.ref_name }}`
- **Cancel-in-progress:** `false` (builds run to completion, queued sequentially)

This ensures builds for the same branch run one at a time, minimizing conflicts.

### Viewing Results

After the workflow runs:
1. Check the **Actions** tab for build status
2. Download firmware binaries from the `firmware/112025/` directory
3. Review build logs for compilation statistics (memory usage, etc.)

### Using the Firmware Binaries

The compiled binaries can be uploaded to Arduino Opta devices using:
- **Arduino IDE:** Tools → Programmer → DFU Mode, then File → Upload
- **Arduino CLI:** `arduino-cli upload -b arduino:mbed_opta:opta -i firmware.bin`
- **Bootloader versions:** Use `*.with_bootloader.bin` for fresh devices

### Troubleshooting

**Workflow fails to push:**
- Check that the `GITHUB_TOKEN` has write permissions
- Verify that the branch is not protected or has appropriate rules

**Rebase conflicts:**
- The workflow automatically resolves binary file conflicts using `-X theirs`
- If the workflow still fails, check logs for non-binary conflicts (e.g., workflow file changes)

**Build failures:**
- Review compilation errors in the workflow logs
- Check that all required libraries are properly installed
- Verify sketch syntax and Arduino Mbed OS core version

### Maintenance

To modify the workflow:
1. Edit `.github/workflows/build-firmware-112025.yml`
2. Test changes in a branch before merging to ensure the rebase strategy still works
3. Monitor the Actions tab after deployment

## Website Screenshots Workflow

**File:** `update-screenshots.yml`

### Purpose
Automatically generates and updates screenshots of the web interfaces served by TankAlarm-112025-Server-BluesOpta and TankAlarm-112025-Viewer-BluesOpta.

### Triggers
The workflow runs on:
- **Scheduled** - Weekly on Mondays at 2 AM UTC
- **Push events** to `main` or `master` branches when changes are made to:
  - `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
  - `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino`
  - The workflow file itself
- **Manual trigger** via workflow_dispatch

### What It Does

1. **Extracts HTML from Arduino code**
   - Parses the .ino files to extract embedded HTML content
   - Extracts CSS styles and HTML pages using Python regex
   - Supports multi-part HTML string literals

2. **Takes screenshots using Playwright**
   - Sets up headless Chromium browser
   - Captures screenshots of all web pages at high resolution (1920x1080)
   - Handles different viewport sizes for different pages
   
   **Server pages captured (original HTTP routes in parentheses):**
   - Dashboard (route `/`)
   - Client Console (route `/client-console`)
   - Config Generator (route `/config-generator`)
   - Serial Monitor (route `/serial-monitor`)
   - Calibration (route `/calibration`)
   - Contacts Manager (route `/contacts`)
   - Server Settings (route `/server-settings`)
   - Historical Data (route `/historical`)
   
   These routes refer to the original server endpoints in the Arduino web UI. During this workflow, no HTTP server is started: Playwright loads the extracted HTML files directly from the filesystem using `file://` URLs. CSS is inlined into each HTML file to ensure proper styling.
   
   **Viewer pages captured (original HTTP routes in parentheses):**
   - Dashboard (route `/`)

3. **Updates documentation**
   - Updates `WEBSITE_PREVIEW.md` files with current timestamp
   - Commits and pushes screenshot files automatically

4. **Saves screenshots**
   - Server screenshots: `TankAlarm-112025-Server-BluesOpta/screenshots/*.png`
   - Viewer screenshots: `TankAlarm-112025-Viewer-BluesOpta/screenshots/*.png`

### Viewing Results

After the workflow runs:
1. Check the **Actions** tab for workflow execution status
2. View updated screenshots in the screenshots folders
3. Check `WEBSITE_PREVIEW.md` files for the latest timestamp

### How Screenshots Are Generated

The workflow uses a three-stage process:

1. **HTML Extraction** - Python script parses Arduino .ino files and extracts HTML content from C++ string literals (R"HTML(...)HTML" format)

2. **Browser Automation** - Playwright launches headless Chromium and loads the extracted HTML files as local files

3. **Screenshot Capture** - Full-page screenshots are taken at appropriate viewport sizes and saved to the screenshots directories

### Customizing Screenshots

To add or modify screenshots:

1. **Add a new page:**
   - In `.github/workflows/update-screenshots.yml`, edit the heredoc JavaScript in the **"Create screenshot script"** step (which writes `/tmp/take-screenshots.js` during the workflow run) to add a new entry to the `screenshots` array
   - Specify the HTML file, output path, and viewport size

2. **Change viewport sizes:**
   - Modify the `viewport` property for each screenshot entry in the workflow YAML file

3. **Add page interactions:**
   - If needed in the future, add custom logic before taking screenshots (e.g., clicking buttons, filling forms)

### Troubleshooting

**Screenshots look broken or incomplete:**
- Check that the HTML extraction step completed successfully
- Verify that the HTML files were created in `/tmp/html-files`
- Ensure the page has time to load (the script uses `page.waitForLoadState('load')`; you can change to 'networkidle' or add additional waits if needed)

**Workflow fails to commit:**
- Check that the `GITHUB_TOKEN` has write permissions
- Verify that the screenshots actually changed

**Missing screenshots:**
- Check the workflow logs for HTML extraction errors
- Verify that the HTML variable names match those in the Arduino code

### Maintenance

To modify the workflow:
1. Edit `.github/workflows/update-screenshots.yml`
2. Test changes in a branch before merging
3. Monitor the Actions tab after deployment
4. Use workflow_dispatch to trigger manual runs for testing
