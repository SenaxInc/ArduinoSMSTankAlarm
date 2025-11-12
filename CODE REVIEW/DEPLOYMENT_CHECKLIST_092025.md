# Deployment Checklist - TankAlarm 092025

**Purpose:** Use this checklist before deploying TankAlarm-092025 code to production hardware.

**Code Version:** 092025 (Post Code Review)  
**Review Date:** September 2025

---

## Pre-Deployment Configuration

### 1. Configuration Files

#### Client Configuration (`tank_config.txt`)
- [ ] HOLOGRAM_DEVICE_KEY set to actual device key (not "your_device_key_here")
- [ ] ALARM_PHONE_PRIMARY set to valid phone number (not "+12223334444")
- [ ] ALARM_PHONE_SECONDARY configured (optional but recommended)
- [ ] DAILY_REPORT_PHONE configured
- [ ] SITE_LOCATION_NAME set to meaningful site name
- [ ] TANK_NUMBER set correctly for multi-tank installations
- [ ] TANK_HEIGHT_INCHES set to actual tank height (must be > 0)

#### Sensor Configuration (based on SENSOR_TYPE)

**If SENSOR_TYPE == ANALOG_VOLTAGE:**
- [ ] TANK_FULL_VOLTAGE configured
- [ ] TANK_EMPTY_VOLTAGE configured
- [ ] Verify TANK_FULL_VOLTAGE ≠ TANK_EMPTY_VOLTAGE (difference must be > 0.001V)
- [ ] ANALOG_SENSOR_PIN set correctly

**If SENSOR_TYPE == CURRENT_LOOP:**
- [ ] TANK_FULL_CURRENT configured
- [ ] TANK_EMPTY_CURRENT configured
- [ ] Verify TANK_FULL_CURRENT ≠ TANK_EMPTY_CURRENT (difference must be > 0.001mA)
- [ ] I2C address configured correctly

**If SENSOR_TYPE == DIGITAL_FLOAT:**
- [ ] TANK_LEVEL_PIN configured
- [ ] Float switch normally-open vs normally-closed verified

#### Server Configuration (`server_config.h`)
- [ ] HOLOGRAM_DEVICE_KEY set
- [ ] MAC address configured for Ethernet shield
- [ ] Static IP settings configured correctly
- [ ] Daily email recipients configured
- [ ] Monthly report settings configured

---

## Hardware Verification

### Client Hardware
- [ ] Arduino MKR NB 1500 with antenna attached
- [ ] Hologram SIM card inserted correctly
- [ ] SD card inserted and formatted (FAT32)
- [ ] MKR SD PROTO Shield properly seated
- [ ] Sensor wiring verified against diagram
- [ ] Power supply adequate (recommend battery + solar for remote)
- [ ] Relay shield connected (if using relay control)
- [ ] Tank sensor physically installed and tested

### Server Hardware
- [ ] Arduino MKR NB 1500 with antenna attached
- [ ] Hologram SIM card inserted correctly
- [ ] MKR ETH Shield properly seated
- [ ] Ethernet cable connected
- [ ] SD card inserted and formatted (FAT32)
- [ ] Power supply connected (mains power recommended)

---

## Software Testing

### 1. Upload and Initial Boot
- [ ] Code compiles without errors
- [ ] Upload successful to Arduino
- [ ] Serial monitor shows startup messages
- [ ] Configuration validation passes (no CRITICAL ERROR messages)
- [ ] SD card initialized successfully
- [ ] RTC initialized

### 2. Network Connectivity
- [ ] Cellular connection established
- [ ] Network time sync successful (if enabled)
- [ ] Hologram connection verified
- [ ] Ethernet connection established (server only)
- [ ] Web interface accessible (server only)

### 3. Sensor Reading Tests
- [ ] Sensor reads correctly in Serial Monitor
- [ ] Values convert to inches correctly
- [ ] Calibration data loads (if using calibration)
- [ ] Feet/inches format displays correctly
- [ ] Level changes detected properly

### 4. Logging Tests
- [ ] Hourly log file created on SD card
- [ ] Daily log file created on SD card
- [ ] Alarm log file created on SD card
- [ ] Event log file created on SD card
- [ ] Log format matches specification (YYYYMMDD00:00,...)
- [ ] Timestamps are correct

### 5. Communication Tests
- [ ] SMS alarm message sends successfully
- [ ] Daily report SMS sends successfully
- [ ] Hologram data messages transmit successfully
- [ ] Server receives client messages (if server deployed)
- [ ] Email reports send (server only)

### 6. Error Handling Tests
- [ ] Remove SD card - system continues without crashing
- [ ] Reinsert SD card - logging resumes
- [ ] Disconnect network - system retries appropriately
- [ ] Invalid sensor reading - system handles gracefully
- [ ] Test with invalid configuration - startup validation catches errors

---

## Field Installation

### Client Installation
- [ ] Unit mounted in weatherproof enclosure
- [ ] Antenna positioned for good signal
- [ ] Power connections secure
- [ ] Sensor connections secure and weatherproofed
- [ ] SD card accessible for maintenance (optional)
- [ ] Label unit with site name and tank number
- [ ] Document GPS coordinates of installation
- [ ] Test cellular signal strength at location

### Server Installation
- [ ] Unit in climate-controlled environment
- [ ] Ethernet connection stable
- [ ] Power backed up by UPS (recommended)
- [ ] Web interface accessible from target computers
- [ ] Firewall rules configured (if needed)
- [ ] Static IP address reserved in DHCP

---

## Operational Verification (First 24 Hours)

### Client Operation
- [ ] Hourly readings logged correctly
- [ ] Daily report sent at configured time
- [ ] Alarm triggers tested (if safe to do so)
- [ ] Power consumption acceptable
- [ ] No unexpected reboots
- [ ] Memory usage stable (check log messages)

### Server Operation
- [ ] Receives data from all clients
- [ ] Daily email report sent successfully
- [ ] Web dashboard updates correctly
- [ ] Tank status displayed accurately
- [ ] No unexpected errors in logs

---

## Calibration (If Using Multi-Point Calibration)

### Initial Calibration
- [ ] Add at least 2 calibration points
- [ ] Calibration points span expected operating range
- [ ] Sensor values not duplicated (different readings)
- [ ] Physical measurements recorded accurately
- [ ] Calibration data saved to SD card
- [ ] Interpolation tested with known levels

### Ongoing Calibration
- [ ] Document procedure for adding calibration points
- [ ] Schedule periodic recalibration (recommend quarterly)
- [ ] Keep physical log of calibration measurements

---

## Documentation

### Installation Documentation
- [ ] Site location and GPS coordinates recorded
- [ ] Tank specifications documented (height, diameter, capacity)
- [ ] Sensor type and model recorded
- [ ] Installation date recorded
- [ ] Configuration file backed up
- [ ] Photos of installation taken
- [ ] Contact information for site access

### Operational Documentation
- [ ] Daily report schedule documented
- [ ] Alarm phone numbers verified and documented
- [ ] Maintenance schedule created
- [ ] Troubleshooting contact information available
- [ ] SD card replacement procedure documented

---

## Known Issues and Limitations

Review these items from CODE_REVIEW_092025.md:

- [ ] **String Memory Usage**: Monitor for memory issues during extended operation
- [ ] **Sorting Performance**: Calibration limited to reasonable number of points (<20)
- [ ] **Blocking Delays**: Some SMS retry logic uses blocking delays
- [ ] **Web Security**: Server web interface has limited security (local network only)

---

## Emergency Contacts

**Technical Support:**
- Name: _________________________
- Phone: _________________________
- Email: _________________________

**Site Contact:**
- Name: _________________________
- Phone: _________________________

**Cellular Provider:**
- Hologram Support: support@hologram.io
- Phone: _________________________

---

## Maintenance Schedule

### Weekly
- [ ] Check web dashboard for anomalies
- [ ] Verify daily reports received

### Monthly
- [ ] Review SD card logs for errors
- [ ] Check cellular signal strength
- [ ] Verify battery voltage (if battery powered)
- [ ] Test alarm notifications

### Quarterly
- [ ] Replace SD card (optional, for reliability)
- [ ] Recalibrate sensors
- [ ] Update firmware if new version available
- [ ] Clean sensors if accessible

### Annually
- [ ] Full system inspection
- [ ] Replace batteries (if applicable)
- [ ] Verify all phone numbers and email addresses
- [ ] Review and update configuration

---

## Sign-Off

**Deployed By:** _________________________ **Date:** _____________

**Site Location:** _________________________

**Client ID:** _________________________ **Server ID:** _________________________

**Configuration Validated:** [ ] YES [ ] NO

**Testing Complete:** [ ] YES [ ] NO

**Documentation Complete:** [ ] YES [ ] NO

**System Status:** [ ] OPERATIONAL [ ] ISSUES NOTED

**Issues/Notes:**
```
_________________________________________________________________

_________________________________________________________________

_________________________________________________________________
```

---

## Revision History

| Date | Version | Changes | By |
|------|---------|---------|-----|
| Sept 2025 | 1.0 | Initial checklist post code review | GitHub Copilot |
|  |  |  |  |

---

**Next Review Date:** _________________________

