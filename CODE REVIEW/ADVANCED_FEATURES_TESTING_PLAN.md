# Advanced Features Testing Plan

## Test Environment Setup

### Requirements
- Arduino Opta board running TankAlarm Server firmware
- Blues Notecard with active Notehub connection
- At least 2 client devices (real or simulated)
- Modern web browser (Chrome, Firefox, Safari, or Edge)
- Network access to server web interface

### Test Data Preparation
1. **Create test configurations:**
   - Simple single-sensor config
   - Complex multi-sensor config (3+ sensors)
   - Config with alarms and relays
   - Config with SMS alerts

2. **Export baseline JSON files:**
   ```
   Standard_Tank.json
   Multi_Sensor_Gas.json
   Complex_Relay_Setup.json
   ```

---

## Test Suite

### 1. Tooltip System Tests

#### Test 1.1: Tooltip Display on Hover
**Steps:**
1. Open Client Console
2. Hover mouse over Product UID `?` icon
3. Observe tooltip appears
4. Move mouse away
5. Observe tooltip disappears

**Expected Result:**
- Tooltip appears within 100ms of hover
- Displays: "Blues Notehub Product UID. This is set fleet-wide..."
- Tooltip positioned above field
- Text wraps naturally, max width 280px
- Tooltip disappears when mouse leaves

**Pass/Fail:** ☐

#### Test 1.2: Tooltip Display on Keyboard Focus
**Steps:**
1. Open Client Console
2. Press Tab repeatedly until Power Source `?` icon is focused
3. Observe focus ring appears
4. Press Enter or Space
5. Observe tooltip

**Expected Result:**
- Focus ring visible on `?` icon
- Tooltip appears on Enter/Space press
- Tooltip content: "Select the primary power source..."
- Accessible via keyboard only

**Pass/Fail:** ☐

#### Test 1.3: Tooltip on Mobile Touch
**Steps:**
1. Open Client Console on mobile device
2. Tap Input Mode `?` icon
3. Observe tooltip

**Expected Result:**
- Tooltip appears on tap
- Readable on small screen
- No tooltip overlap with other fields
- Can dismiss by tapping elsewhere

**Pass/Fail:** ☐

#### Test 1.4: All Tooltips Present
**Steps:**
1. Verify `?` icon present for:
   - Product UID
   - Power Source
   - Switch Mode (in digital sensor cards)
   - 4-20mA Sensor Type (in current loop sensor cards)
   - Input Mode
   - Relay Mode
   - Target Client UID
   - Clear Relay Action

**Expected Result:**
- All 8+ tooltip locations have `?` icons
- Each tooltip has unique, helpful content
- No duplicate or empty tooltips

**Pass/Fail:** ☐

---

### 2. Import JSON Tests

#### Test 2.1: Import Valid Configuration
**Steps:**
1. Click "Download JSON" to export current config
2. Click "Import JSON"
3. Select the just-downloaded file
4. Observe form population

**Expected Result:**
- File picker opens with `.json` filter
- Form populates with all values from file
- Sensors appear in correct order
- Alarms/relays/SMS preserved
- Toast: "Configuration loaded from [filename]"
- Button shows "Send Configuration *"

**Pass/Fail:** ☐

#### Test 2.2: Import Invalid JSON (Syntax Error)
**Steps:**
1. Create malformed JSON file (missing bracket)
2. Click "Import JSON"
3. Select malformed file

**Expected Result:**
- Error toast: "Failed to import: [error message]"
- Form unchanged
- No JavaScript console errors
- File input cleared

**Pass/Fail:** ☐

#### Test 2.3: Import Invalid JSON (Schema Error)
**Steps:**
1. Create valid JSON but missing required fields (no "site")
2. Click "Import JSON"
3. Select file

**Expected Result:**
- Error toast: "Invalid JSON: Site Name is required"
- Form unchanged
- Validation runs before applying changes

**Pass/Fail:** ☐

#### Test 2.4: Import Complex Configuration
**Steps:**
1. Create config with:
   - 3 sensors (digital, analog, 4-20mA)
   - 2 alarms per sensor
   - Relay control on sensor 1
   - SMS alert on sensor 2
   - 2 input buttons
2. Download as JSON
3. Clear form (select different client)
4. Import the JSON file

**Expected Result:**
- All 3 sensors restored correctly
- All alarms present with thresholds
- Relay configuration intact
- SMS phones and messages correct
- Both inputs restored
- No data loss

**Pass/Fail:** ☐

#### Test 2.5: Import Twice (Replace)
**Steps:**
1. Import "Standard_Tank.json"
2. Immediately import "Multi_Sensor_Gas.json"

**Expected Result:**
- First import populates form
- Second import replaces all fields
- No mixing of configs
- Final state matches second file exactly

**Pass/Fail:** ☐

---

### 3. Unsaved Changes Tests

#### Test 3.1: Mark Unsaved on Edit
**Steps:**
1. Load a client config
2. Observe button: "Send Configuration" (normal)
3. Edit Site Name field
4. Observe button changes

**Expected Result:**
- Button updates to "Send Configuration *"
- Button becomes bold (font-weight: 600)
- `state.hasUnsavedChanges === true`

**Pass/Fail:** ☐

#### Test 3.2: Browser Warning on Close
**Steps:**
1. Load client config
2. Edit any field (make unsaved)
3. Attempt to close browser tab/window
4. Observe browser warning

**Expected Result:**
- Browser shows native warning dialog
- Message: "You have unsaved configuration changes..."
- Can cancel to stay on page
- Can proceed to leave (loses changes)

**Pass/Fail:** ☐

#### Test 3.3: Confirmation on Client Switch
**Steps:**
1. Load Client A
2. Edit field (make unsaved)
3. Click dropdown and select Client B

**Expected Result:**
- Confirmation dialog: "You have unsaved changes. Switch clients anyway?"
- Cancel keeps Client A selected, preserves edits
- OK switches to Client B, loses edits
- Dropdown reverts if canceled

**Pass/Fail:** ☐

#### Test 3.4: Clear Unsaved on Send
**Steps:**
1. Load client config
2. Edit field (make unsaved)
3. Click "Send Configuration *"
4. Confirm send in comparison dialog
5. Wait for success toast

**Expected Result:**
- After successful send, button reverts to "Send Configuration"
- Bold style removed
- `state.hasUnsavedChanges === false`
- No browser warning if now trying to close

**Pass/Fail:** ☐

#### Test 3.5: Unsaved Triggers
**Steps:**
Test each trigger individually:
1. Edit text input (Site Name)
2. Edit number input (Sample Seconds)
3. Edit select (Power Source)
4. Add new sensor
5. Remove sensor
6. Add alarm section
7. Add relay section
8. Add SMS section
9. Import JSON file

**Expected Result:**
- Each action marks form as unsaved
- Asterisk appears on button
- Browser warning active

**Pass/Fail:** ☐

---

### 4. Configuration Comparison Tests

#### Test 4.1: Simple Modification
**Steps:**
1. Load client with highAlarm=100
2. Change highAlarm to 80
3. Click "Send Configuration"
4. Observe comparison dialog

**Expected Result:**
- Dialog shows: "Modified: 1 field"
- Can click OK to proceed or Cancel to abort
- If canceled, form unchanged, can re-edit

**Pass/Fail:** ☐

#### Test 4.2: Multiple Changes
**Steps:**
1. Load client config
2. Change Site Name, Sample Seconds, Report Hour
3. Click "Send Configuration"

**Expected Result:**
- Dialog shows: "Modified: 3 fields"
- All changes tracked
- Confirmation required

**Pass/Fail:** ☐

#### Test 4.3: Add New Sensor
**Steps:**
1. Load client with 1 sensor
2. Click "Add Sensor"
3. Configure Sensor #2
4. Click "Send Configuration"

**Expected Result:**
- Dialog shows: "Added: X fields" (where X = all new sensor fields)
- Comparison detects new sensor
- Can proceed or cancel

**Pass/Fail:** ☐

#### Test 4.4: Remove Sensor
**Steps:**
1. Load client with 2 sensors
2. Click "Remove" on Sensor #2
3. Click "Send Configuration"

**Expected Result:**
- Dialog shows: "Removed: X fields"
- Detects deletion
- Warns before sending

**Pass/Fail:** ☐

#### Test 4.5: No Changes
**Steps:**
1. Load client config
2. Don't edit anything
3. Click "Send Configuration"

**Expected Result:**
- Either: Comparison shows "No changes detected" and proceeds
- Or: No comparison dialog (optional optimization)
- Config sends successfully

**Pass/Fail:** ☐

#### Test 4.6: Complex Change Mix
**Steps:**
1. Load client with 2 sensors
2. Modify Sensor #1 alarm (modify)
3. Add Sensor #3 (add)
4. Remove Sensor #2 (remove)
5. Click "Send Configuration"

**Expected Result:**
- Dialog shows all three categories:
  - Modified: [count]
  - Added: [count]  
  - Removed: [count]
- Summary accurate
- User can review before confirming

**Pass/Fail:** ☐

---

### 5. Audit Log Tests

#### Test 5.1: Log on Successful Send
**Steps:**
1. Load client "dev:864475044012345"
2. Edit field
3. Send configuration successfully
4. Check browser console: `console.log(state.configHistory)`

**Expected Result:**
- New entry in configHistory array
- Entry contains:
  - timestamp (recent Unix milliseconds)
  - clientUid: "dev:864475044012345"
  - changes: {modified: [...], added: [], removed: []}
  - user: "admin"

**Pass/Fail:** ☐

#### Test 5.2: Log Persists Across Refresh
**Steps:**
1. Send 2 configs (creating 2 log entries)
2. Refresh browser page
3. Check console: `console.log(state.configHistory)`

**Expected Result:**
- Both entries still present
- Data loaded from localStorage
- Timestamps and details intact

**Pass/Fail:** ☐

#### Test 5.3: Log Auto-Prunes at 50 Entries
**Steps:**
1. Send 52 configurations (simulated)
2. Check configHistory length

**Expected Result:**
- Array length capped at 50
- Oldest 2 entries removed
- Most recent 50 preserved
- FIFO (first in, first out) behavior

**Pass/Fail:** ☐

#### Test 5.4: Log Tracks Multiple Clients
**Steps:**
1. Send config to Client A
2. Send config to Client B
3. Send config to Client A again
4. Check configHistory

**Expected Result:**
- 3 entries total
- Each has correct clientUid
- Chronological order preserved
- Can filter by client UID

**Pass/Fail:** ☐

#### Test 5.5: Log Survives Browser Close
**Steps:**
1. Send config (creates log entry)
2. Close browser completely
3. Reopen browser
4. Navigate to Client Console
5. Check configHistory

**Expected Result:**
- localStorage persists across sessions
- Log entry still present
- No data loss

**Pass/Fail:** ☐

---

### 6. Integration Tests

#### Test 6.1: Import → Edit → Compare → Send → Log
**Steps:**
1. Import JSON file
2. Edit 1 field
3. Click Send
4. Review comparison (shows 1 modified)
5. Confirm send
6. Check audit log

**Expected Result:**
- Import marks unsaved
- Edit keeps unsaved flag
- Comparison accurate
- Send logs to history
- Unsaved flag clears
- Complete workflow success

**Pass/Fail:** ☐

#### Test 6.2: Tooltip → Edit → Unsaved → Cancel
**Steps:**
1. Hover tooltip to understand field
2. Edit that field based on tooltip guidance
3. Observe unsaved indicator
4. Try to switch clients
5. Cancel switch confirmation

**Expected Result:**
- Tooltip provides helpful info
- User makes informed edit
- Unsaved warning prevents data loss
- Edits preserved

**Pass/Fail:** ☐

#### Test 6.3: Download → Modify → Import → Compare
**Steps:**
1. Download current config as "baseline.json"
2. Manually edit JSON in text editor (change 1 value)
3. Import modified JSON
4. Click Send
5. Observe comparison

**Expected Result:**
- Comparison shows difference between current form state and imported state
- Detects the 1 modified value
- Workflow enables external JSON editing

**Pass/Fail:** ☐

---

### 7. Edge Cases & Error Handling

#### Test 7.1: Import During Unsaved Changes
**Steps:**
1. Load Client A
2. Edit field (unsaved)
3. Import JSON file (different config)

**Expected Result:**
- Import replaces all fields
- Unsaved flag remains (new imported config not sent yet)
- No data corruption
- User can send imported config

**Pass/Fail:** ☐

#### Test 7.2: Concurrent Client Editing
**Steps:**
1. Open Client Console in Tab 1
2. Open Client Console in Tab 2 (same browser)
3. Edit Client A in Tab 1
4. Edit Client A in Tab 2
5. Send from Tab 1
6. Refresh Tab 2

**Expected Result:**
- Each tab independent
- Sends don't interfere
- Refresh shows latest server state
- Audit log shows both sends

**Pass/Fail:** ☐

#### Test 7.3: Large Configuration Import
**Steps:**
1. Create config with 8 sensors (max Opta inputs)
2. Each sensor has alarms, relays, SMS
3. 4 input buttons
4. Download as JSON
5. Import JSON

**Expected Result:**
- Import completes in <2 seconds
- All 8 sensors rendered
- All config details preserved
- No performance degradation
- Form usable after import

**Pass/Fail:** ☐

#### Test 7.4: PIN Expiration During Edit
**Steps:**
1. Enter PIN to unlock form
2. Edit fields (unsaved)
3. Wait for PIN to expire (90 days - simulated)
4. Click Send

**Expected Result:**
- PIN modal re-appears
- Unsaved changes preserved
- After re-entering PIN, can send
- No data loss

**Pass/Fail:** ☐

#### Test 7.5: Network Error During Send
**Steps:**
1. Edit config
2. Disconnect network
3. Click Send
4. Observe error handling

**Expected Result:**
- Error toast: "Failed to send config"
- Unsaved flag remains
- Config status shows error
- Can retry after reconnecting

**Pass/Fail:** ☐

---

### 8. Accessibility Tests

#### Test 8.1: Screen Reader Compatibility
**Tools:** NVDA (Windows) or VoiceOver (Mac)

**Steps:**
1. Navigate Client Console with screen reader
2. Tab to tooltip icon
3. Activate tooltip (Enter/Space)
4. Hear tooltip content
5. Tab to Import button
6. Activate and select file
7. Hear import success announcement

**Expected Result:**
- All tooltips announced with context
- Button labels clear ("Import JSON", not just "Import")
- Form field labels associated correctly
- Status messages (toasts) announced
- No unlabeled elements

**Pass/Fail:** ☐

#### Test 8.2: Keyboard-Only Navigation
**Steps:**
1. Disconnect mouse
2. Use only Tab, Enter, Space, Arrow keys
3. Navigate entire form
4. Activate all tooltips
5. Import JSON
6. Send config

**Expected Result:**
- All interactive elements reachable
- Focus visible (outline) on all focusable elements
- Can complete entire workflow keyboard-only
- No keyboard traps

**Pass/Fail:** ☐

#### Test 8.3: High Contrast Mode
**Steps:**
1. Enable OS high contrast mode
2. Open Client Console
3. Observe tooltips, buttons, indicators

**Expected Result:**
- Tooltips readable in high contrast
- Asterisk visible on unsaved button
- Dialog borders visible
- No reliance on color alone

**Pass/Fail:** ☐

---

### 9. Cross-Browser Tests

#### Test 9.1: Chrome
- [ ] All 8 feature groups pass
- [ ] No console errors
- [ ] UI renders correctly

#### Test 9.2: Firefox
- [ ] All 8 feature groups pass
- [ ] No console errors
- [ ] UI renders correctly

#### Test 9.3: Safari
- [ ] All 8 feature groups pass
- [ ] No console errors
- [ ] UI renders correctly

#### Test 9.4: Edge
- [ ] All 8 feature groups pass
- [ ] No console errors
- [ ] UI renders correctly

---

### 10. Performance Tests

#### Test 10.1: Import Speed
**Steps:**
1. Prepare JSON with 5 sensors
2. Time import operation (click Import → form populated)

**Expected Result:**
- Import completes in <500ms
- No UI freeze
- Smooth form population

**Pass/Fail:** ☐ Time: _____ms

#### Test 10.2: Comparison Speed
**Steps:**
1. Load config with 50+ fields
2. Modify 10 fields
3. Time comparison (click Send → dialog appears)

**Expected Result:**
- Comparison runs in <200ms
- Dialog appears instantly
- No lag

**Pass/Fail:** ☐ Time: _____ms

#### Test 10.3: Audit Log Write
**Steps:**
1. Send config
2. Measure time for log entry to write to localStorage

**Expected Result:**
- Write completes in <50ms
- Non-blocking
- No user-visible delay

**Pass/Fail:** ☐ Time: _____ms

---

## Bug Reporting Template

```
### Bug Report

**Feature:** [Tooltip / Import / Unsaved / Comparison / Audit]

**Severity:** [Critical / High / Medium / Low]

**Browser:** [Chrome 120 / Firefox 121 / Safari 17 / Edge 120]

**Steps to Reproduce:**
1. 
2. 
3. 

**Expected Behavior:**


**Actual Behavior:**


**Screenshots:**
[Attach if applicable]

**Console Errors:**
```
[Paste errors]
```

**Additional Context:**

```

---

## Test Summary Report Template

```
### Advanced Features Test Summary

**Test Date:** _____________
**Tester:** _____________
**Environment:** 
  - Server Firmware Version: _____________
  - Browser: _____________
  - Device: _____________

**Results:**

| Feature Group           | Tests | Passed | Failed | N/A |
|------------------------|-------|--------|--------|-----|
| 1. Tooltips            |   4   |        |        |     |
| 2. Import JSON         |   5   |        |        |     |
| 3. Unsaved Changes     |   5   |        |        |     |
| 4. Comparison          |   6   |        |        |     |
| 5. Audit Log           |   5   |        |        |     |
| 6. Integration         |   3   |        |        |     |
| 7. Edge Cases          |   5   |        |        |     |
| 8. Accessibility       |   3   |        |        |     |
| 9. Cross-Browser       |   4   |        |        |     |
| 10. Performance        |   3   |        |        |     |
|------------------------|-------|--------|--------|-----|
| **TOTAL**              |  43   |        |        |     |

**Pass Rate:** _____% 

**Critical Issues Found:** _____

**Recommendations:**


**Sign-off:**

[ ] Ready for production deployment
[ ] Requires bug fixes before deployment
[ ] Requires additional testing
```

---

**Last Updated:** February 5, 2026  
**Version:** 1.0  
**Status:** Ready for Testing
