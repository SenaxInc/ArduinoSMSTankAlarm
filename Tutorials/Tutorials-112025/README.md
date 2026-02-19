# TankAlarm Tutorials

**Professional Installation and Setup Guides for the TankAlarm 112025 System**

---

## 📚 Tutorial Library

This directory contains comprehensive, step-by-step tutorials for deploying and managing the TankAlarm 112025 tank monitoring system. All tutorials follow a consistent SparkFun-style format with clear instructions, troubleshooting sections, and helpful diagrams.

**Tutorial Count:** 15 comprehensive guides covering installation, configuration, communication, operations, and advanced features.

---

## 🚀 Quick Navigation

### New Users: Start Here

**[Quick Start Guide](QUICK_START_GUIDE.md)** ⭐  
*Get your first system running in 30 minutes*

Perfect for:
- First-time users
- Proof-of-concept testing
- Learning the system basics
- Single server + single client setup

**What you'll learn:**
- Notehub account creation
- Server installation and dashboard access
- Client deployment with one tank
- Verifying data flow

**Time:** 30 minutes setup + 30 minutes for first data

---

## 🔗 Communication & Routes (Set Up Before Devices!)

### [Notehub Routes Setup](NOTEHUB_ROUTES_SETUP.md) ⭐

*Configure the Notehub Routes that connect your devices*

**Covers:**
- How Blues Notecard routing works (.qo → Route → .qi)
- Creating the 5 Notehub Routes (ClientToServer, ServerToClient, ServerToViewer, SMS, Email)
- The Route Relay pattern explained
- Dynamic URL routing with JSONata for command.qo
- Verification checklist for each route
- Troubleshooting common route errors
- Data usage and cost estimates

**Prerequisites:**
- Notehub account with project created
- At least one device provisioned

**Time:** 30 minutes

---

### [Firmware Communication Guide](FIRMWARE_COMMUNICATION_GUIDE.md)

*Understand how Client, Server, and Viewer firmware communicate*

**Covers:**
- Notefile naming convention (.qo outbound, .qi inbound)
- Complete notefile definition table
- How command.qo consolidates server→client messaging
- Client sending patterns (publishNote, triggerRemoteRelays)
- Server sending patterns (sendRelayCommand, sendConfigViaNotecard)
- Server polling patterns (processNotefile)
- Migration from old fleet:/device: patterns

**Time:** 15 minutes to read

---

## 📖 Core Installation Guides

### [Client Installation Guide](CLIENT_INSTALLATION_GUIDE.md)

*Complete setup for field monitoring devices*

**Covers:**
- Hardware assembly (Arduino Opta + Blues Notecard + Analog Expansion)
- Arduino IDE configuration
- Blues Notehub setup
- Sensor wiring (4-20mA, 0-10V, digital)
- Firmware upload and verification
- Troubleshooting (compilation, upload, runtime, cellular)
- Advanced configuration (watchdog, intervals, sensor types)
- Field deployment checklist

**Prerequisites:**
- Arduino IDE 2.0+
- Blues Notehub account
- Basic understanding of sensors

**Time:** 45-60 minutes per device

---

### [Server Installation Guide](SERVER_INSTALLATION_GUIDE.md)

*Central dashboard and fleet management setup*

**Covers:**
- Server hardware assembly
- Network configuration (DHCP and static IP)
- Arduino IDE setup
- Blues Notehub integration
- Web dashboard access
- Client configuration interface
- Troubleshooting (network, compilation, Ethernet, dashboard)
- Security considerations
- Backup and recovery
- Monitoring and maintenance

**Prerequisites:**
- Ethernet network with DHCP
- Router/switch access
- Basic networking knowledge

**Time:** 45-60 minutes

---

### [Fleet Setup Guide](FLEET_SETUP_GUIDE.md)

*Multi-site deployment and device-to-device communication*

**Covers:**
- Fleet architecture overview
- Creating fleets in Blues Notehub
- Understanding fleet-based routing
- Pilot deployment (1-3 clients)
- Staged rollout (4-20 clients)
- Full production deployment (20+ clients)
- Scaling considerations
- Troubleshooting fleet-wide issues
- Best practices for large deployments

**Prerequisites:**
- Completed server installation
- At least one client deployed
- Understanding of Notehub basics

**Time:** Varies by fleet size (1-5 hours planning + deployment)

---

### Firmware Updates

#### [Firmware Update Guide](FIRMWARE_UPDATE_GUIDE.md)

*Over-the-air (OTA) firmware updates via Blues Notecard DFU*

**Covers:**
- Blues Notecard DFU overview
- Compiling firmware for DFU
- Uploading to Notehub
- Targeting specific devices or fleets
- Monitoring update progress
- Troubleshooting failed updates
- Rollback procedures
- Best practices for fleet updates

**Prerequisites:**
- Deployed devices (client and/or server)
- Notehub access
- New firmware binary

**Time:** 15-30 minutes (update propagation takes 1-6 hours)

---

## 🎨 Advanced Feature Guides

### [Sensor Calibration Guide](SENSOR_CALIBRATION_GUIDE.md)

*Precision sensor calibration for accurate measurements*

**Covers:**
- Calibration theory and linear interpolation
- Equipment and safety requirements
- Procedures for 4-20mA, 0-10V, and digital sensors
- Multi-point calibration techniques
- Dashboard and serial monitor calibration methods
- Verification and accuracy testing
- Troubleshooting calibration issues
- Best practices and maintenance schedules
- Calibration worksheet templates

**Prerequisites:**
- Installed client device
- Access to tank for physical measurements
- Measurement tools (dipstick, tape measure)

**Time:** 30-60 minutes per tank

---

### [Relay Control Guide](RELAY_CONTROL_GUIDE.md)

*Equipment automation and alarm-triggered relay control*

**Covers:**
- Arduino Opta relay specifications (4× 10A @ 250VAC)
- Electrical safety and wiring examples
- Inductive load protection (snubbers, flyback diodes)
- Configuration via bitmask relay masks
- Three operation modes: manual, automatic, device-to-device
- Advanced automation scenarios (pump protection, cascade fill)
- Troubleshooting relay and load issues
- Safety interlocks and testing procedures
- HTTP API reference for /api/relay endpoint

**Prerequisites:**
- Installed client with relay-capable Opta
- Understanding of electrical safety
- Equipment to control (pumps, valves, strobes)

**Time:** 1-2 hours for setup and testing

---

### [Unload Tracking Guide](UNLOAD_TRACKING_GUIDE.md)

*Fill-and-empty cycle detection for delivery monitoring*

**Covers:**
- Detection algorithm (peak tracking, drop detection, debouncing)
- Configuration parameters for unload tracking
- Tuning for different tank types (small, large, partial unloads)
- SMS/email notifications for delivery events
- API access to unload history
- Practical applications (fuel billing, milk collection, maintenance)
- Troubleshooting false triggers and missed detections
- Best practices and data management

**Prerequisites:**
- Calibrated sensors
- Understanding of tank usage patterns
- Access to dashboard or API

**Time:** 30-45 minutes configuration + testing period

---

## 🛡️ Operational Guides

### [Backup and Recovery Guide](BACKUP_RECOVERY_GUIDE.md)

*Protecting configurations and recovering from failures*

**Covers:**
- Understanding TankAlarm data storage (LittleFS, cloud, files)
- Manual and automated backup procedures
- Disaster recovery scenarios (client failure, server failure, site loss)
- Fleet-wide backup strategies
- Data preservation for compliance requirements
- Testing recovery procedures (annual drill checklist)
- 3-2-1 backup rule implementation
- Long-term archival strategies

**Prerequisites:**
- Running TankAlarm deployment
- Backup storage (USB drive, cloud, FTP server)
- Documentation templates

**Time:** 1-2 hours initial setup + ongoing maintenance

---

### [Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md)

*Comprehensive diagnostic procedures and problem resolution*

**Covers:**
- Quick decision trees for common issues
- Power and hardware diagnostics
- Cellular connectivity troubleshooting
- Network issues (server Ethernet)
- Sensor problems (zero readings, inaccuracy, fluctuations)
- Alarm configuration issues
- Dashboard and configuration problems
- Relay control troubleshooting
- Serial monitor diagnostics and interpretation
- Advanced diagnostics (Notecard commands, LittleFS, memory)
- Common error messages and solutions

**Prerequisites:**
- Access to affected device
- Serial monitor capability
- Multimeter (for advanced diagnostics)

**Time:** Varies by issue (5 minutes to 2 hours)

---

### [Dashboard Guide](DASHBOARD_GUIDE.md)

*Using the server web interface for monitoring and management*

**Covers:**
- Accessing the dashboard (finding IP, browsers, mobile)
- Dashboard layout and navigation
- Real-time tank monitoring and data freshness
- Client configuration console (remote settings)
- Calibration dashboard interface
- Manual and automatic relay control
- Historical data viewing and export
- Serial monitor dashboard
- Mobile optimization and home screen shortcuts
- Dashboard customization (HTML/CSS)
- API reference for integration
- Troubleshooting dashboard issues
- Security best practices

**Prerequisites:**
- Installed server with network access
- Web browser (desktop or mobile)
- Admin PIN configured

**Time:** 30 minutes to learn + ongoing use

---

## ⚡ Power User Guide

### [Advanced Configuration Guide](ADVANCED_CONFIGURATION_GUIDE.md)

*Expert-level customization and optimization*

**Covers:**
- Multiple sensor types per client (tanks, engines, pumps, flow)
- Custom object types and naming conventions
- Advanced sampling strategies (adaptive rates, event-based)
- Burst reporting during delivery
- Watchdog and stability tuning
- Task scheduling optimization
- Memory optimization for large fleets (RAM, LittleFS)
- Custom alarm logic (rate-of-change, time-based, predictive)
- Notecard communication optimization
- Sensor reading performance tuning
- Fleet management at scale (hierarchical config, bulk operations)
- Health monitoring dashboards

**Prerequisites:**
- Strong technical background
- Completed basic installation
- Comfortable with JSON and firmware modification
- Backup strategy in place

**Time:** Varies by customization (1-8 hours)

---

## 📋 Recommended Learning Paths

### Path 1: First-Time User (Complete Beginner)

**Goal:** Get a working system quickly, learn fundamentals

1. **[Quick Start Guide](QUICK_START_GUIDE.md)** - Get running in 30 minutes
2. **[Sensor Calibration Guide](SENSOR_CALIBRATION_GUIDE.md)** - Ensure accurate readings
3. **[Dashboard Guide](DASHBOARD_GUIDE.md)** - Learn to use the web interface
4. **[Client Installation Guide](CLIENT_INSTALLATION_GUIDE.md)** - Deep dive into client features
5. **[Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md)** - Bookmark for when issues arise

**Time:** 4-6 hours total

---

### Path 2: Experienced Arduino User (Fast Track)

**Goal:** Quickly deploy production system

1. **[Server Installation Guide](SERVER_INSTALLATION_GUIDE.md)** - Set up central dashboard
2. **[Client Installation Guide](CLIENT_INSTALLATION_GUIDE.md)** - Deploy field devices
3. **[Sensor Calibration Guide](SENSOR_CALIBRATION_GUIDE.md)** - Calibrate sensors
4. **[Fleet Setup Guide](FLEET_SETUP_GUIDE.md)** - Scale to production
5. **[Backup and Recovery Guide](BACKUP_RECOVERY_GUIDE.md)** - Protect your investment

**Time:** 3-5 hours total

---

### Path 3: System Administrator / Deployment Team

**Goal:** Understand architecture, deploy at scale, maintain fleet

1. **[Fleet Setup Guide](FLEET_SETUP_GUIDE.md)** - Understand architecture first
2. **[Quick Start Guide](QUICK_START_GUIDE.md)** - Rapid deployment reference
3. **[Dashboard Guide](DASHBOARD_GUIDE.md)** - Fleet monitoring interface
4. **[Backup and Recovery Guide](BACKUP_RECOVERY_GUIDE.md)** - Disaster recovery planning
5. **[Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md)** - Diagnostic procedures
6. **[Firmware Update Guide](FIRMWARE_UPDATE_GUIDE.md)** - Maintenance and updates
7. **[Advanced Configuration Guide](ADVANCED_CONFIGURATION_GUIDE.md)** - Optimization

**Time:** 8-12 hours total

---

### Path 4: Automation Specialist

**Goal:** Implement relay control and automated processes

1. **[Quick Start Guide](QUICK_START_GUIDE.md)** - Basic system setup
2. **[Relay Control Guide](RELAY_CONTROL_GUIDE.md)** - Equipment automation
3. **[Dashboard Guide](DASHBOARD_GUIDE.md)** - Remote relay control
4. **[Advanced Configuration Guide](ADVANCED_CONFIGURATION_GUIDE.md)** - Custom alarm logic
5. **[Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md)** - Relay diagnostics

**Time:** 5-7 hours total

---

### Path 5: Data Manager / Analyst

**Goal:** Leverage data collection for billing, compliance, analytics

1. **[Server Installation Guide](SERVER_INSTALLATION_GUIDE.md)** - Data aggregation setup
2. **[Sensor Calibration Guide](SENSOR_CALIBRATION_GUIDE.md)** - Ensure data accuracy
3. **[Unload Tracking Guide](UNLOAD_TRACKING_GUIDE.md)** - Delivery detection
4. **[Dashboard Guide](DASHBOARD_GUIDE.md)** - Data export and API access
5. **[Backup and Recovery Guide](BACKUP_RECOVERY_GUIDE.md)** - Long-term archival

**Time:** 4-6 hours total

---

## 🛠️ System Overview

### Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    Blues Notehub Cloud                       │
│                                                               │
│  ┌────────────────┐                  ┌────────────────┐     │
│  │ Fleet:         │                  │ Fleet:         │     │
│  │ tankalarm-     │                  │ tankalarm-     │     │
│  │ clients        │◄─────────────────┤ server         │     │
│  │                │  Configuration   │                │     │
│  │ • Client 1     ├─────────────────►│ • Server 1     │     │
│  │ • Client 2     │  Telemetry       │                │     │
│  │ • Client 3     │  Alarms          │                │     │
│  │ • ...          │                  │                │     │
│  └────────────────┘                  └────────────────┘     │
│         ↕                                     ↕               │
└─────────┼─────────────────────────────────────┼─────────────┘
          │                                     │
    Cellular/WiFi                          Ethernet
          │                                     │
┌─────────▼──────────┐              ┌──────────▼───────────┐
│  Client Device     │              │  Server Device       │
│                    │              │                      │
│  Arduino Opta      │              │  Arduino Opta        │
│  + Notecard        │              │  + Notecard          │
│  + Analog Exp.     │              │  + Ethernet          │
│                    │              │                      │
│  Monitors:         │              │  Provides:           │
│  • Tank levels     │              │  • Web dashboard     │
│  • 4-20mA sensors  │              │  • Data aggregation  │
│  • Alarms          │              │  • Configuration UI  │
│  • Up to 8 tanks   │              │  • SMS/Email alerts  │
└────────────────────┘              └──────────────────────┘
```

### Component Overview

| Component | Purpose | Quantity | Details |
|-----------|---------|----------|---------|
| **Server** | Central dashboard and fleet management | 1+ per region | Arduino Opta + Notecard + Ethernet |
| **Client** | Field monitoring device | 1+ per site | Arduino Opta + Notecard + Analog Expansion |
| **Blues Notecard** | Cellular/WiFi connectivity | 1 per device | Cellular or WiFi variant |
| **Blues Notehub** | Cloud routing and management | 1 account | Free tier available |
| **Analog Sensors** | Tank level measurement | 1-8 per client | 4-20mA or 0-10V |

---

## 🎯 Key Features

### Client Device Capabilities

- **Multi-Tank Monitoring**: Up to 8 tanks per device
- **Sensor Support**: 4-20mA, 0-10V, digital inputs
- **Flexible Sampling**: 5 seconds to 24 hours
- **Event-Based Reporting**: Immediate alerts on level changes
- **Remote Configuration**: Via server dashboard
- **Daily Reports**: Scheduled summary emails/SMS
- **Watchdog Protection**: Auto-recovery from hangs
- **Low Power**: Optimized for battery operation

### Server Device Capabilities

- **Web Dashboard**: Real-time monitoring interface
- **Fleet Management**: Configure unlimited clients
- **Alarm Processing**: SMS and email notifications
- **Data Aggregation**: Centralized telemetry storage
- **API Access**: RESTful endpoints for integration
- **Configuration UI**: Browser-based client setup
- **Network Flexibility**: DHCP or static IP
- **Persistent Storage**: Configuration survives restarts

### Cloud Features (Blues Notehub)

- **Device-to-Device**: Direct client-server messaging
- **Fleet Routing**: Automatic message delivery
- **OTA Updates**: Firmware updates without site visits
- **Event Logging**: Full history of all note traffic
- **Custom Routes**: Integration with external systems
- **Global Coverage**: Cellular connectivity worldwide

---

## 📖 Tutorial Features

All tutorials in this directory include:

- **✅ Clear Prerequisites**: Know what you need before starting
- **📝 Step-by-Step Instructions**: Never get lost
- **🔧 Troubleshooting Sections**: Solve common problems quickly
- **📊 Configuration Reference**: Quick lookup for settings
- **🔗 External Resources**: Links to related documentation
- **💡 Pro Tips**: Best practices from the field
- **📷 Visual Aids**: Diagrams and connection tables

---

## 🆘 Getting Help

### Quick Troubleshooting Reference

| Problem | Quick Fix | Full Guide |
|---------|-----------|------------|
| Dashboard not loading | Check Ethernet connection, verify IP | [Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md#dashboard-not-accessible) |
| Client not appearing | Wait 30 min for first telemetry | [Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md#dashboard-not-showing-all-clients) |
| Compilation errors | Install required libraries | [Client Installation Guide](CLIENT_INSTALLATION_GUIDE.md#step-3-install-required-libraries) |
| Sensor reading zero | Check wiring, power, and expansion | [Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md#readings-always-show-00) |
| Readings inaccurate | Calibrate sensors with known levels | [Sensor Calibration Guide](SENSOR_CALIBRATION_GUIDE.md) |
| Notecard not connecting | Verify SIM card and Product UID | [Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md#notecard-shows-disconnected) |
| Alarms not triggering | Check enabled, thresholds, contacts | [Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md#alarms-not-triggering) |
| Relay not activating | Verify command received, check wiring | [Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md#relays-not-activating) |
| Config changes not saving | Check client online, verify PIN | [Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md#configuration-changes-not-saving) |
| Firmware update failed | Check binary format and DFU mode | [Firmware Update Guide](FIRMWARE_UPDATE_GUIDE.md#troubleshooting) |

**For comprehensive diagnostics:** See the **[Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md)** with decision trees, serial monitor commands, and advanced diagnostics.

### Additional Resources

**External Documentation:**
- [Arduino Opta Documentation](https://docs.arduino.cc/hardware/opta)
- [Blues Wireless Developer Portal](https://dev.blues.io)
- [Blues Notehub Walkthrough](https://dev.blues.io/notehub/notehub-walkthrough/)

**Community Support:**
- [Blues Community Forum](https://community.blues.io)
- [Arduino Forum - Opta](https://forum.arduino.cc/c/hardware/opta/181)
- [GitHub Issues](https://github.com/SenaxInc/ArduinoSMSTankAlarm/issues)

**Video Tutorials:**
- [Blues Notecard Quickstart](https://www.youtube.com/blues_wireless)
- [Arduino Opta Getting Started](https://www.youtube.com/arduino)

---

## 📦 What's Included

### Tutorial Files

```
Tutorials/
├── README.md (this file - tutorial index)
│
├── Core Installation Guides/
│   ├── QUICK_START_GUIDE.md (~8,500 words)
│   ├── CLIENT_INSTALLATION_GUIDE.md (~10,000 words)
│   ├── SERVER_INSTALLATION_GUIDE.md (~12,000 words)
│   ├── FLEET_SETUP_GUIDE.md (~11,000 words)
│   └── FIRMWARE_UPDATE_GUIDE.md (~6,000 words)
│
├── Advanced Feature Guides/
│   ├── SENSOR_CALIBRATION_GUIDE.md (~6,000 words)
│   ├── RELAY_CONTROL_GUIDE.md (~7,500 words)
│   └── UNLOAD_TRACKING_GUIDE.md (~5,500 words)
│
├── Operational Guides/
│   ├── BACKUP_RECOVERY_GUIDE.md (~10,000 words)
│   ├── TROUBLESHOOTING_GUIDE.md (~15,000 words)
│   └── DASHBOARD_GUIDE.md (~10,000 words)
│
└── Power User Guides/
    └── ADVANCED_CONFIGURATION_GUIDE.md (~10,000 words)

Total: 13 comprehensive guides (~111,500 words)
```

### Firmware Locations

```
Project Root/
├── TankAlarm-112025-Client-BluesOpta/
│   ├── TankAlarm-112025-Client-BluesOpta.ino
│   ├── config_template.h
│   └── ... (supporting files)
│
├── TankAlarm-112025-Server-BluesOpta/
│   ├── TankAlarm-112025-Server-BluesOpta.ino
│   ├── server_config.h
│   └── ... (supporting files)
│
└── TankAlarm-112025-Common/
    └── ... (shared libraries)
```

---

## ⚙️ System Requirements

### Hardware Requirements

**Minimum Setup (Quick Start):**
- 1x Arduino Opta (server)
- 1x Arduino Opta (client)
- 2x Blues Notecard (cellular or WiFi)
- 2x Blues Notecarrier-F
- 1x Arduino Opta Analog Expansion
- 1x 4-20mA tank sensor
- Ethernet cable and network
- USB-C cables for programming
- Power supplies

**Production Setup:**
- Additional client devices as needed
- Redundant server (optional)
- Backup power (UPS)
- Managed network switch
- Antenna upgrades for cellular

### Software Requirements

**Required:**
- Arduino IDE 2.0 or later
- Arduino Mbed OS Opta Boards package
- ArduinoJson library (v7.x)
- Blues Wireless Notecard library
- Arduino_JSON library
- Ethernet library

**Optional:**
- FTP client (for server config backup)
- Serial terminal (PuTTY, screen, etc.)
- Web browser (for dashboard access)

### Network Requirements

**Server:**
- Ethernet connection
- DHCP-enabled network (or static IP capability)
- Open port 80 (HTTP) for dashboard
- Internet access for Notehub connectivity

**Client:**
- Cellular coverage (for cellular Notecard)
- WiFi network (for WiFi Notecard)
- No Ethernet required

### Cloud Requirements

- Blues Notehub account (free tier available)
- Product created in Notehub
- Fleets configured (tankalarm-server, tankalarm-clients)

---

## 🔄 Maintenance and Updates

### Regular Maintenance

**Weekly:**
- Check dashboard for anomalies
- Review alarm history
- Verify all clients reporting

**Monthly:**
- Review Notehub usage/billing
- Check for firmware updates
- Validate sensor calibrations

**Quarterly:**
- Physical site inspections
- Wiring and connection checks
- Backup server configurations

**Annually:**
- Major firmware updates
- Hardware refresh (if needed)
- Security audit

### Keeping Tutorials Updated

These tutorials are maintained alongside firmware releases:

| Tutorial | Version | Last Updated | Firmware Compatibility |
|----------|---------|--------------|------------------------|
| Quick Start | 1.0 | Jan 2026 | v1.0.0+ |
| Client Installation | 1.0 | Jan 2026 | v1.0.0+ |
| Server Installation | 1.0 | Jan 2026 | v1.0.0+ |
| Fleet Setup | 1.0 | Jan 2026 | v1.0.0+ |
| Firmware Update | 1.0 | Jan 2026 | v1.0.0+ |
| Sensor Calibration | 1.0 | Jan 2026 | v1.0.0+ |
| Relay Control | 1.0 | Jan 2026 | v1.0.0+ |
| Unload Tracking | 1.0 | Jan 2026 | v1.0.0+ |
| Backup and Recovery | 1.0 | Jan 2026 | v1.0.0+ |
| Troubleshooting | 1.0 | Jan 2026 | v1.0.0+ |
| Dashboard | 1.0 | Jan 2026 | v1.0.0+ |
| Advanced Configuration | 1.0 | Jan 2026 | v1.0.0+ |

---

## 🚦 Deployment Stages

### Stage 1: Proof of Concept
**Goal:** Validate system functionality  
**Duration:** 1-2 weeks  
**Devices:** 1 server + 1-2 clients  
**Guide:** [Quick Start Guide](QUICK_START_GUIDE.md)

### Stage 2: Pilot Deployment
**Goal:** Test in real-world conditions  
**Duration:** 2-4 weeks  
**Devices:** 1 server + 3-5 clients  
**Guide:** [Fleet Setup Guide](FLEET_SETUP_GUIDE.md#pilot-deployment-1-3-clients)

### Stage 3: Staged Rollout
**Goal:** Expand to small fleet  
**Duration:** 1-2 months  
**Devices:** 1 server + 5-20 clients  
**Guide:** [Fleet Setup Guide](FLEET_SETUP_GUIDE.md#staged-rollout-4-20-clients)

### Stage 4: Production Deployment
**Goal:** Full-scale operation  
**Duration:** Ongoing  
**Devices:** 1+ servers + 20+ clients  
**Guide:** [Fleet Setup Guide](FLEET_SETUP_GUIDE.md#full-production-20-clients)

---

## 📝 Feedback and Contributions

### Reporting Issues

Found a problem with a tutorial or have a suggestion?

1. **Documentation Issues**: [GitHub Issues](https://github.com/SenaxInc/ArduinoSMSTankAlarm/issues)
2. **Technical Support**: [Blues Community Forum](https://community.blues.io)
3. **Feature Requests**: Submit via GitHub Issues with `[ENHANCEMENT]` tag

### Contributing

We welcome contributions to improve these tutorials:

- **Corrections**: Grammar, technical accuracy, broken links
- **Examples**: Real-world deployment scenarios
- **Diagrams**: Wiring, architecture, flow charts
- **Troubleshooting**: Additional common issues and solutions

**How to Contribute:**
1. Fork the repository
2. Make your changes
3. Submit a pull request with clear description
4. Reference any related issues

---

## 📜 License

This documentation and associated firmware are licensed under [LICENSE](../LICENSE).

**Commercial Use:**  
These tutorials and firmware are provided for both personal and commercial use. Attribution is appreciated but not required.

---

## 🏆 Credits

**Development:**
- Firmware: Senax Inc.
- Documentation: Community contributors
- Platform: Blues Wireless (Notecard, Notehub)
- Hardware: Arduino (Opta platform)

**Special Thanks:**
- Blues Wireless for outstanding developer support
- Arduino for the industrial-grade Opta platform
- Community testers and early adopters

---

## 📧 Contact

**Project Maintainer:**  
Senax Inc.  
[GitHub Repository](https://github.com/SenaxInc/ArduinoSMSTankAlarm)

**Support Channels:**
- GitHub Issues (bug reports, feature requests)
- Blues Community (technical questions)
- Arduino Forum (hardware-specific questions)

---

**Ready to get started?** 🚀  
Head to the **[Quick Start Guide](QUICK_START_GUIDE.md)** and have your first system running in 30 minutes!

---

*Tutorial Library v1.0 | Last Updated: January 7, 2026*  
*Compatible with TankAlarm Firmware 1.0.0+*
