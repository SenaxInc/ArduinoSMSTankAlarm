# TankAlarm Sensor Calibration Guide

**Achieving Accurate Tank Level Measurements Through Proper Sensor Calibration**

---

## Introduction

Accurate tank level measurement is critical for inventory management, alarm reliability, and operational decisions. While sensors provide electrical signals proportional to tank levels, translating those signals into precise measurements requires proper calibration.

This guide covers calibration procedures for all sensor types supported by the TankAlarm 112025 system, from basic two-point calibration to advanced multi-point techniques.

### What You'll Learn

- Understanding why calibration matters
- Calibration theory and linear interpolation
- Step-by-step procedures for each sensor type
- Verifying and testing calibration accuracy
- Troubleshooting calibration problems
- Best practices for field calibration

### Prerequisites

Before calibrating sensors:
- [ ] System installed and operational (see [Client Installation Guide](CLIENT_INSTALLATION_GUIDE.md))
- [ ] Sensors physically installed and wired
- [ ] Access to server dashboard or serial monitor
- [ ] Measuring equipment (tape measure, ruler, calibrated gauge)
- [ ] Ability to safely measure actual tank levels

---

## Why Calibration Matters

### The Problem: Raw Sensor Readings

Without calibration, the system uses theoretical sensor specifications:

**4-20mA Sensor Example:**
- Sensor specs: 4mA = empty, 20mA = full
- Tank height: 96 inches
- **Assumption**: Linear relationship from 0" to 96"

**Reality:**
- 4mA might actually occur at 2" (sensor mount height)
- 20mA might occur at 94" (sensor mounting position)
- Actual relationship may not be perfectly linear
- Result: **2-4 inch errors** are common

### The Solution: Field Calibration

Calibration maps **actual measured heights** to **sensor readings**:

```
Calibration Point 1: Sensor reads 4.2mA  → Actually 3.5"
Calibration Point 2: Sensor reads 12.0mA → Actually 48.0"
Calibration Point 3: Sensor reads 19.8mA → Actually 93.2"

System interpolates between points → Accuracy: ±0.5"
```

### Benefits of Proper Calibration

- ✅ **Accuracy**: ±0.5" instead of ±3"
- ✅ **Reliability**: Correct alarm triggering
- ✅ **Inventory**: Precise volume calculations
- ✅ **Compliance**: Meet regulatory requirements
- ✅ **Confidence**: Trust your data

---

## Calibration Theory

### Linear Interpolation

The TankAlarm system uses **linear interpolation** between calibration points:

```
For sensor reading R between points P1 and P2:

Height = H1 + (H2 - H1) * (R - R1) / (R2 - R1)

Where:
  P1 = (R1, H1) = first point (sensor reading, actual height)
  P2 = (R2, H2) = second point (sensor reading, actual height)
  R = current sensor reading
```

**Example:**
```
Point 1: 4mA → 2"
Point 2: 20mA → 94"
Current reading: 12mA → ?

Height = 2 + (94 - 2) * (12 - 4) / (20 - 4)
       = 2 + 92 * 8 / 16
       = 2 + 46
       = 48 inches
```

### Multi-Point Calibration

More calibration points = better accuracy:

**2-Point Calibration:**
- Minimum requirement
- Good for linear sensors
- Accuracy: ±1-2"

**3-Point Calibration:**
- Recommended standard
- Corrects for minor non-linearity
- Accuracy: ±0.5-1"

**5+ Point Calibration:**
- Best for non-linear sensors
- Corrects for tank geometry changes
- Accuracy: ±0.25-0.5"

### Calibration Point Selection

**Strategic heights for 3-point calibration:**
1. **Low** (10-20% capacity) - Catches offset errors
2. **Mid** (45-55% capacity) - Corrects mid-range
3. **High** (80-90% capacity) - Validates full-scale

**For 5-point calibration, add:**
4. **Quarter** (25% capacity)
5. **Three-quarter** (75% capacity)

---

## Equipment and Safety

### Required Equipment

**Measurement Tools:**
- **Tape measure** or calibrated measuring stick
- **Flashlight** for dark tanks
- **Level** (bubble level or laser)
- **Calculator** or smartphone

**Safety Equipment:**
- Hard hat
- Safety glasses
- Gloves
- Non-slip footwear
- Harness (for top access)

**Optional:**
- Multimeter (for sensor verification)
- Laptop with serial monitor
- Camera (document measurements)

### Safety Considerations

⚠️ **WARNING:**
- Never enter confined spaces without proper training and equipment
- Use lockout/tagout for active equipment
- Ensure adequate ventilation
- Have spotter/backup personnel
- Follow site-specific safety procedures

**Tank Access Safety:**
- Secure ladders properly
- Use fall protection above 6 feet
- Verify stable footing
- Don't lean over tank edges

**Measurement Safety:**
- Use drop-line measurements from top when possible
- Avoid contact with tank contents
- Be aware of fumes/vapors
- Know emergency procedures

---

## Calibration Procedures by Sensor Type

### 4-20mA Current Loop Sensors

**Common Applications:**
- Submersible level sensors
- Pressure transducers
- Ultrasonic level sensors

#### Preparation

1. **Verify Sensor Installation:**
   - Check wiring: sensor → Analog Expansion CH0-7
   - Confirm 24V power supply stable
   - Measure actual current with multimeter (optional)

2. **Access Server Dashboard:**
   - Navigate to: `http://<server-ip>/`
   - Select target client device
   - Go to configuration page

3. **Record Tank Information:**
   ```
   Tank ID: _______________
   Sensor Channel: CH____
   Tank Capacity: _________ gallons
   Tank Height: _________ inches
   Sensor Type: 4-20mA
   ```

#### Step 1: Establish Empty Point

**Method A: Tank is Empty**
1. Verify tank is actually empty (visual inspection)
2. Measure distance from sensor to liquid surface (or tank bottom)
3. Record:
   ```
   Actual Height: _____ inches
   Sensor Reading: _____ mA (from dashboard)
   ```
4. Click **"Add Calibration Point"** on dashboard
5. Enter actual height
6. System records current sensor reading automatically

**Method B: Tank Has Some Liquid**
1. Measure actual liquid height with tape measure
2. Record current sensor reading from dashboard
3. Add calibration point as above
4. Plan to capture lower point when tank empties (or skip if midpoint acceptable)

#### Step 2: Establish High Point

**Controlled Fill Method:**
1. Fill tank to near-full level (80-90%)
2. Wait 5-10 minutes for level to stabilize
3. Measure actual height from top of tank:
   ```
   Tank Height: 96 inches
   Air Gap: 8 inches
   Actual Height: 88 inches
   ```
4. Record sensor reading from dashboard
5. Add calibration point

**Operational Method:**
1. Wait for normal operations to fill tank
2. When tank reaches high level, quickly measure
3. Add calibration point while at peak

#### Step 3: Add Midpoint (Recommended)

1. Wait for tank to reach approximately 50% capacity
2. Measure actual height precisely
3. Add calibration point
4. System now has 3 points for improved accuracy

#### Step 4: Verify Calibration

1. Dashboard should show: **"Calibration: 3 points active"**
2. Compare displayed level to known heights
3. Check at different levels if possible
4. Maximum error should be <1 inch

**Verification Example:**
```
Known Height: 48.0 inches
Dashboard Shows: 47.6 inches
Error: 0.4 inches ✓ Acceptable

Known Height: 75.0 inches
Dashboard Shows: 78.2 inches
Error: 3.2 inches ✗ Re-check measurement or add more points
```

---

### 0-10V Analog Sensors

**Common Applications:**
- Float-based level sensors
- Capacitive level sensors
- Some ultrasonic sensors

#### Calibration Procedure

**Similar to 4-20mA but note voltage range:**

1. **Check Voltage Compatibility:**
   - Arduino Opta Analog Expansion: 0-10V range
   - Some sensors output 0-5V (still compatible)
   - Verify sensor powered correctly (usually 12-24V)

2. **Follow 4-20mA procedure** with these adjustments:
   - Record voltage instead of current
   - Typical: 0V = empty, 10V = full
   - Actual may vary: 0.2V min, 9.8V max

3. **Common Voltage Ranges:**
   ```
   0-10V:  Most industrial sensors
   0-5V:   Arduino-compatible sensors
   1-5V:   Some pressure transducers
   0.5-4.5V: Ratiometric sensors
   ```

4. **Dashboard shows voltage** in sensor reading field

#### Example Calibration Points

```
Point 1: 0.3V  → 1.5"   (sensor offset)
Point 2: 5.1V  → 48.0"  (midpoint)
Point 3: 9.7V  → 94.5"  (near full)
```

---

### Digital Float Switches

**Common Applications:**
- High level alarms
- Low level alarms
- Binary level indication

#### Simplified Calibration

Digital sensors provide binary (on/off) states:

1. **No numeric calibration needed** - float either open or closed
2. **Configure threshold heights** instead:
   ```
   Float Switch Position: 85 inches
   Action: Trigger high alarm when closed
   ```

3. **Verification:**
   - Manually operate float
   - Verify dashboard shows correct state
   - Confirm alarm triggers at correct height

4. **Multiple Float Configuration:**
   ```
   CH0: Low alarm float (10 inches)
   CH1: Normal level float (50 inches)  
   CH2: High alarm float (85 inches)
   CH3: Overflow float (95 inches)
   ```

---

## Advanced Calibration Techniques

### Multi-Tank Calibration Strategy

**For sites with multiple tanks:**

1. **Prioritize by Criticality:**
   - Critical inventory tanks: 5-point calibration
   - Standard monitoring: 3-point calibration
   - Overflow alarms only: 2-point calibration

2. **Batch Calibration Sessions:**
   - Calibrate all tanks during fill/empty cycles
   - Document measurements systematically
   - Use spreadsheet to track progress

3. **Calibration Schedule:**
   ```
   Day 1: Establish low points (all tanks low after delivery)
   Day 2-3: Capture midpoints as tanks fill
   Day 4-7: Capture high points at peak capacity
   ```

### Calibration for Non-Standard Tank Shapes

**Irregular tank geometry requires more points:**

**Horizontal Cylindrical Tanks:**
- Non-linear volume vs height relationship
- Use 5-7 calibration points
- Focus points near mid-height (maximum non-linearity)

**Cone-Bottom Tanks:**
- Rapid level change near empty
- Add extra points in bottom 20%
- Standard spacing above cone

**Irregularly Shaped Tanks:**
- Measure known fill volumes
- Calculate corresponding heights
- Create custom calibration table

### Temperature Compensation

**Some liquids expand/contract with temperature:**

**Manual Approach:**
1. Calibrate at average operational temperature
2. Document temperature during calibration
3. Re-calibrate if temperature range exceeds ±20°F

**Advanced Approach:**
1. Add temperature sensor to client
2. Create temperature-adjusted calibration tables
3. System applies correction based on current temp

---

## Using the Server Dashboard for Calibration

### Web Interface Method

**Recommended for most users - no programming required**

#### Step 1: Access Calibration Page

1. Navigate to: `http://<server-ip>/`
2. Select client device from dropdown
3. Click **"Calibration"** tab (or similar)
4. Select tank to calibrate

#### Step 2: Add Calibration Point

**While tank is at known height:**

1. Measure actual height: `48.5 inches`
2. Dashboard shows current sensor reading: `12.3 mA`
3. Click **"Add Calibration Point"** button
4. Enter measured height: `48.5`
5. Click **"Save"**
6. Server sends calibration data to client
7. Client confirms: `"Calibration point added: 12.3mA → 48.5in"`

#### Step 3: Review Calibration

**Dashboard displays:**
```
Calibration Points:
1. 4.2mA  → 2.0"   (low)
2. 12.3mA → 48.5"  (mid)
3. 19.6mA → 93.0"  (high)

Status: ✓ Active (3 points)
Interpolation: Linear
Coverage: 2.0" to 93.0"
```

#### Step 4: Test Interpolation

**Dashboard "Test Mode":**
1. Enter hypothetical sensor reading: `8.0mA`
2. System calculates: `24.8 inches`
3. Verify calculation makes sense
4. Test several readings across range

---

## Using Serial Monitor for Calibration

### Direct Client Configuration

**For users comfortable with serial commands**

#### Step 1: Connect to Client

1. Connect laptop to client Opta via USB-C
2. Open Arduino IDE → **Tools** → **Serial Monitor**
3. Set baud rate: **115200**
4. Client displays status messages

#### Step 2: Enter Calibration Mode

**Type command:**
```
CAL
```

**Client responds:**
```
Calibration Mode Active
Current sensor reading: 12.3mA
Enter actual height in inches:
```

#### Step 3: Add Calibration Point

**Type measured height:**
```
48.5
```

**Client responds:**
```
✓ Calibration point added
  Sensor: 12.3mA
  Height: 48.5"
  Total points: 2

Continue? (y/n):
```

#### Step 4: Add More Points or Exit

**To add more points:**
```
y
```

**To finish:**
```
n
```

**Client responds:**
```
Calibration saved to LittleFS
3 points active
Calibration range: 2.0" to 93.0"
```

---

## Verifying Calibration Accuracy

### Field Verification

**After calibration, validate at known heights:**

#### Method 1: Incremental Fill Test

1. **Empty tank** (or start at low level)
2. **Add known volume:**
   ```
   Tank: 1000 gallon capacity, 96" height
   Add: 100 gallons
   Expected height increase: 9.6 inches
   ```
3. **Compare dashboard reading** to expected
4. **Repeat** at different levels

#### Method 2: Direct Measurement Comparison

1. **Measure actual height** with tape measure
2. **Record dashboard reading**
3. **Calculate error:**
   ```
   Actual: 48.0"
   Dashboard: 47.5"
   Error: 0.5" (acceptable)
   ```
4. **Test at 3-5 different levels**

#### Acceptance Criteria

| Application | Maximum Error | Calibration Points |
|-------------|---------------|-------------------|
| **Critical Inventory** | ±0.25" | 5+ points |
| **Standard Monitoring** | ±0.5" | 3 points |
| **Alarm Only** | ±1.0" | 2 points |
| **Overflow Protection** | ±2.0" | 2 points (top range) |

---

## Troubleshooting Calibration Problems

### Calibration Not Improving Accuracy

**Symptoms:**
- After calibration, errors still >2 inches
- Inconsistent readings at same level
- Erratic behavior

**Causes and Solutions:**

#### 1. Sensor Installation Issues

**Check:**
- Sensor mounting angle (should be vertical)
- Sensor partially obstructed
- Sensor too close to tank walls/inlet pipes
- Electrical noise from pumps/motors nearby

**Solutions:**
- Relocate sensor to calm area of tank
- Ensure minimum clearances (6-12" from walls)
- Add shielded cable if EMI suspected
- Verify sensor is rated for liquid type

#### 2. Unstable Sensor Readings

**Check Serial Monitor:**
```
Sample 1: 12.3mA
Sample 2: 12.8mA
Sample 3: 11.9mA
Sample 4: 12.4mA
Variation: ±0.9mA  ✗ Too much
```

**Causes:**
- Loose wiring connections
- Inadequate power supply
- Sensor failure
- Liquid turbulence

**Solutions:**
- Check all terminal connections
- Verify 24V power supply stable (use multimeter)
- Sample during calm periods (not during fill)
- Add averaging/filtering in firmware

#### 3. Non-Linear Sensor Response

**Symptoms:**
- Accurate at calibration points
- Large errors between points
- Errors concentrated in specific range

**Solution:**
Add more calibration points in problem area:
```
Before:
Point 1: 0" 
Point 2: 48"   ← Big gap
Point 3: 96"

After:
Point 1: 0"
Point 2: 24"   ← Added
Point 3: 48"
Point 4: 72"   ← Added
Point 5: 96"
```

### Calibration Data Lost After Power Cycle

**Symptoms:**
- Calibration works initially
- After power loss, system reverts to uncalibrated

**Causes:**
- Calibration not saved to persistent storage
- LittleFS corruption
- Firmware bug

**Solutions:**

1. **Verify Save Operation:**
   - Dashboard should confirm: "Calibration saved"
   - Serial monitor shows: "Writing to LittleFS"

2. **Force Manual Save:**
   ```cpp
   // Serial monitor command
   SAVE_CAL
   ```

3. **Check LittleFS Health:**
   ```cpp
   // Serial monitor diagnostic
   FS_STATUS
   
   // Should show:
   // LittleFS mounted: ✓
   // Free space: XXXX bytes
   // Calibration file exists: ✓
   ```

4. **Re-flash Firmware** if persistent issues

### Dashboard Not Showing Calibration Points

**Symptoms:**
- Added calibration via serial monitor
- Dashboard doesn't reflect changes

**Causes:**
- Client hasn't sent updated telemetry
- Server cache stale
- Communication delay

**Solutions:**

1. **Force Telemetry Send:**
   ```cpp
   // Serial monitor command
   SEND_NOW
   ```

2. **Refresh Server Dashboard:**
   - Click browser refresh
   - Wait 5-10 minutes for next scheduled telemetry
   - Check Notehub events for recent data

3. **Verify Client→Server Communication:**
   - Check Notehub events tab
   - Confirm telemetry includes calibration data
   - Review server logs for parsing errors

---

## Best Practices

### Calibration Planning

**Before You Start:**

1. **Choose the Right Time:**
   - During planned maintenance
   - When tank levels will vary naturally
   - Avoid rush periods

2. **Prepare Documentation:**
   ```
   Site: _________________
   Date: _________________
   Tank ID: ______________
   Technician: ___________
   Weather: ______________
   Temperature: __________
   ```

3. **Gather Equipment:**
   - Measuring tools
   - Safety equipment
   - Laptop/tablet for dashboard access
   - Camera for documentation

### During Calibration

**Quality Tips:**

1. **Measure Twice:**
   - Take two independent height measurements
   - Average if they differ slightly
   - Investigate if difference >0.5"

2. **Wait for Stability:**
   - After filling/emptying, wait 5-10 minutes
   - Turbulence affects readings
   - Verify level isn't changing

3. **Document Everything:**
   - Photo of measuring tape
   - Screenshot of dashboard
   - Written notes of readings
   - Any anomalies observed

### After Calibration

**Follow-Up:**

1. **Test at Multiple Levels:**
   - Don't just trust calibration points
   - Verify interpolation works
   - Check extremes (near empty/full)

2. **Monitor for Drift:**
   - Re-check after 1 week
   - Re-check after 1 month
   - Annual recalibration recommended

3. **Backup Calibration Data:**
   - Export from server dashboard
   - Save calibration coefficients
   - Document in site logbook

---

## Calibration Maintenance

### When to Recalibrate

**Scheduled:**
- **Annually**: Standard practice
- **Semi-annually**: Critical applications
- **Quarterly**: Extreme environments

**Event-Driven:**
- After sensor replacement
- After tank modifications
- After suspected sensor damage
- Regulatory compliance audits
- Significant unexplained errors

### Calibration Drift Detection

**Warning Signs:**

1. **Gradual Errors:**
   - Readings slowly diverge from actual
   - Consistent offset develops
   - Indicates sensor aging

2. **Sudden Jumps:**
   - Abrupt change in readings
   - Suggests physical damage
   - Requires immediate investigation

3. **Increasing Variability:**
   - Readings fluctuate more than before
   - May indicate electrical issues
   - Check connections first

**Automated Monitoring:**
- Compare recent alarms to historical patterns
- Track fill/empty cycle times
- Flag unexpected inventory changes

### Calibration Data Management

**Server Dashboard:**
- Stores all calibration data in LittleFS
- Survives power cycles
- Backed up with system configuration

**Export Options:**
```json
{
  "device": "dev:864475044012345",
  "tank": "A",
  "calibration": [
    {"sensor": 4.2, "height": 2.0},
    {"sensor": 12.3, "height": 48.5},
    {"sensor": 19.6, "height": 93.0}
  ],
  "date": "2026-01-07",
  "technician": "John Doe"
}
```

**Version Control:**
- Keep history of calibrations
- Track changes over time
- Identify trends or problems

---

## Advanced Topics

### Sensor Linearization

**For highly non-linear sensors:**

Some sensors have inherent non-linearity that can't be fully corrected with simple interpolation.

**Solution: Piecewise Linear Approximation**

1. **Divide range into segments:**
   ```
   0-20%:   3 calibration points (rapid change)
   20-80%:  2 calibration points (linear)
   80-100%: 3 calibration points (rapid change)
   ```

2. **Apply different interpolation per segment**

3. **Requires custom firmware modification** for optimal results

### Calibration Uncertainty Analysis

**Understanding measurement accuracy:**

**Total uncertainty = √(sensor² + installation² + calibration²)**

Where:
- **Sensor uncertainty**: ±0.25" (manufacturer spec)
- **Installation uncertainty**: ±0.5" (mounting/positioning)
- **Calibration uncertainty**: ±0.25" (measurement error)

**Result:**
```
Total = √(0.25² + 0.5² + 0.25²)
      = √(0.0625 + 0.25 + 0.0625)
      = √0.375
      = ±0.61 inches
```

**Improving Accuracy:**
- Better installation reduces uncertainty
- More calibration points reduce error
- Higher-quality sensors provide better baseline

### Volume Calibration vs Height Calibration

**Two approaches:**

**Height Calibration (Standard):**
- Measures level in inches
- Converts to volume using tank geometry
- Simple and intuitive

**Volume Calibration (Advanced):**
- Directly calibrates to gallons/liters
- Uses known fill amounts
- More accurate for irregular tanks

**Volume Calibration Procedure:**
```
1. Start with empty tank
2. Add 100 gallons (metered)
3. Record sensor reading → 100 gal
4. Add 100 more (total 200)
5. Record sensor reading → 200 gal
6. Continue for full range
```

Requires accurate flow metering but eliminates tank geometry errors.

---

## Resources and Further Reading

### External Documentation

**Sensor Manufacturers:**
- [Omega Engineering - 4-20mA Handbook](https://www.omega.com/en-us/resources/4-20-ma-current-loop-output)
- [Automation Direct - Level Sensor Guide](https://www.automationdirect.com/microsites/levelsensors/)
- [Emerson - Calibration Best Practices](https://www.emerson.com/documents/automation/white-paper-best-practices-for-level-measurement-en-76024.pdf)

**Calibration Standards:**
- NIST SP 250 - Calibration Procedures
- ISA-51.1 - Process Instrumentation Terminology
- ISO 17025 - Testing and Calibration Laboratories

**Arduino Opta Resources:**
- [Opta Analog Expansion Datasheet](https://docs.arduino.cc/resources/datasheets/AFX00006-datasheet.pdf)
- [4-20mA Input Specifications](https://docs.arduino.cc/hardware/opta-analog)

### Related TankAlarm Guides

- [Client Installation Guide](CLIENT_INSTALLATION_GUIDE.md) - Initial sensor setup
- [Advanced Configuration Guide](ADVANCED_CONFIGURATION_GUIDE.md) - Custom sensor types
- [Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md) - Sensor problems
- [Dashboard Guide](DASHBOARD_GUIDE.md) - Using web interface for calibration

### Video Tutorials

- [4-20mA Sensor Calibration Basics](https://www.youtube.com/watch?v=example) (coming soon)
- [TankAlarm Calibration Walkthrough](https://www.youtube.com/watch?v=example) (coming soon)

---

## Appendix: Calibration Worksheet

### Field Calibration Data Sheet

**Site Information:**
```
Site Name: _______________________
Tank ID: _________________________
Date: ____________________________
Technician: ______________________
Temperature: _____________________
Weather: _________________________
```

**Tank Specifications:**
```
Tank Type: _______________________
Capacity: __________ gallons
Height: __________ inches
Diameter: __________ inches
Contents: ________________________
```

**Sensor Information:**
```
Sensor Type: _____________________
Model: ___________________________
Serial Number: ___________________
Installation Date: _______________
Analog Channel: __________________
```

**Calibration Points:**

| Point | Time | Actual Height | Sensor Reading | Notes |
|-------|------|---------------|----------------|-------|
| 1     |      | _____ inches  | _____ mA/V     |       |
| 2     |      | _____ inches  | _____ mA/V     |       |
| 3     |      | _____ inches  | _____ mA/V     |       |
| 4     |      | _____ inches  | _____ mA/V     |       |
| 5     |      | _____ inches  | _____ mA/V     |       |

**Verification Tests:**

| Test Level | Expected | Dashboard | Error | Pass/Fail |
|------------|----------|-----------|-------|-----------|
| _____ "    | _____ "  | _____ "   | _____ | ⬜        |
| _____ "    | _____ "  | _____ "   | _____ | ⬜        |
| _____ "    | _____ "  | _____ "   | _____ | ⬜        |

**Acceptance:**
- [ ] All errors within ±0.5 inches
- [ ] Calibration data saved to system
- [ ] Dashboard displays calibration status
- [ ] Alarms tested and functioning

**Signatures:**
```
Technician: _____________________ Date: __________
Supervisor: _____________________ Date: __________
```

---

*Sensor Calibration Guide v1.1 | Last Updated: February 20, 2026*  
*Compatible with TankAlarm Firmware 1.1.1+*
