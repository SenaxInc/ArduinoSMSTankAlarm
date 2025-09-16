# Wiring Diagram - Tank Alarm 092025

```
Arduino MKR NB 1500 Tank Alarm Wiring Diagram

                         ┌─────────────────────────────────┐
                         │        MKR RELAY SHIELD         │
                         │                                 │
                         │  Relay 1 Output:               │
                         │  ┌─ COM ──── [External Load]    │
                         │  └─ NO  ──── [External Load]    │
                         │                                 │
                         └─────────────┬───────────────────┘
                                       │
                         ┌─────────────┴───────────────────┐
                         │       MKR SD PROTO SHIELD       │
                         │                                 │
                         │  ┌─────────┐                    │
                         │  │ SD CARD │ ← Insert formatted │
                         │  │ SLOT    │   FAT32 SD card    │
                         │  └─────────┘                    │
                         │                                 │
                         │  Prototyping Area:              │
                         │  (Available for custom wiring)  │
                         └─────────────┬───────────────────┘
                                       │
                         ┌─────────────┴───────────────────┐
                         │      Arduino MKR NB 1500       │
                         │                                 │
                         │  Cellular Antenna ──┐          │
                         │                      │          │
                         │  SIM Card Slot ──────┘          │
                         │  ┌─────────┐                    │
                         │  │ SIM CARD│ ← Insert Hologram  │
                         │  │  SLOT   │   activated SIM     │
                         │  └─────────┘                    │
                         │                                 │
Pin 7  ──────────────────┤  D7         VCC ├──────────── +3.3V (Optional)
                         │                                 │
Ground ──────────────────┤  GND        GND ├──────────── Ground
                         │                                 │
USB Power ───────────────┤  USB        VIN ├──────────── External Power
                         │              │  │              (7-12V DC)
                         │              │  │                   │
                         └──────────────┼──┼───────────────────┘
                                        │  │
                         ┌──────────────┘  │
                         │                 │
              ┌──────────┴─────────┐       │
              │ Tank Level Sensors │       │
              │    (Choose One)    │       │
              └────────────────────┘       │
                                           │
Tank Sensor Options:                       │

Option 1: Digital Float Switch             │
┌─────────────────────────┐                │
│      Float Switch       │                │
│                         │                │
│  C (Common) ────────────┼────────────────┘
│  NO (Norm. Open) ───────┘
└─────────────────────────┘

Option 2: Analog Voltage Sensor (0.5-4.5V)
┌─────────────────────────┐                
│   Pressure Sensor       │                
│   (Dwyer 626 series)    │                
│                         │                
│  Signal+ ───────────────┼────────────────► Pin A1
│  Signal- ───────────────┼────────────────► GND
│  VCC ───────────────────┼────────────────► +3.3V
└─────────────────────────┘                

Option 3: 4-20mA Current Loop
┌─────────────────────────┐  ┌─────────────────────────┐
│   Current Loop Sensor   │  │  NCD.io I2C Module      │
│    (4-20mA output)      │  │  (4-channel receiver)   │
│                         │  │                         │
│  Current+ ──────────────┼──┼─► Channel 0 Input       │
│  Current- ──────────────┼──┼─► Channel 0 Return      │
└─────────────────────────┘  │                         │
                             │  SDA ───────────────────┼────► SDA (Pin 11)
                             │  SCL ───────────────────┼────► SCL (Pin 12)
                             │  VCC ───────────────────┼────► +3.3V
                             │  GND ───────────────────┼────► GND
                             └─────────────────────────┘

Power Options:
┌─────────────────────────────────────────────────────────────┐
│ Option 1: USB Power (Development/Testing)                  │
│ USB Cable ────► MKR NB 1500 USB Port                       │
│                                                             │
│ Option 2: Battery Power (Field Deployment)                 │
│ LiPo Battery ──► MKR NB 1500 Battery Connector             │
│                                                             │
│ Option 3: External Power Supply                            │
│ 7-12V DC ──────► MKR NB 1500 VIN Pin                       │
└─────────────────────────────────────────────────────────────┘

Sensor Wiring Detail:
┌─────────────────────────────────────────────────────────────┐
│ Float Switch Connection:                                    │
│                                                             │
│ Float Switch Types:                                         │
│ - SPST (Single Pole Single Throw)                          │
│ - Normally Open (NO) configuration preferred               │
│                                                             │
│ Wiring:                                                     │
│ ┌─────────────┐                                             │
│ │Float Switch │                                             │
│ │             │                                             │
│ │  C ●────────┼─── GND (Any ground pin)                     │
│ │  NO ●───────┼─── Pin 7 (with internal pullup)            │
│ │             │                                             │
│ └─────────────┘                                             │
│                                                             │
│ Logic:                                                      │
│ - Float down (normal): Pin 7 reads HIGH (pullup)           │
│ - Float up (alarm): Pin 7 reads LOW (switch closed)        │
│                                                             │
│ Note: Alarm condition is when sensor reads HIGH in code    │
│       Adjust TANK_ALARM_STATE in config if needed          │
└─────────────────────────────────────────────────────────────┘

Installation Notes:
┌─────────────────────────────────────────────────────────────┐
│ 1. Stack shields carefully, ensure all pins align          │
│ 2. Insert SIM card before powering on                      │
│ 3. Format SD card as FAT32 before installation             │
│ 4. Test all connections before sealing enclosure           │
│ 5. Use cable glands for weatherproof sensor connections    │
│ 6. Keep cellular antenna away from metal objects           │
│ 7. Consider external antenna for metal enclosures          │
└─────────────────────────────────────────────────────────────┘

Signal Flow:
┌─────────────────────────────────────────────────────────────┐
│ Tank Level ──► Float Switch ──► Pin 7 ──► Arduino          │
│                                             │               │
│ Arduino ──► Cellular Radio ──► Hologram.io ──► SMS         │
│    │                                                        │
│    └─────► SD Card (Local Logging)                         │
│    └─────► Relay Output (Alarm Indication)                 │
└─────────────────────────────────────────────────────────────┘
```

## Pin Usage Summary

| Pin | Function | Direction | Description |
|-----|----------|-----------|-------------|
| D7 | Digital Tank Sensor | Input | Float switch (with pullup) - DIGITAL_FLOAT mode |
| A1 | Analog Tank Sensor | Input | 0.5-4.5V pressure sensor - ANALOG_VOLTAGE mode |
| D11 (SDA) | I2C Data | I/O | I2C communication for current loop module |
| D12 (SCL) | I2C Clock | Output | I2C communication for current loop module |
| D5 | Relay Control | Output | Relay activation via MKR RELAY shield |
| D4 | SD Card CS | Output | SD card chip select (via MKR SD PROTO) |
| LED_BUILTIN | Status LED | Output | Visual status indication |
| VIN | External Power | Input | 7-12V DC external power supply |
| 3.3V | Sensor Power | Output | Power supply for analog/current loop sensors |
| GND | Ground | - | Common ground reference |
| USB | USB Power | Input | USB power and programming |

## Shield Stack Order (Bottom to Top)
1. **Arduino MKR NB 1500** (Base board with cellular radio)
2. **MKR SD PROTO Shield** (SD card + prototyping area)
3. **MKR RELAY Shield** (Relay control capability)

## External Connections
- **Cellular Antenna**: Built into MKR NB 1500 (external antenna optional)
- **Tank Sensor**: 2-wire connection to float switch
- **Power Supply**: USB, battery, or external DC supply
- **Relay Output**: Available via MKR RELAY shield terminals

---

*For detailed installation instructions, see INSTALLATION.md*
*For component specifications, see BillOfMaterials092025.md*