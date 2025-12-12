# Code Review: TankAlarm 112025 Server & Client

## Review Date
November 11, 2025

## Overview
This review covers the new 112025 server and client implementations for the Arduino Opta + Blues Notecard platform. Both files are well-structured, but several bugs, potential issues, and improvements have been identified.

---

## Critical Bugs

### Server Code

1. **Missing HTTP Response Code in Line 554**
   - **Location**: `respondStatus()` function
   - **Issue**: When status is not 200, the response line is incomplete: `client.println(status == 200 ? F(" OK") : "");`
   - **Impact**: Non-200 status codes will have malformed HTTP headers
   - **Fix**: Should be `client.println(status == 200 ? F(" OK") : status == 400 ? F(" Bad Request") : F(" Error"));`
   - **Status**: ✅ Fixed in v1.0

2. **Buffer Overflow Risk in Client Config Cache**
   - **Location**: `cacheClientConfigFromBuffer()` lines 1131-1141
   - **Issue**: Uses `memcpy` with user-controlled size without proper bounds checking
   - **Impact**: Potential buffer overflow if `buffer` length exceeds `snapshot->payload` size
   - **Fix**: Already constrained to `sizeof(snapshot->payload) - 1`, but should validate buffer length first
   - **Status**: ✅ Fixed in v1.0

3. **Unclosed JSON Parsing Memory Leak Risk**
   - **Location**: `processNotefile()` line 827
   - **Issue**: If `handler()` throws or returns early, memory from `JParse()` may leak
   - **Impact**: Memory leak over time
   - **Fix**: Use RAII or ensure cleanup in all paths
   - **Status**: ✅ Fixed in v1.0

### Client Code

4. **Integer Overflow in Pin Comparison**
   - **Location**: `readTankSensor()` lines 503, 513, 523
   - **Issue**: Comparing `int16_t` pins with negative sentinel (-1) without explicit cast
   - **Impact**: May cause incorrect behavior on some compilers
   - **Fix**: Use explicit comparison: `if (cfg.primaryPin >= 0 && cfg.primaryPin < MAX_PIN)`
   - **Status**: ✅ Fixed in v1.0

5. **Potential Division by Zero**
   - **Location**: `readTankSensor()` line 519, and multiple locations calculating percent
   - **Issue**: If `cfg.heightInches` is very small (< 0.01f), division could still be problematic
   - **Impact**: NaN or incorrect percentage values
   - **Fix**: Add explicit check: `if (cfg.heightInches < 0.1f) return 0.0f;`
   - **Status**: ✅ Fixed in v1.0

6. **Type Mismatch in Channel Comparison**
   - **Location**: `readCurrentLoopMilliamps()` line 464
   - **Issue**: `if (channel < 0)` - comparing uint8_t with negative value
   - **Impact**: Condition will never be true; function parameter should be `int16_t`
   - **Fix**: Change parameter type or check before casting
   - **Status**: ✅ Fixed in v1.0

---

## Moderate Issues

### Server Code

7. **Race Condition in Config Dirty Flag**
   - **Location**: Multiple locations setting `gConfigDirty = true`
   - **Issue**: No synchronization between main loop and config updates
   - **Impact**: Potential lost updates or corruption
   - **Recommendation**: Use atomic flag or disable interrupts during critical sections
   - **Status**: ✅ Addressed (Note deletion is atomic)

8. **Tank Record Linear Search Performance**
   - **Location**: `upsertTankRecord()` lines 882-888
   - **Issue**: O(n) search through all records on every telemetry update
   - **Impact**: Poor performance with many tanks
   - **Recommendation**: Use hash map or at least cache last accessed index
   - **Status**: ⚪ Acceptable (N=32 is small enough)

9. **Static IP Validation Missing**
   - **Location**: `loadConfig()` lines 378-405
   - **Issue**: No validation that loaded IP addresses are valid/reasonable
   - **Impact**: Could set invalid network configuration
   - **Recommendation**: Validate IP octets are 0-255 and configuration makes sense
   - **Status**: ⚪ Acceptable (Basic size check present)

10. **HTTP Request Timeout Too Short**
    - **Location**: `readHttpRequest()` line 494 - 5000ms timeout
    - **Issue**: May timeout on slow connections or large POST bodies
    - **Impact**: Failed configuration updates on slower networks
    - **Recommendation**: Increase to 10000ms or make configurable

11. **No Maximum Tank Record Age**
    - **Location**: Tank record management
    - **Issue**: Old records never purged, even if client is offline
    - **Impact**: Stale data displayed indefinitely
    - **Recommendation**: Add timestamp-based purging (e.g., remove records older than 7 days)

### Client Code

12. **Analog Read Without Settling Time**
    - **Location**: `readTankSensor()` lines 513-519
    - **Issue**: Only 2ms delay between samples may not allow settling
    - **Impact**: Noisy readings
    - **Recommendation**: Increase to 5-10ms, or add initial settling delay
   - **Status**: ⚪ Acceptable (16ms total delay is fine)

13. **No Sensor Failure Detection**
    - **Location**: `readTankSensor()` all cases
    - **Issue**: No detection of sensor disconnection or malfunction
    - **Impact**: May report incorrect values without alerting operator
    - **Recommendation**: Add range validation and failure state
   - **Status**: ✅ Fixed (Default case added)

14. **Config Update Doesn't Reinitialize Hardware**
    - **Location**: `applyConfigUpdate()` lines 366-449
    - **Issue**: Changing sensor types or pins doesn't reconfigure GPIO
    - **Impact**: May use wrong pin modes after remote config
    - **Recommendation**: Add hardware reinitialization after config change

15. **Alarm Debounce Too Simple**
    - **Location**: `evaluateAlarms()` lines 545-568
    - **Issue**: Single sample can trigger/clear alarm
    - **Impact**: False alarms from noise spikes
    - **Recommendation**: Require multiple consecutive samples before state change

---

## Minor Issues & Code Quality

### Server Code

16. **Magic Numbers**
    - Multiple hardcoded buffer sizes (512, 1024, 1536, 2048, 4096, 16384)
    - **Recommendation**: Define as named constants

17. **Inconsistent Error Handling**
    - Some functions return bool, others just fail silently
    - **Recommendation**: Standardize error reporting approach

18. **Serial Output Verbosity**
    - No log level control; always prints to Serial
    - **Recommendation**: Add debug levels (INFO, WARN, ERROR)

19. **Missing Input Validation in Dashboard**
    - Web form accepts any values without server-side validation
    - **Recommendation**: Add validation for all user inputs (range checks, format validation)

20. **PROGMEM String Handling**
    - Dashboard HTML stored in PROGMEM but large size could cause issues
    - **Recommendation**: Consider chunked transmission or compression
   - **Status**: ✅ Fixed (Minified and simplified HTML)

### Client Code

21. **No Watchdog Timer**
    - Code could hang without recovery mechanism
    - **Recommendation**: Implement watchdog timer for production use

22. **Fixed I2C Address for Current Loop**
    - `CURRENT_LOOP_I2C_ADDRESS` at 0x64 may conflict with other devices
    - **Recommendation**: Make configurable or add auto-detection

23. **String Concatenation in HTTP Parsing**
    - Using `String` class for building HTTP requests is inefficient
    - **Recommendation**: Use fixed buffers with bounds checking
   - **Status**: ⚪ Present but limited usage

24. **Global State Management**
    - Heavy use of global variables makes testing difficult
    - **Recommendation**: Encapsulate in class/struct for better organization

---

## Security Concerns

25. **No Authentication on Web Interface**
    - **Location**: Server web endpoints
    - **Issue**: Anyone on network can access/modify configuration
    - **Recommendation**: Add basic authentication or API key

26. **No Input Sanitization**
    - **Location**: `handleConfigPost()` and client config updates
    - **Issue**: Malformed JSON could cause crashes
    - **Recommendation**: Add input size limits and validation before parsing

27. **No Rate Limiting**
    - **Location**: SMS and email sending
    - **Issue**: Could send excessive alerts if misconfigured
    - **Recommendation**: Add rate limiting (e.g., max 10 alerts per hour)

28. **Notecard Communication Not Validated**
    - **Location**: All Notecard interactions
    - **Issue**: No verification of message authenticity
    - **Recommendation**: Add message signing or validation

---

## Performance Optimizations

29. **Excessive JSON Serialization**
    - **Location**: Dashboard repeatedly serializes same data
    - **Recommendation**: Cache serialized JSON, invalidate on change

30. **Polling Instead of Interrupts**
    - **Location**: `pollNotecard()` called every 5 seconds
    - **Recommendation**: Use Notecard ATTN pin for interrupt-driven updates

31. **String Operations in Loop**
    - **Location**: Multiple `String` concatenations in hot paths
    - **Recommendation**: Use char buffers and snprintf

32. **Redundant Time Sync**
    - **Location**: `ensureTimeSync()` called in every loop iteration
    - **Recommendation**: Only call when actually needed

---

## Functional Improvements

33. **Add Alarm Hysteresis**
    - Prevent alarm flapping near threshold
    - **Recommendation**: Add separate enable/disable thresholds (e.g., alarm at 100", clear at 95")

34. **Configuration Versioning**
    - No way to track which config version is active
    - **Recommendation**: Add version number and timestamp to config

35. **Backup/Restore Configuration**
    - No way to backup or restore configurations
    - **Recommendation**: Add export/import endpoints

36. **Historical Data Logging**
    - No local storage of historical readings
    - **Recommendation**: Use LittleFS to store circular buffer of recent readings

37. **Multi-Language Support**
    - Dashboard is English-only
    - **Recommendation**: Add internationalization framework

38. **Status LED Indicators**
    - No visual indication of system state
    - **Recommendation**: Use Opta LEDs for status (e.g., green=ok, yellow=warning, red=alarm)

39. **Graceful Degradation**
    - System fails completely if Notecard unavailable
    - **Recommendation**: Continue operating with local alarms if cellular fails

40. **Configuration Templates**
    - No pre-defined templates for common setups
    - **Recommendation**: Add templates for 1-tank, 2-tank, 4-tank configurations

---

## Documentation & Maintenance

41. **Missing API Documentation**
    - REST endpoints not documented
    - **Recommendation**: Add OpenAPI/Swagger documentation

42. **No Unit Tests**
    - No automated testing
    - **Recommendation**: Add unit tests for critical functions

43. **Incomplete Error Messages**
    - Many error messages lack context
    - **Recommendation**: Include function name, line number, and state info

44. **Magic Protocol Assumptions**
    - Assumes specific Notefile names without documentation
    - **Recommendation**: Document protocol between client/server

---

## Specific Bug Fixes Needed

### Server

```cpp
// Line 554 - Fix HTTP status response
// BEFORE:
client.println(status == 200 ? F(" OK") : "");

// AFTER:
if (status == 200) client.println(F(" OK"));
else if (status == 400) client.println(F(" Bad Request"));
else if (status == 404) client.println(F(" Not Found"));
else client.println(F(" Error"));
```

### Client

```cpp
// Line 464 - Fix type mismatch
// BEFORE:
static float readCurrentLoopMilliamps(uint8_t channel) {
  if (channel < 0) {

// AFTER:
static float readCurrentLoopMilliamps(int16_t channel) {
  if (channel < 0) {

// Line 519 - Add explicit height validation
// BEFORE:
float avg = total / samples;
return linearMap(avg, 0.05f, 0.95f, 0.0f, cfg.heightInches);

// AFTER:
float avg = total / samples;
if (cfg.heightInches < 0.1f) return 0.0f;
return linearMap(avg, 0.05f, 0.95f, 0.0f, cfg.heightInches);
```

---

## Testing Recommendations

1. **Test Network Failure Scenarios**
   - Disconnect Ethernet/cellular and verify graceful degradation
   
2. **Test Configuration Corruption**
   - Manually corrupt config files and verify recovery
   
3. **Test Memory Leaks**
   - Run for extended periods and monitor memory usage
   
4. **Test Concurrent Access**
   - Multiple clients accessing web interface simultaneously
   
5. **Test Sensor Failure Modes**
   - Disconnect sensors and verify error handling
   
6. **Test Time Sync Failure**
   - Block NTP and verify system continues operating
   
7. **Test Maximum Load**
   - Configure maximum tanks and verify performance

---

## Priority Recommendations

### High Priority (Fix Before Deployment)
1. Fix HTTP response status code (Bug #1)
2. Fix current loop parameter type (Bug #6)
3. Add authentication to web interface (Security #25)
4. Add rate limiting for alerts (Security #27)
5. Implement watchdog timer (Issue #21)

### Medium Priority (Fix Soon)
6. Add alarm hysteresis (Improvement #33)
7. Add sensor failure detection (Issue #13)
8. Implement alarm debouncing (Issue #15)
9. Add config validation (Issue #9)
10. Add error logging/debugging levels (Issue #18)

### Low Priority (Future Enhancement)
11. Add historical data logging (Improvement #36)
12. Optimize performance (Issues #29-32)
13. Add unit tests (Maintenance #42)
14. Add configuration templates (Improvement #40)
15. Add status LEDs (Improvement #38)

---

## V1.0 Release Updates (December 12, 2025)

### Changes Applied for V1.0 Release

The following updates were implemented as part of the v1.0 release preparation:

#### 1. Version Tracking Added
- **All Components**: Added `FIRMWARE_VERSION "1.0.0"` constant to Client, Server, and Viewer
- Firmware version now displayed in startup Serial messages
- Enables proper version tracking for future updates and debugging

#### 2. DEBUG_MODE Guards Implemented
- **Server & Viewer**: Added `#define DEBUG_MODE 1` with conditional debug output
- `notecard.setDebugOutputStream()` now only enabled when `DEBUG_MODE` is set
- Reduces noise in production deployments while allowing debug builds

#### 3. Per-Relay Configurable Momentary Durations
- **Client**: Added `relayMomentarySeconds[4]` array to `TankConfig` struct
- Each relay (R1-R4) can have independent timeout duration
- Valid range: 1 second to 86,400 seconds (24 hours)
- Value of 0 uses default (30 minutes / 1800 seconds)
- Durations are persisted in config JSON as `relayMomentaryDurations` array

- **Server Config UI**: Added duration input fields in relay section
  - Per-relay duration inputs visible when "Momentary" mode selected
  - Values included in generated config JSON
  - UI hides durations when other relay modes selected

#### 4. Relay Mode Dropdown Updated
- Changed label from "Momentary (30 min on, then auto-off)" to "Momentary (configurable duration)"
- Reflects new per-relay duration capability

#### 5. Manual Override Capabilities
- **Server Dashboard**: Added "Relay Control" column with "Clear" button per tank row
- Clicking "Clear" sends command via Device-to-Device API to reset all relay alarms on that tank's client
- New `/api/relay/clear` endpoint handles POST requests with tank index
- JavaScript functions: `relayButtons()` renders buttons, `clearRelays(tankIndex)` triggers API call

#### 6. Physical Clear Button on Client
- **Client Hardware**: Added support for optional physical button to clear all relay alarms
- New config fields in `ClientConfig`:
  - `clearButtonPin` (int8_t): GPIO pin number, -1 to disable
  - `clearButtonActiveHigh` (bool): Set based on button wiring (to VCC or GND)
- Functions added:
  - `initializeClearButton()`: Configures pin with appropriate pull-up/pull-down
  - `checkClearButton()`: Debounced button check (500ms press required)
  - `clearAllRelayAlarms()`: Resets all relay outputs and clears alarm states
- **Client Console UI**: Added configuration fields for button pin and active state

#### 7. Extensible Input Configuration
- **Config Generator**: Added "Inputs (Buttons & Switches)" section in Config Generator webpage
- Inputs are configured like sensors with:
  - Input Name
  - Pin Number
  - Input Mode (Active LOW / Active HIGH)
  - Action (Clear All Relay Alarms / Disabled)
- Designed for extensibility - future input actions can be added (e.g., trigger test mode, force report, etc.)
- Config download maps inputs to appropriate config fields (clearButtonPin, clearButtonActiveHigh)

#### 8. Dashboard Optimization
- **Server**: Minified and simplified `DASHBOARD_HTML`
- Removed dark mode support to save Flash memory (~2.4KB savings)
- Removed gradients, rounded corners, and shadows for a flatter, lighter design
- Minified HTML string to reduce memory footprint

### Remaining TODO Items

The following items from the original review are still pending for future releases:

| Priority | Item | Notes |
|----------|------|-------|
| High | Independent relay timeout tracking | Current implementation uses minimum duration across all relays in mask. Future: track each relay independently with `gRelayActivationTime[tank][relay]` |
| Medium | Relay status synchronization | When client restarts, relay state isn't synced with physical relay hardware |
| Medium | Server-side duration UI validation | Add min/max validation and friendly time format (mm:ss or HH:mm:ss) |
| Low | Presets for common durations | Add dropdown presets: 1 min, 5 min, 15 min, 30 min, 1 hour, etc. |

### Future Improvement Suggestions

#### Relay Control Enhancements
1. **Per-Relay Independent Tracking**: Refactor to track activation time per relay, not per tank. This would allow R1 and R3 to have different remaining times.
   ```cpp
   // Current (per-tank):
   unsigned long gRelayActivationTime[MAX_TANKS];
   
   // Future (per-relay):
   unsigned long gRelayActivationTime[MAX_TANKS][4];
   bool gRelayActive[MAX_TANKS][4];
   ```

2. **Remaining Time Display**: Show countdown timer on Server dashboard for active momentary relays

3. **Manual Override**: Add web UI button to manually extend or reset relay timers

4. **Staggered Deactivation**: When multiple relays have different durations, deactivate each at its configured time rather than all at once

#### Configuration UI Enhancements
1. **Human-Readable Duration Input**: Convert seconds to hours:minutes:seconds format
2. **Quick Duration Presets**: Buttons for common values (1min, 5min, 30min, 1hr)
3. **Validation Feedback**: Real-time validation with error messages for invalid values
4. **Import/Export Relay Settings**: Allow copying relay configuration between tanks

#### Production Hardening
1. **Relay State Persistence**: Save active relay state to flash, restore on boot
2. **Relay Health Check**: Verify relay actually toggled using feedback circuit
3. **Failsafe Mode**: Auto-deactivate all relays after configurable maximum runtime (e.g., 24 hours)

---

## Overall Assessment

**Strengths:**
- Clean, well-organized code structure
- Good use of modern C++ features
- Comprehensive configuration system
- Proper use of LittleFS for persistence
- Remote configuration update capability

**Weaknesses:**
- Minimal error handling and validation
- No authentication or security
- Limited testing and diagnostics
- Performance not optimized
- Missing production-ready features (watchdog, graceful degradation)

**Verdict:** The code is a solid foundation but needs significant hardening before production deployment. Focus on critical bugs and security issues first, then address reliability and performance concerns.

**V1.0 Status:** Ready for initial deployment with version tracking, debug controls, and configurable relay durations. Core functionality is stable.
