# Security Considerations and Advanced Features

## Issue #8: FTP Password Security

### Current Implementation
The FTP password is stored in plaintext in the `ServerConfig` structure and saved to LittleFS:

```cpp
struct ServerConfig {
  // ...
  char ftpPass[32];  // Stored in plaintext in LittleFS
};
```

### Security Considerations
- **Local Storage**: The password is stored on the device's flash memory
- **Physical Access**: Anyone with physical access to the device could extract the password
- **Network Transmission**: The FTP password is only used for LAN backup/restore operations

### Using Notecard Environment Variables for Secure Storage

The Blues Notecard provides a more secure option through environment variables, which are stored on the Notehub cloud and synchronized to the device. This offers several advantages:

1. **Cloud-Managed**: Passwords aren't stored on the device flash
2. **Encrypted Transport**: Data is encrypted between Notehub and the device
3. **Easy Rotation**: Change credentials without reflashing the device

#### Implementation Example

```cpp
// Retrieve FTP password from Notecard environment variable
static bool getFtpPasswordFromNotecard(char *password, size_t maxLen) {
  J *req = notecard.newRequest("env.get");
  if (!req) return false;
  
  JAddStringToObject(req, "name", "ftp_password");
  
  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) return false;
  
  const char *text = JGetString(rsp, "text");
  if (text && strlen(text) < maxLen) {
    strlcpy(password, text, maxLen);
    notecard.deleteResponse(rsp);
    return true;
  }
  
  notecard.deleteResponse(rsp);
  return false;
}

// Set environment variable on Notehub (one-time setup via API or web UI)
// Or use env.set from the device:
static void setFtpPasswordOnNotecard(const char *password) {
  J *req = notecard.newRequest("env.set");
  if (!req) return;
  
  J *body = JCreateObject();
  JAddStringToObject(body, "ftp_password", password);
  JAddItemToObject(req, "body", body);
  
  notecard.sendRequest(req);
}
```

#### Setting Environment Variables on Notehub

1. Go to your project on [Notehub](https://notehub.io)
2. Navigate to **Settings > Environment**
3. Add a new variable:
   - **Name**: `ftp_password`
   - **Value**: Your FTP password
4. The variable will sync to your devices automatically

#### Security Best Practices
- Use Notehub environment variables for sensitive credentials
- Consider obfuscating passwords in local storage if cloud storage isn't possible
- Limit FTP access to trusted LAN segments
- Use unique passwords per deployment
- Regularly rotate credentials

---

## Issue #9: Unit Testing Approach

### Testing the Shared Library (TankAlarm_Common)

The `TankAlarm-112025-Common` library is well-suited for unit testing as it contains pure logic functions without hardware dependencies.

#### Recommended Testing Framework

**PlatformIO with Unity** (works well with Arduino):
```ini
; platformio.ini
[env:native]
platform = native
test_framework = unity
build_flags = -std=c++11
```

#### Test Examples

##### Testing `tankalarm_computeNextAlignedEpoch()`

```cpp
// test/test_time_alignment/test_main.cpp
#include <unity.h>
#include "tankalarm_common.h"

void test_next_aligned_epoch_same_day(void) {
    // Test: Current time 01:00, target 05:00 -> should be 05:00 same day
    double currentEpoch = 1704070800;  // 2024-01-01 01:00:00 UTC
    double result = tankalarm_computeNextAlignedEpoch(currentEpoch, 5, 0);
    
    // Expected: 2024-01-01 05:00:00 UTC = 1704085200
    TEST_ASSERT_EQUAL_DOUBLE(1704085200, result);
}

void test_next_aligned_epoch_next_day(void) {
    // Test: Current time 06:00, target 05:00 -> should be 05:00 next day
    double currentEpoch = 1704088800;  // 2024-01-01 06:00:00 UTC
    double result = tankalarm_computeNextAlignedEpoch(currentEpoch, 5, 0);
    
    // Expected: 2024-01-02 05:00:00 UTC = 1704171600
    TEST_ASSERT_EQUAL_DOUBLE(1704171600, result);
}

void test_next_aligned_epoch_edge_midnight(void) {
    // Test: Current time 23:59, target 00:00 -> should be 00:00 next day
    double currentEpoch = 1704153540;  // 2024-01-01 23:59:00 UTC
    double result = tankalarm_computeNextAlignedEpoch(currentEpoch, 0, 0);
    
    // Expected: 2024-01-02 00:00:00 UTC = 1704153600
    TEST_ASSERT_EQUAL_DOUBLE(1704153600, result);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_next_aligned_epoch_same_day);
    RUN_TEST(test_next_aligned_epoch_next_day);
    RUN_TEST(test_next_aligned_epoch_edge_midnight);
    return UNITY_END();
}
```

##### Testing JSON Parsing/Serialization

```cpp
#include <unity.h>
#include <ArduinoJson.h>

void test_tank_config_serialization(void) {
    StaticJsonDocument<512> doc;
    doc["site"] = "North Field";
    doc["monitors"][0]["id"] = "A";
    doc["monitors"][0]["name"] = "Diesel Tank";
    doc["monitors"][0]["highAlarm"] = 90.0;
    
    String output;
    serializeJson(doc, output);
    
    TEST_ASSERT_TRUE(output.indexOf("North Field") > 0);
    TEST_ASSERT_TRUE(output.indexOf("Diesel Tank") > 0);
}

void test_config_parsing_missing_fields(void) {
    StaticJsonDocument<256> doc;
    const char* json = "{\"site\":\"Test\"}";
    
    DeserializationError err = deserializeJson(doc, json);
    TEST_ASSERT_EQUAL(DeserializationError::Ok, err);
    
    // Test default handling for missing fields
    int reportHour = doc["reportHour"] | 5;  // Default to 5
    TEST_ASSERT_EQUAL(5, reportHour);
}
```

##### Mocking Notecard Responses

```cpp
// Create a mock notecard for testing
class MockNotecard {
public:
    const char* mockResponse = nullptr;
    
    J* requestAndResponse(J* req) {
        JDelete(req);
        return mockResponse ? JParse(mockResponse) : nullptr;
    }
};

void test_notecard_time_sync(void) {
    MockNotecard mock;
    mock.mockResponse = "{\"time\":1704085200}";
    
    // Test your time sync logic with the mock
}
```

#### Running Tests

```bash
# With PlatformIO
pio test -e native

# Or for specific test
pio test -e native -f test_time_alignment
```

---

## Issue #10: OTA Update Options for Arduino Opta

### ✅ IMPLEMENTED: Blues Notecard DFU

The Blues Notecard DFU (Device Firmware Update) has been fully implemented for both Client and Server.

### How It Works

**Automatic Updates (Default)**
- System checks for firmware updates every hour
- When an update is available on Notehub, it's automatically downloaded and applied
- Device reboots after successful update

**Manual Control (Optional)**
- Comment out the auto-enable lines in `loop()` to require manual triggering
- Use the web API (Server) or serial commands to trigger updates

### Implementation Details

#### 1. Firmware Version Tracking

Version is defined in `TankAlarm-112025-Common/src/TankAlarm_Common.h`:
```cpp
#define FIRMWARE_VERSION "1.1.1"
#define FIRMWARE_BUILD_DATE __DATE__
```

#### 2. DFU State Variables

Both Client and Server track:
```cpp
static unsigned long gLastDfuCheckMillis = 0;
#define DFU_CHECK_INTERVAL_MS 3600000UL  // Check every hour
static bool gDfuUpdateAvailable = false;
static char gDfuVersion[32] = {0};
static bool gDfuInProgress = false;
```

#### 3. Core Functions

**`checkForFirmwareUpdate()`**
- Queries Notecard for available updates
- Logs when new firmware is detected
- Updates `gDfuUpdateAvailable` and `gDfuVersion`

**`enableDfuMode()`**
- Saves pending configuration
- Sends `dfu.mode` request to Notecard
- Device downloads firmware and reboots automatically

#### 4. Main Loop Integration

```cpp
// Runs every hour
if (now - gLastDfuCheckMillis > DFU_CHECK_INTERVAL_MS) {
  gLastDfuCheckMillis = now;
  if (!gDfuInProgress && gNotecardAvailable) {
    checkForFirmwareUpdate();
    // Auto-enable DFU if update available
    if (gDfuUpdateAvailable) {
      enableDfuMode();  // Comment this line for manual control
    }
  }
}
```

#### 5. Server Web API

**GET `/api/dfu/status`**
Returns:
```json
{
  "currentVersion": "1.1.1",
  "buildDate": "Jan  7 2026",
  "updateAvailable": true,
  "availableVersion": "1.1.1",
  "dfuInProgress": false
}
```

**POST `/api/dfu/enable`**
Requires PIN authentication:
```json
{
  "pin": "1234"
}
```
Response:
```json
{
  "success": true,
  "message": "DFU mode enabled - device will update and restart"
}
```

### Using Notehub to Deploy Firmware

#### Step 1: Build Your Firmware

1. Open the project in Arduino IDE or PlatformIO
2. Update `FIRMWARE_VERSION` in `TankAlarm_Common.h`
3. Build the project
4. Locate the compiled `.bin` file:
   - Arduino IDE: Look in `/tmp/arduino_build_*/` 
   - PlatformIO: `.pio/build/<env>/firmware.bin`

**Finding the .bin file in Arduino IDE 2.x:**
```
Sketch > Export Compiled Binary
```
The `.bin` file will be in your sketch folder.

#### Step 2: Upload to Notehub

1. Go to your project on [Notehub.io](https://notehub.io)
2. Click **Devices > Firmware**
3. Click **Upload Firmware**
4. Fill in the form:
   - **Type**: `stm32` (for Arduino Opta)
   - **Version**: `1.1.1` (must match FIRMWARE_VERSION)
   - **Target**: Choose specific devices, fleets, or products
   - **Binary**: Upload your `.bin` file
5. Click **Upload**

#### Step 3: Deployment Options

**Option A: Automatic Deployment**
- Set **Deploy Immediately** = Yes
- Devices will auto-update within 1 hour (next DFU check)

**Option B: Staged Rollout**
- Deploy to a test device first
- Monitor for 24-48 hours
- Expand to full fleet if successful

**Option C: Manual Trigger** (if auto-enable is disabled)
- Navigate to server dashboard
- Go to Settings or System section  
- Click "Check for Updates"
- Confirm update when prompted

#### Step 4: Monitor Update Progress

**Via Notehub:**
- Go to **Events** tab
- Filter by device ID
- Look for `dfu.status` and `dfu.mode` events

**Via Serial Monitor:**
```
========================================
FIRMWARE UPDATE AVAILABLE: v1.1.1
Current version: 1.1.1
Device will auto-update on next check
========================================
```

**Via Web Dashboard (Server):**
```bash
curl http://192.168.1.100/api/dfu/status
```

#### Step 5: Verify Update

After reboot, check:
1. Serial output shows new version number
2. Notehub Events show successful DFU completion
3. Web API returns new `currentVersion`

### Troubleshooting

**Update Not Detected**
- Check Notehub deployment target matches device UID
- Verify Notecard has cellular connectivity
- Force check: `curl -X POST http://192.168.1.100/api/dfu/enable -d '{"pin":"1234"}'`

**Update Fails Mid-Download**
- Notecard will retry automatically
- Check cellular signal strength
- Large binaries may take 10-15 minutes on slow connections

**Device Won't Reboot After Update**
- Watchdog timer will force reset after 30 seconds
- If stuck, power cycle the device
- Previous firmware remains if update fails validation

### Security Considerations

**PIN Protection**: DFU web API requires PIN authentication (server only)

**Firmware Signing**: Blues Notecard validates firmware signatures automatically

**Rollback**: If updated firmware crashes on boot, Notecard can revert to previous version (requires Notehub configuration)

### Disabling Auto-Update

To require manual approval:

1. Open `TankAlarm-112025-Server-BluesOpta.ino` or `TankAlarm-112025-Client-BluesOpta.ino`
2. Find the DFU check in `loop()`:
```cpp
if (gDfuUpdateAvailable) {
  enableDfuMode();  // <- Comment out this line
}
```
3. Recompile and upload
4. Updates will now require manual trigger via web API or serial command

### Example: Full Fleet Update

```bash
# 1. Check current status
curl http://server-ip/api/dfu/status

# 2. Upload new firmware to Notehub (via web UI)

# 3. Monitor for update availability (within 1 hour)
watch -n 60 'curl -s http://server-ip/api/dfu/status | jq .updateAvailable'

# 4. If manual mode, trigger update
curl -X POST http://server-ip/api/dfu/enable \
  -H "Content-Type: application/json" \
  -d '{"pin":"your-pin"}'

# 5. Wait for reboot (2-5 minutes)

# 6. Verify new version
curl http://server-ip/api/dfu/status | jq .currentVersion
```

### Alternative OTA Options

While Blues Notecard DFU is now implemented and recommended, other options remain available:

**Arduino Cloud**
- Requires Arduino Cloud subscription
- Good for non-cellular deployments
- Web-based firmware management

**Custom HTTP OTA** 
- For LAN-only environments
- Requires custom server implementation
- More development work

**USB DFU**
- For factory/maintenance updates
- Double-press reset button → STM32CubeProgrammer
- Good for initial provisioning

---

## Summary of Changes Made (January 2026)

| Issue | Fix Applied | File(s) Modified |
|-------|-------------|------------------|
| #1 Operator Precedence | Added parentheses around OR condition | Client .ino |
| #3 Hash Table Assert | Added static_assert for size validation | Server .ino |
| #4 Input Validation | Added length checks for client UID | Server .ino |
| #5 I2C Address | Made runtime configurable via config.json | Client .ino |
| #6 Memory Leak | Changed deleteResponse to JDelete | Server .ino |
| #7 Race Condition | Added atomic access helpers | Client .ino |
| #8 FTP Security | Documented Notecard env vars | This document |
| #9 Unit Tests | Documented testing approach | This document |
| #10 OTA Updates | ✅ **IMPLEMENTED Blues DFU** | Client, Server & Viewer .ino |

### DFU Implementation Files Changed
- `TankAlarm-112025-Common/src/TankAlarm_Common.h` - Version constants
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`:
  - Added DFU state variables
  - Implemented `checkForFirmwareUpdate()`
  - Implemented `enableDfuMode()`
  - Added hourly DFU check to `loop()`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`:
  - Added DFU state variables
  - Implemented `checkForFirmwareUpdate()`
  - Implemented `enableDfuMode()`
  - Added hourly DFU check to `loop()`
  - Added `/api/dfu/status` GET endpoint
  - Added `/api/dfu/enable` POST endpoint (PIN-protected)
- `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino`:
  - Added DFU state variables
  - Implemented `checkForFirmwareUpdate()`
  - Implemented `enableDfuMode()`
  - Added hourly DFU check to `loop()`
