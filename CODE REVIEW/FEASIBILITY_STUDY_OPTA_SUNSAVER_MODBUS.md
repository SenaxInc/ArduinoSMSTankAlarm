# Feasibility Study: Arduino Opta RS485 & SunSaver MPPT Integration

**Date:** January 13, 2026
**Topic:** Integration of Morningstar SunSaver MPPT with Arduino Opta RS485 for Solar/Battery Monitoring
**Status:** **FINAL**

## 1. Objective
To use the Arduino Opta RS485 (Client/Master) to query a Morningstar SunSaver MPPT solar controller regarding:
- Solar Panel Voltage (Array Voltage)
- Battery Health (Voltage, Status)
- Alarm on degraded battery health

## 2. Hardware Architecture & Wiring

### 2.1. Component Compatibility
The Morningstar SunSaver MPPT utilizes a proprietary **MeterBus (RJ-11)** port. Additional hardware is required to interface with the Arduino Opta.

**RECOMMENDED HARDWARE (The "Pro" Solution):**
- **Morningstar MRC-1 (MeterBus to EIA-485 Adapter)**
- This single adapter connects directly to the SunSaver's RJ-11 port and provides industry-standard RS-485 screw terminals for the Opta.
- **Power:** It is powered **exclusively** by the SunSaver via the RJ-11 cable. No external DC-DC converter or 12V supply is required.
- **Relay Control:** It cannot (and should not) be switched on/off by the Opta. It is an "Always On" device.

**Alternative (Low Cost / DIY):**
- **Generic "TTL to RS-485" Module (with Auto-Flow Control)** (e.g., XY-017).
- *Warning:* Requires custom wiring of RJ-11 cable. **Must step down voltage** (do not connect SunSaver 12V pin to module 5V pin). Not isolated.

### 2.2. Wiring Diagram (Using MRC-1)
**Connection Chain:**
`Arduino Opta A/B` $\longleftrightarrow$ `Twisted Pair` $\longleftrightarrow$ `MRC-1` $\longleftrightarrow$ `RJ-11 Cable` $\longleftrightarrow$ `SunSaver`

| Arduino Opta RS485 | MRC-1 Terminal | Function |
| :--- | :--- | :--- |
| **A (-)** | **Terminal B (-)** | Data Negative (Inverted) |
| **B (+)** | **Terminal A (+)** | Data Positive (Non-Inverted) |
| **GND** | **Terminal G** | Signal Ground |
| *(Shield)* | *(Earth)* | Optional Shield Drain |

*Critical Note: Manufacturers often swap A/B labeling definitions. If communication fails, try swapping A and B wires at the Opta end.*

## 3. Modbus Protocol Implementation

### 3.1. Register Map (SunSaver MPPT)
The SunSaver MPPT uses **Modbus RTU**.
- **Slave ID:** Default is **1**.
- **Baud Rate:** Typically **9600** (Check device DIP switches if applicable).
- **Parity/Stop:** 8N2 is common for Modbus, or 8N1.

**Key Registers (Holding Registers, Function Code 03):**

| Parameter | Register (Decimal) | Address (Hex) | Scaling Calculation (12V System) |
| :--- | :--- | :--- | :--- |
| **Battery Voltage** | **19** | `0x0012` | $ V_{batt} = \frac{\text{Raw} \times 100}{32768} $ |
| **Array Voltage** | **20** | `0x0013` | $ V_{array} = \frac{\text{Raw} \times 100}{32768} $ |
| **Charge Current** | **17** | `0x0010` | $ I_{charge} = \frac{\text{Raw} \times 79.16}{32768} $ |
| **Faults** | **45** | `0x002C` | Bitfield (See Section 5) |
| **Alarms** | **47** | `0x002E` | Bitfield |
| **Charge State** | **44** | `0x002B` | 0=Start, 3=Night, 4=Fault, 5=Bulk, 6=Absorb, 7=Float |
| **Min Battery V** | **62** | `0x003D` | $ V_{min} = \frac{\text{Raw} \times 100}{32768} $ (Daily Min) |
| **Max Battery V** | **63** | `0x003E` | $ V_{max} = \frac{\text{Raw} \times 100}{32768} $ (Daily Max) |
| **Ah Daily** | **53** | `0x0034` | $ Ah = \text{Raw} \times 0.1 $ (Verify specific unit in datasheet) |
| **Heatsink Temp** | **28** | `0x001B` | Degrees C (Signed) |

*(Note: In `ArduinoModbus`, you typically use the Hex Address or (Decimal Register - 1).)*

### 3.2. Additional Detailed Data Points
Beyond basic voltage and health, the SunSaver offers these useful operational metrics:

1.  **System State (Register 44 / 0x2B):**
    - Knowing if the system is in **Float (7)** tells you the battery is fully charged.
    - Knowing if it is in **Night (3)** confirms solar is offline.
2.  **Energy Counters (Registers 53, 57):**
    - **Ah Daily (53):** Total charge put into the battery today. Great for efficiency tracking.
    - **kWh Daily (57):** Total energy harvested.
3.  **Temperature (Register 28 / 0x1B):**
    - **Heatsink Temperature:** Can indicate if the controller is overheating in a hot enclosure.
4.  **Load Data (If Load Terminals Used):**
    - **Load Current (18):** How much power the attached equipment is drawing.
    - **Load State (48):** Tells you if the controller cut power to the load due to low battery (LVD).

### 3.3. Modbus TCP vs Modbus RTU
- **Modbus RTU (Remote Terminal Unit):**
    - **Physical Layer:** RS-485 Serial (Differential signal).
    - **Encoding:** Binary, compact.
    - **Role:** Used between Opta and SunSaver.
- **Modbus TCP:**
    - **Physical Layer:** Ethernet.
    - **Encoding:** TCP/IP packets.
    - **Role:** The Opta could act as a Gateway, converting RTU data to TCP for a remote SCADA system, or simply process RTU data locally.

## 4. Solar Charging Requirements (12V AGM)

To charge a 12V AGM battery with MPPT:
- **Minimum Start Voltage:** $V_{batt} + 1.0V$
- **Operating Voltage:** $V_{batt} + \text{drop}$ (approx 0.5V headroom).
- **Recommendation:** Use "12V Nominal" panels which typically have a:
    - **Voc (Open Circuit):** ~21V
    - **Vmp (Max Power):** ~17-18V
- **Low Voltage Alarm:**
    - AGM batteries should generally not be discharged below **50% (approx. 12.0V - 12.2V)** to maximize lifespan.
    - **Alarm Threshold:** Trigger alert if $V_{batt} < 11.8V$ (sustained).

## 5. Alarm & Health Logic

**Degraded Health Detection:**
1.  **Low Voltage:** $V_{batt}$ drops below threshold (e.g., 11.5V) while solar is available (Daytime).
2.  **Fault Register (Address 0x002C):**
    - Any non-zero value is a concern.
    - **Bit 1:** Overcurrent.
    - **Bit 5:** Array High Voltage.

## 6. Software Implementation (Arduino Opta)

### 6.1. Libraries
- `ArduinoRS485` (Hardware Interface)
- `ArduinoModbus` (Protocol Layer)

### 6.2. Code Snippet Concept
```cpp
#include <ArduinoRS485.h>
#include <ArduinoModbus.h>

void setup() {
  Serial.begin(9600);
  
  // Start Modbus RTU Client (Master)
  // Baud rate matches SunSaver (usually 9600)
  if (!ModbusRTUClient.begin(9600)) {
    Serial.println("Failed to start Modbus RTU Client!");
    while (1);
  }
}

void loop() {
  // Read Battery Voltage (0x12)
  // Slave ID 1, Hold Register, Address 0x12, 1 Register
  if (ModbusRTUClient.requestFrom(1, HOLDING_REGISTERS, 0x0012, 1)) {
    uint16_t raw_kb = ModbusRTUClient.read();
    
    // Scaling for 12V system
    float battery_voltage = (raw_kb * 100.0) / 32768.0;
    
    Serial.print("Battery V: ");
    Serial.println(battery_voltage);
    
    // Alarm Logic
    if (battery_voltage < 11.8) {
       Serial.println("ALARM: Low Battery Health!");
       // Trigger SMS/Cloud Alert
    }
  } else {
    Serial.print("Modbus Error: ");
    Serial.println(ModbusRTUClient.lastError());
  }
  
  delay(5000);
}
```

## 7. Next Steps: Software Adaptation
To deploy this on the **TankAlarm-112025-Client-BluesOpta** project:

1.  **Hardware Purchase:** Acquire one (1) **Morningstar MRC-1 Adapter**.
2.  **Modbus Class:** Create a new C++ class (e.g., `SolarManager`) in the Client software to encapsulate Modbus queries.
3.  **Main Loop Integration:**
    - Add non-blocking Modbus polling (e.g., every 60 seconds).
    - Caution: RS485 reads are synchronous/blocking by default in some libraries. Ensure detailed timeout settings are low (e.g., 200ms) to avoid stalling the main Tank Alarm loop.
4.  **Alarm Trigger:**
    - Map `Min VBatt Daily` < 11.5V to a new Alarm Code (e.g., `ALARM_BATTERY_CRITICAL`).
    - Send this alarm via the existing Blues Wireless Notecard mechanism.
