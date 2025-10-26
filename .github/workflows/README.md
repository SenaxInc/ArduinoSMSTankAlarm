# GitHub Actions Workflows

This directory contains automated CI/CD workflows for the ArduinoSMSTankAlarm project.

## Arduino CI Workflow

**File:** `arduino-ci.yml`

### Purpose
Automatically compiles the Arduino code for the TankAlarm-092025-Client-Hologram project
to ensure code quality and catch compilation errors early.

### Triggers
The workflow runs on:
- **Push events** to `main` or `master` branches when changes are made to:
  - `TankAlarm-092025-Client-Hologram/` directory
  - The workflow file itself
- **Pull requests** targeting `main` or `master` branches
- **Manual trigger** via workflow_dispatch

### What It Does

1. **Sets up the environment**
   - Checks out the repository code
   - Installs Arduino CLI
   - Installs Arduino SAMD core for MKR boards

2. **Installs required libraries**
   - MKRNB (cellular connectivity)
   - SD (SD card operations)
   - ArduinoLowPower (power management)
   - RTCZero (real-time clock)

3. **Compiles the sketch**
   - Compiles `TankAlarm-092025-Client-Hologram.ino`
   - Target board: Arduino MKR NB 1500 (`arduino:samd:mkrnb1500`)

4. **Handles compilation failures**
   - If compilation fails, automatically creates a GitHub issue
   - Assigns the issue to the copilot user
   - Labels the issue with: `arduino`, `compilation-error`, `bug`
   - Mentions @copilot in the issue body for notifications
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

# Install the SAMD core
arduino-cli core update-index
arduino-cli core install arduino:samd

# Install required libraries
arduino-cli lib install "MKRNB"
arduino-cli lib install "SD"
arduino-cli lib install "ArduinoLowPower"
arduino-cli lib install "RTCZero"

# Compile the sketch
arduino-cli compile --fqbn arduino:samd:mkrnb1500 \
  TankAlarm-092025-Client-Hologram/TankAlarm-092025-Client-Hologram.ino
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
