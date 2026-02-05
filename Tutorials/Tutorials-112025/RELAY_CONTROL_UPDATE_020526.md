# Relay Control Configuration Update (Feb 2026)

**New Sensor Card UI - Simplified Relay Setup**

---

## What's New

The relay control configuration has been redesigned with a new sensor card interface that makes setup more intuitive and powerful:

✅ **Per-sensor relay configuration** - Each sensor can have independent relay actions  
✅ **Visual UI builder** - No more JSON editing  
✅ **Multiple relay targets** - One sensor can trigger multiple relay actions  
✅ **Built-in validation** - Prevents configuration errors  
✅ **Checkbox relay selection** - Easier than bitmask calculations  

---

## Configuring Relay Control via Dashboard

### Step 1: Access Configuration

1. Open server dashboard: `http://<server-ip>/`
2. Look for **"New Sites (Unconfigured)"** section
3. Click **"Configure →"** for the client you want to set up

### Step 2: Add Sensor with Monitoring

1. Click **"+ Add Sensor"** button
2. Configure sensor basics:
   ```
   Monitor Type: Tank Level
   Tank Number: 1
   Name: Primary Storage Tank
   Sensor Type: 4-20mA Current Loop
   Pin/Channel: Expansion CH0
   Height: 96 inches
   ```

### Step 3: Add Alarms

1. Click **"+ Add Alarm"** (blue button)
2. Set alarm thresholds:
   ```
   ✓ High Alarm: 90 inches
   ✓ Low Alarm: 10 inches
   ```

💡 **Tip:** Alarms must be configured before relay control becomes available.

### Step 4: Add Relay Control

1. Click **"+ Add Relay Control"** button (appears after adding alarms)
2. Fill in relay configuration:

   **Target Client UID:**
   - Enter the UID of the client with relay outputs
   - Example: `dev:864475044012345`
   - Can be same client (local) or different client (remote)
   - Get UID from Notehub Devices tab or server dashboard

   **Trigger On:**
   - **Any Alarm**: Activates on high OR low alarm
   - **High Alarm Only**: Only when level exceeds high threshold
   - **Low Alarm Only**: Only when level drops below low threshold

   **Relay Mode:**
   - **Momentary**: Pulse relay for fixed duration
   - **Until Clear**: Stay on until alarm clears
   - **Manual Reset**: Requires manual clear

   **Relay Outputs:**
   - ☐ R1 (Relay D0)
   - ☐ R2 (Relay D1)
   - ☐ R3 (Relay D2)
   - ☐ R4 (Relay D3)
   - Check boxes for relays to activate

### Step 5: Send Configuration

1. Review configuration
2. Click **"Download JSON"** to save backup
3. Click **"Send Configuration"**
4. Watch status: "✓ Configuration queued for delivery"
5. Client receives within ~5 minutes

---

## Example Configurations

### Example 1: Tank Overflow Prevention

**Scenario:** Stop pump when tank reaches 90% capacity

**Configuration:**
```
Sensor 1 (Tank Level):
  Monitor Type: Tank Level
  Height: 96 inches
  
  Alarms:
    ✓ High Alarm: 90 inches (94%)
  
  Relay Control:
    Target: dev:pump-controller-123
    Trigger: High Alarm Only
    Mode: Until Clear
    Relays: ☑ R1 (connected to pump contactor)
```

**Result:** When tank reaches 90 inches, relay R1 on pump controller activates, cutting power to pump. Relay releases when level drops below 90 inches.

### Example 2: Low Level Warning Light

**Scenario:** Activate beacon when tank drops below 15%

**Configuration:**
```
Sensor 1 (Diesel Tank):
  Monitor Type: Tank Level
  Height: 120 inches
  
  Alarms:
    ✓ Low Alarm: 18 inches (15%)
  
  Relay Control:
    Target: dev:alarm-panel-456
    Trigger: Low Alarm Only
    Mode: Manual Reset
    Relays: ☑ R2 (connected to strobe light)
```

**Result:** When tank drops to 18 inches, strobe light activates and stays on until manually cleared via dashboard or physical button.

### Example 3: Multi-Relay Emergency Shutdown

**Scenario:** Gas leak detected - shut down all equipment

**Configuration:**
```
Sensor 2 (Gas Pressure):
  Monitor Type: Gas Pressure
  Max Pressure: 10 PSI
  
  Alarms:
    ✓ High Alarm: 8 PSI
  
  Relay Control:
    Target: dev:safety-controller-789
    Trigger: High Alarm Only
    Mode: Manual Reset
    Relays: ☑ R1 ☑ R2 ☑ R3 ☑ R4 (all relays = emergency shutdown)
```

**Result:** High pressure triggers all 4 relays simultaneously, activating emergency shutdown sequence. Requires manual reset.

### Example 4: Dual-Action Alarm

**Scenario:** Tank low level triggers BOTH pump activation AND warning light

**Configuration:**
```
Sensor 1 (Water Tank):
  Monitor Type: Tank Level
  Height: 96 inches
  
  Alarms:
    ✓ Low Alarm: 12 inches (13%)
  
  Relay Control #1:
    Target: dev:pump-controller-123
    Trigger: Low Alarm Only
    Mode: Until Clear
    Relays: ☑ R1 (refill pump)
  
  Relay Control #2:
    Target: dev:same-client (local)
    Trigger: Low Alarm Only
    Mode: Until Clear
    Relays: ☑ R3 (warning indicator)
```

**Result:** Single low alarm triggers two independent relay actions - starts refill pump on remote controller AND activates local warning light.

---

## Benefits Over Old System

| Feature | Old System | New System |
|---------|-----------|------------|
| **Configuration** | Manual JSON editing | Visual UI builder |
| **Relay Selection** | Calculate bitmasks | Checkbox selection |
| **Multiple Actions** | One relay mask per sensor | Multiple relay sections per sensor |
| **Validation** | Manual verification | Automatic validation |
| **Backup** | Manual JSON copy | One-click download |
| **Error Detection** | Trial and error | Real-time validation |

---

## Migration from Old Configuration

If you have existing relay configurations in JSON format, the new system auto-converts them on first load. To update:

1. Select configured client in dashboard
2. Existing relays appear in relay control sections
3. Edit as needed using new UI
4. Click "Send Configuration" to update

Your existing relay logic continues working - the new UI just makes editing easier.

---

## Troubleshooting

**Q: "Add Relay Control" button is hidden**  
**A:** You must add alarms first. Click "+ Add Alarm" before relay control becomes available.

**Q: Relay doesn't activate on alarm**  
**A:** Verify:
- Target Client UID is correct
- Target client is online and receiving configs
- Relay mode matches your use case
- Alarm thresholds are properly set

**Q: Want to control relays on same device as sensor**  
**A:** Use the client's own UID as the target. This enables local relay control.

**Q: Need to trigger different relays for high vs low alarms**  
**A:** Add multiple relay control sections to the same sensor - one for high, one for low.

---

For the complete Relay Control Guide including wiring, safety, and hardware specs, see [RELAY_CONTROL_GUIDE.md](RELAY_CONTROL_GUIDE.md).
