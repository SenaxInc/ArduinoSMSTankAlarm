# TankAlarm Backup and Recovery Guide

**Protecting Your Configuration and Data with Comprehensive Backup Strategies**

---

## Introduction

A comprehensive backup strategy ensures your TankAlarm deployment can recover quickly from hardware failures, accidental misconfigurations, or data loss. This guide covers backup procedures for client devices, servers, and fleet-wide deployments.

### What You'll Learn

- Understanding what needs to be backed up
- Manual and automated backup procedures  
- Disaster recovery workflows
- Fleet-wide backup strategies
- Data preservation for compliance
- Testing recovery procedures

### What Gets Backed Up

**Client Devices:**
- Configuration (tank settings, thresholds, calibration)
- Sensor calibration data
- Relay assignments
- Network settings (Product UID, fleet assignments)

**Server Devices:**
- All client configurations (stored centrally)
- Historical telemetry data
- Alarm logs and contact lists
- Network configuration (IP, DNS)
- Dashboard customizations

**Cloud (Blues Notehub):**
- Device registrations and fleet memberships
- Event history
- Route configurations

### Recovery Scenarios

This guide prepares you for:
- 🔧 Hardware failure (replace Opta device)
- ⚡ Power surge damage
- 🔄 Accidental configuration overwrite
- 💾 LittleFS corruption
- 🌊 Complete site loss (disaster recovery)
- 📊 Data forensics (compliance/audit)

---

## Understanding TankAlarm Data Storage

### Client Device Storage

**LittleFS Internal Flash:**
```
/config.json          - Active configuration
/calibration.json     - Sensor calibration points
/state.json           - Runtime state (peaks, counters)
```

**Characteristics:**
- ✅ Survives power cycles
- ✅ No SD card required
- ❌ Lost if device reflashed/replaced
- ❌ Not easily accessible without USB connection

**Data Persistence:**
- Configuration: Written on changes from server
- Calibration: Written when points added
- State: Updated periodically (varies by feature)

### Server Device Storage

**LittleFS Internal Flash:**
```
/server_config.json     - Server settings
/clients/               - Client configurations (one file per device)
  ├── dev_123456.json
  ├── dev_789012.json
  └── ...
/history/               - Historical telemetry (tiered)
  ├── recent.json       - Last 7 days detailed
  ├── monthly_202601.json
  └── ...
/alarms/                - Alarm log
/unloads/               - Unload event log
```

**Characteristics:**
- ✅ Centralized storage for all clients
- ✅ Accessible via web interface
- ❌ Limited capacity (~1.5MB usable)
- ❌ Lost if device replaced

**Optional External Storage:**
- FTP server for historical data archival
- Automatic offload when 80% full
- Long-term data retention

### Cloud Storage (Blues Notehub)

**Persisted Data:**
- Device registrations (Notecard serial numbers)
- Fleet memberships
- Event history (retention policy dependent)

**Characteristics:**
- ✅ Accessible from anywhere
- ✅ Survives local hardware failures
- ✅ Automatic redundancy
- ❌ Event history limited by plan (90 days typical)
- ❌ Cannot store configuration directly

---

## Backup Strategies

### Strategy 1: Manual Configuration Export

**Best For:**
- Small deployments (1-10 devices)
- Infrequent changes
- Compliance documentation

**Procedure:**

**Step 1: Export Server Configuration**

1. Access server dashboard: `http://<server-ip>/`
2. Navigate to **"System"** → **"Export Configuration"**
3. Click **"Download All Configs"** button
4. Save file: `tankalarm_backup_2026-01-07.zip`

**Contents:**
```
tankalarm_backup_2026-01-07.zip
├── server_config.json
├── clients/
│   ├── dev_864475044012345.json
│   ├── dev_864475044056789.json
│   └── ...
├── contacts.json
├── alarms_log.json
└── metadata.json (backup date, firmware version)
```

**Step 2: Document Network Settings**

Create `network_config.txt`:
```
Server Network Configuration
============================
IP Address: 192.168.1.150
Subnet: 255.255.255.0
Gateway: 192.168.1.1
DNS: 192.168.1.1
DHCP: false (static IP)

Blues Notehub
=============
Product UID: com.company.tankalarm:production
Server Fleet: tankalarm-server
Client Fleet: tankalarm-clients

Contact Information
==================
Primary SMS: +1234567890
Secondary SMS: +1234567891
Daily Email: alerts@company.com
```

**Step 3: Export Calibration Data**

For each client with custom calibration:
1. Access calibration page
2. Click **"Export Calibration"** for each tank
3. Save as: `calibration_sitename_tankid_date.csv`

**Example CSV:**
```csv
Site,Tank,Sensor_mA,Height_in,Date
North Farm,Diesel,4.2,2.0,2026-01-07
North Farm,Diesel,12.3,48.5,2026-01-07
North Farm,Diesel,19.6,93.0,2026-01-07
```

**Step 4: Store Backups Securely**

- **Local**: USB drive in fireproof safe
- **Cloud**: Google Drive, Dropbox, OneDrive
- **Version Control**: GitHub private repository (for JSON files)
- **Off-Site**: Another physical location

**Frequency:**
- After any configuration change
- Weekly for active deployments
- Monthly for stable systems

### Strategy 2: Automated Server Backup

**Best For:**
- Medium deployments (10-50 devices)
- Frequent configuration changes
- Continuous operations

**Automated Backup via FTP (Future Enhancement)**

**Server Configuration:**
```json
{
  "backup": {
    "enabled": true,
    "ftpServer": "backup.company.com",
    "ftpUser": "tankalarm",
    "ftpPassword": "encrypted_password",
    "ftpPath": "/backups/tankalarm/",
    "schedule": "daily",
    "retention": 30
  }
}
```

**Backup Schedule:**
- Daily at 3:00 AM (low activity time)
- Uploads all configurations
- Rotates old backups (keeps last 30 days)

**Current Workaround (Manual FTP):**

1. **Export configuration bundle** as above
2. **Connect to FTP server** with client (FileZilla, etc.)
3. **Upload** backup ZIP file
4. **Name with date**: `backup_2026-01-07.zip`
5. **Automate** with scheduled task:

```powershell
# Windows PowerShell scheduled task
$date = Get-Date -Format "yyyy-MM-dd"
$source = "http://192.168.1.150/api/export"
$dest = "\\backupserver\tankalarm\backup_$date.zip"

Invoke-WebRequest -Uri $source -OutFile $dest
```

### Strategy 3: Git Version Control

**Best For:**
- Development/testing environments
- Multiple administrators
- Change tracking requirements
- Advanced users

**Setup Git Repository:**

```bash
# Initialize repository
mkdir tankalarm-config
cd tankalarm-config
git init

# Create directory structure
mkdir -p clients server calibration

# Download current configs
curl http://192.168.1.150/api/export -o backup.zip
unzip backup.zip

# Initial commit
git add .
git commit -m "Initial configuration backup"

# Push to remote (GitHub/GitLab)
git remote add origin https://github.com/company/tankalarm-config.git
git push -u origin master
```

**Daily Backup Script:**

```bash
#!/bin/bash
# daily_backup.sh

DATE=$(date +%Y-%m-%d)
SERVER_IP="192.168.1.150"

# Export current config
curl -s http://$SERVER_IP/api/export -o /tmp/tankalarm_export.zip

# Extract
cd /path/to/tankalarm-config
unzip -o /tmp/tankalarm_export.zip

# Commit changes
git add .
git diff --quiet || git commit -m "Auto backup $DATE"
git push origin master

echo "Backup complete: $DATE"
```

**Benefits:**
- Full change history
- Diff between versions
- Rollback to any previous state
- Collaboration-friendly

### Strategy 4: Fleet-Wide Coordinated Backup

**Best For:**
- Large deployments (50+ devices)
- Multiple servers
- Enterprise environments

**Architecture:**

```
Regional Servers:
├── Server A (Site 1-20)
├── Server B (Site 21-40)  
├── Server C (Site 41-60)
└── ...

Central Backup Server:
└── Receives nightly backups from all servers
    ├── server_a_backup/
    ├── server_b_backup/
    └── server_c_backup/
```

**Coordination:**

1. **Each Server** exports config at 2:00 AM local time
2. **Central Script** collects from all servers
3. **Validation** checks integrity
4. **Archival** to long-term storage
5. **Alerting** if any backup fails

**Monitoring:**

```python
# Check all servers backed up
servers = [
  "192.168.1.150",  # Server A
  "192.168.2.150",  # Server B  
  "192.168.3.150",  # Server C
]

for server in servers:
  try:
    response = requests.get(f"http://{server}/api/backup/status")
    data = response.json()
    
    if data['lastBackup'] < yesterday():
      alert(f"Server {server} backup overdue!")
  except:
    alert(f"Server {server} unreachable!")
```

---

## Disaster Recovery Procedures

### Scenario 1: Replace Failed Client Device

**Problem:** Client Opta hardware failure, need replacement

**Required:**
- ✅ Backup of client configuration
- ✅ Replacement Opta + Notecard
- ✅ Replacement Analog Expansion
- ✅ Blues Notehub access

**Recovery Steps:**

**1. Provision New Notecard**

- Log into Blues Notehub
- Go to **Devices** → **Claim Device**
- Enter new Notecard serial number
- Assign to product
- Assign to `tankalarm-clients` fleet

**2. Upload Firmware**

- Connect new Opta via USB-C
- Open Arduino IDE
- Upload latest client firmware
- Verify compilation successful

**3. Restore Configuration**

**Option A: Via Server Dashboard (Recommended)**

1. Power on replacement client
2. Wait for first telemetry (30 min default)
3. Client appears in server dropdown
4. Select client → **"Configure"**
5. Load saved configuration JSON
6. Send to device
7. Verify settings applied

**Option B: Via Serial Monitor**

```cpp
// Connect to client via USB
// Open serial monitor (115200 baud)

// Paste configuration JSON:
CONFIG:{
  "site": "North Farm",
  "deviceLabel": "Tank-01",
  ...
}
```

**4. Restore Calibration**

1. Access calibration page for this client
2. Upload saved calibration CSV
3. Or manually enter calibration points
4. Verify accuracy with known level

**5. Verify Operation**

- [ ] Client sends telemetry
- [ ] Levels read correctly (compare to physical measurement)
- [ ] Alarms trigger at correct thresholds
- [ ] Relay commands work (if configured)
- [ ] Daily reports include this client

**Downtime:** 1-2 hours (mostly waiting for first telemetry)

### Scenario 2: Replace Failed Server Device

**Problem:** Server Opta failure, clients still operational

**Impact:** No dashboard access, no central configuration, clients continue monitoring independently

**Required:**
- ✅ Complete server backup (all client configs)
- ✅ Network settings documentation
- ✅ Replacement server hardware
- ✅ Blues Notehub access

**Recovery Steps:**

**1. Provision New Server**

- Claim new Notecard in Notehub
- Assign to `tankalarm-server` fleet
- Upload server firmware
- Configure network (static IP or DHCP)

**2. Restore Network Configuration**

```cpp
// ServerConfig.h (optional — create in server sketch folder)

#define DEFAULT_SERVER_PRODUCT_UID "com.company.tankalarm:production"

// Static IP (if used)
#define USE_STATIC_IP true
IPAddress static_ip(192, 168, 1, 150);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
```

**3. Upload Server Firmware**

- Compile with correct settings
- Upload to new Opta
- Verify Ethernet connection
- Access dashboard at configured IP

**4. Restore Server Configuration**

Via API or dashboard import:

```bash
# Upload server config
curl -X POST http://192.168.1.150/api/import \
  -H "Content-Type: application/json" \
  -d @server_config.json
```

**5. Restore All Client Configurations**

```bash
# Batch import all client configs
for config in clients/*.json; do
  curl -X POST http://192.168.1.150/api/clients/import \
    -H "Content-Type: application/json" \
    -d @$config
done
```

**6. Verify Full Operation**

- [ ] Dashboard loads correctly
- [ ] All clients visible in dropdown
- [ ] Historical data displayed (if preserved)
- [ ] Can configure clients remotely
- [ ] Alarms and notifications working
- [ ] Daily reports sending

**Downtime:** 2-4 hours

**Note:** Historical data only restored if backed up to external FTP. Otherwise starts fresh.

### Scenario 3: Complete Site Loss

**Problem:** Fire, flood, theft - all equipment gone

**Required:**
- ✅ Off-site configuration backups
- ✅ Insurance/budget for new equipment
- ✅ Site access for reinstallation
- ✅ Blues Notehub account intact

**Recovery Steps:**

**Phase 1: Assessment (Day 1)**

1. Verify extent of loss
2. Review backup currency (how old?)
3. Order replacement hardware
4. Plan installation logistics

**Phase 2: Procurement (Days 2-7)**

1. Order Opta devices (clients + server)
2. Order Blues Notecards
3. Order Analog Expansions
4. Order sensors (if destroyed)
5. Expedite shipping if critical

**Phase 3: Notehub Cleanup (Day 3-4)**

1. Remove old devices from Notehub (if not salvageable)
2. Create new fleet if needed
3. Prepare for new device provisioning

**Phase 4: Installation (Days 8-10)**

1. Install server first (central coordination)
2. Restore server configuration from backup
3. Test server dashboard access
4. Install clients one at a time
5. Restore each client configuration
6. Test telemetry from each client

**Phase 5: Validation (Days 11-14)**

1. Verify all devices communicating
2. Recalibrate all sensors (physical measurements required)
3. Test alarms and notifications
4. Monitor for issues
5. Document any changes from backup

**Total Recovery Time:** 2-3 weeks

**Cost Optimization:**
- Keep spare Opta/Notecard on hand (reduces downtime)
- Insurance for equipment
- Cloud backups ensure config not lost

---

## Data Preservation for Compliance

### Regulatory Requirements

**Industries with retention requirements:**
- Dairy (milk production records)
- Fuel distribution (delivery logs)
- Wastewater (discharge monitoring)
- Chemical storage (inventory tracking)

**Typical Requirements:**
- 3-7 years retention
- Tamper-evident storage
- Audit trail of changes

### Long-Term Data Archival

**Export Historical Data:**

```bash
# Export all historical telemetry
GET /api/history/export?start=2025-01-01&end=2025-12-31

# Response: CSV file
timestamp,site,device,tank,level,sensor,alarm
2025-01-01T00:00:00Z,North Farm,dev:123,A,48.5,12.3,false
2025-01-01T00:30:00Z,North Farm,dev:123,A,48.7,12.4,false
...
```

**Archive Format:**

```
compliance_archive_2025.zip
├── telemetry_2025_Q1.csv
├── telemetry_2025_Q2.csv
├── telemetry_2025_Q3.csv
├── telemetry_2025_Q4.csv
├── alarms_2025.csv
├── unloads_2025.csv
├── configurations/
│   └── (all client configs as of year-end)
└── audit_log.txt
```

**Storage:**

- **Write-once media**: CD-R, DVD-R (for legal compliance)
- **Cloud archive**: AWS Glacier, Azure Archive
- **Encrypted**: AES-256 encryption
- **Off-site**: Geographic separation

### Audit Trail

**Track Configuration Changes:**

```json
{
  "auditLog": [
    {
      "timestamp": "2026-01-07T10:30:00Z",
      "user": "admin",
      "action": "config_change",
      "device": "dev:864475044012345",
      "changes": {
        "highAlarm": {"old": 90.0, "new": 85.0},
        "lowAlarm": {"old": 10.0, "new": 15.0}
      }
    },
    {
      "timestamp": "2026-01-07T14:15:00Z",
      "user": "admin",
      "action": "calibration_add",
      "device": "dev:864475044012345",
      "tank": "A",
      "point": {"sensor": 12.3, "height": 48.5}
    }
  ]
}
```

**Export for Compliance:**

```bash
# Generate audit report
GET /api/audit?start=2025-01-01&end=2025-12-31

# PDF report with:
- All configuration changes
- Who made changes and when
- Before/after values
- Calibration history
```

---

## Testing Recovery Procedures

### Annual Disaster Recovery Drill

**Purpose:** Verify backups are complete and recovery works

**Procedure:**

**1. Preparation (Week Before)**

- Notify team of drill date/time
- Ensure recent backups exist
- Prepare test hardware (if available)
- Or plan to use spare/dev server

**2. Simulated Failure (Drill Day)**

- Power off production server (or use test server)
- Pretend all data lost
- Start recovery from backups

**3. Recovery Execution (2-4 hours)**

- Follow disaster recovery procedure
- Time each step
- Document any issues
- Note missing information

**4. Validation**

- Verify all client configs restored
- Check calibration data accuracy
- Test dashboard functionality
- Confirm alarms working

**5. Post-Drill Review**

- Identify gaps in backup
- Update recovery documentation
- Fix discovered issues
- Schedule next drill

**6. Return to Production**

- If test server used: discard
- If production: restart normally
- Document drill results

### Backup Verification Checklist

**Monthly Quick Check:**

- [ ] Latest backup file exists and is recent
- [ ] Backup file size reasonable (not 0 bytes)
- [ ] Can extract/open backup file
- [ ] Spot-check one config file for validity

**Quarterly Deep Check:**

- [ ] Restore one client config to test device
- [ ] Verify config applies correctly
- [ ] Test calibration data restores
- [ ] Check off-site backup accessible
- [ ] Review backup retention/rotation

**Annual Full Test:**

- [ ] Complete disaster recovery drill
- [ ] Restore entire server from backup
- [ ] Validate all clients operational
- [ ] Test compliance reporting
- [ ] Update recovery documentation

---

## Best Practices

### The 3-2-1 Rule

**Follow industry-standard backup practice:**

- **3** copies of data (production + 2 backups)
- **2** different media types (server + external drive/cloud)
- **1** off-site copy (cloud or different physical location)

**Example Implementation:**

1. **Production**: Server LittleFS (active data)
2. **Backup 1**: Local USB drive (weekly manual export)
3. **Backup 2**: Cloud storage (automated daily upload)
4. **Off-site**: Backup 2 is already off-site

### Backup Frequency Guidelines

**Configuration Changes:**

- Immediately after any change
- Before bulk updates
- Before firmware upgrades

**Regular Schedule:**

| Deployment Size | Backup Frequency | Method |
|-----------------|------------------|--------|
| 1-5 devices | Weekly | Manual export |
| 6-20 devices | Daily | Automated FTP |
| 21-50 devices | Daily | Automated + Git |
| 50+ devices | Continuous | Enterprise backup solution |

### Documentation

**Keep Updated:**

- Network diagram
- Device inventory (serial numbers)
- Backup procedures
- Recovery contact list
- Vendor contacts

**Sample Inventory:**

```
Device Inventory - TankAlarm Deployment
=======================================

Server:
  Location: Main Office Server Room
  IP: 192.168.1.150
  Opta Serial: ABX00049-XXXXX
  Notecard Serial: XXXXXXXXXXXX
  Purchase Date: 2025-06-15
  Warranty: Until 2027-06-15

Client 1:
  Location: North Farm - Diesel Tank
  Site Label: "North Farm Diesel"
  Opta Serial: ABX00049-YYYYY
  Notecard Serial: YYYYYYYYYYYY
  Notecard IMEI: 864475044012345
  Analog Expansion: AFX00006-ZZZZZ
  Sensors: CH0 (4-20mA Fuel Level)
  Installation Date: 2025-07-01

[Continue for all devices...]
```

---

## Troubleshooting

### Backup File Corrupted

**Symptoms:**
- Cannot extract ZIP file
- JSON files won't parse
- Import fails with errors

**Solutions:**

1. **Try older backup** (if available)
2. **Manual config reconstruction**:
   - Review screenshots/documentation
   - Check Blues Notehub for device UIDs
   - Rebuild configs from memory + defaults

3. **Verify backup process**:
   - Test export function with fresh download
   - Check file integrity immediately after backup
   - Use checksums (MD5/SHA256)

### Restore Fails - Version Mismatch

**Symptoms:**
- Backup from v0.9, current firmware v1.0
- JSON structure changed
- Import errors

**Solutions:**

1. **Check firmware version** in backup metadata
2. **Upgrade/downgrade** firmware to match
3. **Manual migration**:
   - Extract configs from backup
   - Manually create new configs in current format
   - Copy values field-by-field

### Missing Calibration Data

**Symptoms:**
- Client configs restored but calibration lost
- Levels reading incorrectly

**Solutions:**

1. **Check if calibration backed up separately**
2. **Recalibrate from scratch**:
   - Use known tank levels
   - Add calibration points manually
   - See Sensor Calibration Guide

3. **Prevent future loss**:
   - Ensure calibration export in backup process
   - Document calibration points in site logbook

---

## Resources

### Related Guides

- [Server Installation Guide](SERVER_INSTALLATION_GUIDE.md) - Initial setup
- [Fleet Setup Guide](FLEET_SETUP_GUIDE.md) - Multi-device management  
- [Sensor Calibration Guide](SENSOR_CALIBRATION_GUIDE.md) - Calibration procedures
- [Dashboard Guide](DASHBOARD_GUIDE.md) - Export/import via web interface

### External Tools

- [FileZilla](https://filezilla-project.org/) - FTP client for backups
- [Git](https://git-scm.com/) - Version control for configs
- [7-Zip](https://www.7-zip.org/) - Archive management

### Standards and Best Practices

- [NIST SP 800-34](https://csrc.nist.gov/publications/detail/sp/800-34/rev-1/final) - Contingency Planning
- [ISO 22301](https://www.iso.org/standard/75106.html) - Business Continuity
- [Backup 3-2-1 Rule](https://www.backblaze.com/blog/the-3-2-1-backup-strategy/)

---

## Appendix: Backup Checklist

### Pre-Change Backup

Before any major change:

- [ ] Export current server configuration
- [ ] Export all client configurations
- [ ] Export calibration data
- [ ] Document current state (screenshots)
- [ ] Note firmware versions
- [ ] Save to multiple locations

### Post-Change Verification

After change complete:

- [ ] Test all affected clients
- [ ] Verify alarms still functional
- [ ] Check calibration still accurate
- [ ] Create new backup with changes
- [ ] Update documentation

### Monthly Maintenance

- [ ] Verify latest backup exists
- [ ] Test backup file integrity
- [ ] Check off-site backup current
- [ ] Review backup retention
- [ ] Update device inventory
- [ ] Document any changes

### Quarterly Review

- [ ] Restore test from backup
- [ ] Update recovery procedures
- [ ] Review disaster recovery plan
- [ ] Test backup automation
- [ ] Audit backup security

### Annual Drill

- [ ] Schedule disaster recovery drill
- [ ] Full server restore from backup
- [ ] Document recovery time
- [ ] Identify improvement areas
- [ ] Update all documentation

---

*Backup and Recovery Guide v1.1 | Last Updated: February 20, 2026*  
*Compatible with TankAlarm Firmware 1.1.1+*
