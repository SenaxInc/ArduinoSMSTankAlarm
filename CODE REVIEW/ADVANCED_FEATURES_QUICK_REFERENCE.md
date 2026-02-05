# Client Console Advanced Features - Quick Reference

## Tooltip Help 💡

Hover over or tap any **?** icon next to field labels to see helpful explanations:

- **Product UID:** Blues Notehub Product UID (fleet-wide identifier)
- **Power Source:** Grid-Tied / Grid+Battery / Solar / Solar+MPPT options explained
- **Switch Mode:** NO (Normally-Open) vs NC (Normally-Closed) for float switches
- **4-20mA Sensor Type:** Pressure (bottom-mounted) vs Ultrasonic (top-mounted)
- **Input Mode:** Active LOW (button to ground) vs Active HIGH (button to VCC)
- **Relay Mode:** Momentary pulse, Until Clear, or Manual Reset
- **Target Client UID:** Local vs remote relay control
- **Clear Relay Action:** What happens when button is pressed

## Import Configuration JSON 📂

**Purpose:** Load a saved configuration from a JSON file

**Steps:**
1. Click **Import JSON** button (next to Download JSON)
2. Select a previously downloaded `.json` configuration file
3. Form auto-populates with all settings from the file
4. Review and modify if needed
5. Click **Send Configuration** to deploy

**Use Cases:**
- Restore from backup
- Clone configuration to multiple sites
- Use template configurations (e.g., "Standard Tank Monitor", "Gas Sensor Setup")
- Disaster recovery after accidental misconfiguration

**Note:** Imported configurations are marked as unsaved (*) until you send them.

## Unsaved Changes Warning ⚠️

**How It Works:**
- Any form change adds an asterisk (*) to "Send Configuration" button
- Button becomes bold: **Send Configuration ***
- Browser warns if you try to close/navigate away
- Switching clients asks: "You have unsaved changes. Switch clients anyway?"

**Triggers:**
- Editing any input field
- Adding/removing sensors
- Adding/removing inputs
- Modifying alarms, relays, or SMS alerts
- Importing a JSON file

**Clears:**
- Successfully sending configuration
- Loading a new client (after confirmation)
- Refreshing the page (after browser warning)

## Configuration Comparison 🔍

**Purpose:** See exactly what changed before sending

**How It Works:**
1. Load a client configuration (baseline stored automatically)
2. Make changes to sensors, alarms, settings
3. Click **Send Configuration**
4. See confirmation dialog:
   - **Modified:** 3 fields
   - **Added:** 2 fields
   - **Removed:** 0 fields
5. Click OK to proceed or Cancel to review

**What It Shows:**
- **+ Added:** New sensors, alarms, or settings
- **• Modified:** Changed values (threshold from 20→80, etc.)
- **- Removed:** Deleted sensors or features

**Benefits:**
- Catch mistakes before deployment ("Wait, did I mean 80 or 800?")
- Verify bulk changes ("Did I update all 5 tanks or just 3?")
- Understand impact of template imports

## Configuration History 📜

**Purpose:** Track all configuration changes for audit and troubleshooting

**What's Logged:**
- Every successful configuration send
- Client UID targeted
- What changed (added/modified/removed fields)
- Timestamp of change
- User who made the change (currently "admin")

**Storage:**
- Saved in browser localStorage
- Persists across page refreshes
- Keeps last 50 changes (auto-prunes older entries)

**View History:** (UI coming soon)
Currently stored in browser console:
```javascript
console.log(state.configHistory)
```

**Use Cases:**
- "Who changed the alarm threshold last week?"
- "When did we add the second sensor to Site A?"
- "What was the configuration before it stopped working?"
- Compliance auditing

## Workflow Examples

### Template Deployment Across Fleet

**Scenario:** Deploy same 3-sensor tank configuration to 10 sites

1. Configure first site perfectly
2. Click **Download JSON**
3. For each remaining site:
   - Select client from dropdown
   - Click **Import JSON** → select template file
   - Update Site Name and Device Label only
   - Click **Send Configuration**
   - Review comparison: "Modified: 2 fields (site name, device label)"
   - Confirm and send

**Time Saved:** 5 minutes per site × 10 sites = 50 minutes

### Backup Before Major Change

**Scenario:** Upgrading from 1-sensor to 3-sensor monitoring

1. Load existing client config
2. Click **Download JSON** → save as backup
3. Add 2 new sensors
4. Configure new alarms and relays
5. Click **Send Configuration**
6. Review comparison showing all additions
7. If something breaks → **Import JSON** from backup

### Recovering from Accidental Edit

**Scenario:** Changed wrong client's config by mistake

**Without unsaved warning:** ❌
- Edit Client A
- Forget to send
- Switch to Client B
- Edits lost forever

**With unsaved warning:** ✅
- Edit Client A
- Try to switch to Client B
- Warning: "You have unsaved changes. Switch clients anyway?"
- Click Cancel
- Send changes to Client A
- Now safely switch to Client B

## Tips & Best Practices

### Tooltips
- **Mobile users:** Tap the ? icon (doesn't require hover)
- **Keyboard users:** Tab to ? icon, press Enter to focus and show tooltip
- **Screen readers:** Tooltip text read automatically via aria-label

### Import JSON
- **File naming:** Use descriptive names like `TankAlarm_Site_A_Baseline_2026.json`
- **Version control:** Include date in filename for tracking
- **Templates:** Create library of common configs (tank, gas, multi-sensor)
- **Validation:** File must pass same validation as manual configuration

### Unsaved Changes
- **Force leave:** Close browser warning can be bypassed (browser policy)
- **Tab switch:** Warning only on tab close/refresh, not tab switching
- **Auto-save:** Not implemented - must manually send to save
- **Session timeout:** PIN expiration doesn't clear unsaved flag

### Configuration Comparison
- **Granularity:** Shows field-level changes, not value-level details
- **Deep changes:** Sensor array changes show as "modified" not individual sensor diffs
- **Cancel safe:** Canceling comparison does NOT send config
- **Review time:** No timeout - take as long as needed

### Audit Log
- **Privacy:** Stored locally in browser - not shared across devices
- **Capacity:** Last 50 changes = ~6 months of weekly updates per client
- **Export:** Not yet available (coming soon)
- **Multi-user:** Currently shows "admin" for all changes (future: real user tracking)

## Keyboard Shortcuts

| Action | Shortcut |
|--------|----------|
| Show tooltip | Tab to ?, then Enter or Space |
| Import JSON | Alt+I (focus Import button) |
| Send Config | Alt+S (submit form) |
| Download JSON | Alt+D (focus Download button) |

## Browser Compatibility

| Feature | Chrome | Firefox | Safari | Edge |
|---------|--------|---------|--------|------|
| Tooltips | ✅ | ✅ | ✅ | ✅ |
| Import JSON | ✅ | ✅ | ✅ | ✅ |
| Unsaved Warning | ✅ | ✅ | ✅ | ✅ |
| Config Comparison | ✅ | ✅ | ✅ | ✅ |
| Audit Log (localStorage) | ✅ | ✅ | ✅ | ✅ |

**Minimum Versions:**
- Chrome 4+ (localStorage support)
- Firefox 3.5+
- Safari 4+
- Edge (all versions)

## Troubleshooting

### Tooltip Not Showing
- **Cause:** CSS not loaded or browser zoom too high
- **Fix:** Refresh page, reset browser zoom to 100%

### Import JSON Fails with "Invalid JSON"
- **Cause:** File corruption or wrong file format
- **Fix:** 
  - Verify file opens in text editor and looks like valid JSON
  - Try downloading a fresh config and importing that
  - Check for validation errors in error message

### Unsaved Warning Not Appearing
- **Cause:** JavaScript error or browser policy
- **Fix:**
  - Check browser console for errors
  - Verify browser allows beforeunload events (some privacy modes block)

### Comparison Shows Wrong Changes
- **Cause:** Original config not captured properly
- **Fix:**
  - Reload client (selects client again to capture fresh baseline)
  - Import JSON to set clean baseline

### Audit Log Lost
- **Cause:** Browser localStorage cleared (privacy mode, manual clear)
- **Fix:**
  - Export history before clearing browser data (feature coming soon)
  - Server-side logging (future enhancement)

## Coming Soon 🚀

- **Audit Log Viewer UI:** Dedicated page to browse history
- **FTP Backup:** Auto-save configs to FTP server
- **Configuration Templates:** Built-in library of common setups
- **Diff Viewer:** Visual side-by-side comparison
- **Rollback:** One-click revert to previous configuration
- **Multi-User:** Real user tracking instead of "admin"

---

**Last Updated:** February 5, 2026  
**Version:** 1.0  
**Documentation:** [ADVANCED_FEATURES_IMPLEMENTATION_02052026.md](./ADVANCED_FEATURES_IMPLEMENTATION_02052026.md)
