# Pump-Off Control (POC) — Comprehensive Options & Implementation Plan

**Date:** March 20, 2026  
**Target Version:** 1.2.0+  
**Current Firmware:** 1.1.9  
**Platform:** Arduino Opta Lite (AFX00003) + Blues Notecard + A0602 Expansion

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Background — What Is Pump-Off Control?](#2-background--what-is-pump-off-control)
3. [Electric Motor Pumpjack Options](#3-electric-motor-pumpjack-options)
4. [Gas Engine Pumpjack Options](#4-gas-engine-pumpjack-options)
5. [Throttle Control — PID Speed Modulation](#5-throttle-control--pid-speed-modulation)
6. [Altronic Electronic Governor Integration](#6-altronic-electronic-governor-integration)
7. [Dedicated POC Opta Architecture](#7-dedicated-poc-opta-architecture)
8. [GOOD / BETTER / BEST Tier Options](#8-good--better--best-tier-options)
9. [Client Web UI Integration Plan](#9-client-web-ui-integration-plan)
10. [Firmware Implementation Plan](#10-firmware-implementation-plan)
11. [Code Examples](#11-code-examples)
12. [Bill of Materials Summary](#12-bill-of-materials-summary)
13. [Recommendations](#13-recommendations)

---

## 1. Executive Summary

Pump-Off Control (POC) is the ability to detect when a rod pump (pumpjack) is pumping a well dry ("pump-off") and respond by slowing or stopping the pump to prevent equipment damage and improve well production. This document evaluates all sensor, actuator, and architecture options discussed for adding POC capability to the TankAlarm platform.

**Key findings:**
- The existing A0602 expansion module has **DAC output (0-10V, 4-20mA) and PWM output** capabilities that are currently unused — these enable proportional throttle/speed control with zero additional hardware
- For electric motor pumpjacks, a **4-20mA split-core CT sensor** on the A0602 provides the simplest POC path with existing firmware patterns
- For gas engine pumpjacks, an **inductive proximity sensor** using the existing `SENSOR_PULSE` interface requires **zero new driver code**
- The Altronic **AGV5 Smart Gas Control Valve** accepts a standard 4-20mA input and can be driven directly by the A0602 current DAC for proportional throttle control
- A **dedicated POC Opta** communicating via direct Ethernet + GPIO to the Client Opta is the recommended architecture for high-accuracy POC with PID speed control
- POC sensors fit naturally as new `CurrentLoopSensorType` and `ObjectType` values in the existing monitor framework

---

## 2. Background — What Is Pump-Off Control?

### The Problem

A rod pump (pumpjack) lifts fluid from a wellbore. When the fluid level drops below the pump intake, the pump "pumps off" — the downstroke encounters no fluid resistance. This causes:

- **Fluid pound** — the traveling valve slams into the fluid column on the downstroke, creating shock loads that damage rods, couplings, and the pump barrel
- **Gas lock** — gas accumulation prevents the pump from moving fluid
- **Excessive wear** — rods, bearings, stuffing box, and gearbox wear accelerate
- **Wasted energy** — the prime mover runs at full speed doing no useful work
- **Reduced production** — paradoxically, over-pumping produces less oil than optimally-timed pumping

### The Solution

Detect pump-off via sensor data and respond by:
1. **Detection only** (alarm) — alert the operator
2. **Stop/restart cycling** — shut down the prime mover, wait for fluid recovery, restart
3. **Speed modulation** (PID control) — reduce pump speed to match inflow rate, never fully stopping

Option 3 is the most sophisticated and maximizes production, minimizes mechanical stress, and is the approach used by commercial rod pump controllers (Lufkin, Weatherford, etc.).

### Physics of Pump-Off Detection

| Prime Mover | Normal Operation | Pump-Off Signature |
|---|---|---|
| Electric motor | Draws 8-15A on downstroke (fluid load) | Current drops 20-40% on downstroke (no fluid) |
| Gas engine | RPM steady, governor maintains speed | RPM spikes on downstroke (engine unloads, governor lag) |
| Either | Consistent vibration pattern | Vibration pattern changes (impact, frequency shift) |
| Either | Stable rod load profile | Load drops on downstroke (dynamometer card collapses) |

---

## 3. Electric Motor Pumpjack Options

Electric motor pumpjacks are simpler to instrument because motor current is a direct, high-fidelity proxy for pump load. These installations typically have grid power, eliminating solar constraints.

### Option A: 4-20mA Split-Core CT Sensor (Current Transformer)

**How it works:** A split-core current transformer clamps around one phase wire of the pump motor. It outputs a 4-20mA signal proportional to motor current. The A0602 expansion reads this as a standard current loop input.

**Hardware:**
| Component | Example | Cost | Link |
|---|---|---|---|
| Split-Core CT (4-20mA output, 0-100A) | Magnelab SCT-0750-100 | $60-90 | [Magnelab](https://www.magnelab.com/split-core-current-transformer/) |
| Split-Core CT (4-20mA output, 0-50A) | CR Magnetics CR4395-50 | $70-100 | [CR Magnetics](https://www.crmagnetics.com/) |
| Split-Core CT (4-20mA output, 0-20A) | NK Technologies AT1-010-24L-SP | $80-120 | [NK Technologies](https://www.nktechnologies.com/) |

> **Note:** The KEMET C-CT-1216 (120A split-core, ~$14) is a **raw CT** that outputs millivolts AC — it requires external signal conditioning (burden resistor + rectifier + op-amp) to produce a usable DC signal. Not recommended. Use a self-powered 4-20mA CT transmitter instead.

**Pros:**
- Plugs directly into A0602 current loop input — uses existing `readCurrentLoopSensor()` path
- Non-invasive installation (clamp around wire, no electrical connection to motor circuit)
- 4-20mA signal is noise-immune over long cable runs
- Simple algorithm: baseline current vs. measured current = load percentage
- ~$60-120 sensor cost

**Cons:**
- Only works with electric motors (not gas engines)
- Single-phase current doesn't capture power factor changes (may miss partial pump-off)
- Motor current includes gearbox/belt losses — not a pure pump load signal
- Sampling rate limited by A0602 I2C read speed (~50ms per read)

**POC Accuracy:** 75-85% (simple threshold), 80-90% (with pattern analysis)

### Option B: ADXL355 Accelerometer (Vibration Analysis)

**How it works:** A MEMS accelerometer mounted on the walking beam or gearbox measures vibration. The vibration signature changes when pump-off occurs (loss of fluid damping, impact events). Uses FFT or RMS analysis to detect state changes.

**Hardware:**
| Component | Example | Cost | Link |
|---|---|---|---|
| ADXL355 Breakout Board | SparkFun or Adafruit ADXL355 | $25-35 | [Analog Devices](https://www.analog.com/en/products/adxl355.html) |
| Weatherproof Enclosure (for sensor) | Hammond 1554 series | $10-20 | Various |
| Shielded 4-wire cable (I2C extension) | Belden 9842 or equivalent | $1-2/ft | Various |

**Specs:**
- 20-bit resolution, ±2g/±4g/±8g range
- 22.5 µg/√Hz noise density
- I2C interface @ 0x1D or 0x53 (address pin selectable)
- 4 kHz max output data rate
- 200 µA typical current (excellent for solar)

**Pros:**
- Works with both electric and gas engine pumpjacks (prime-mover agnostic)
- Extremely low power draw (0.2 mW)
- Can detect pump-off, fluid pound, gas lock, rod parting, and bearing wear from vibration signatures
- Vibration trending enables predictive maintenance
- Relatively inexpensive ($25-35)

**Cons:**
- Requires new I2C driver (not an existing sensor type in firmware)
- Signal processing (FFT, RMS, peak detection) needs compute time
- Mounting location and coupling quality critically affect accuracy
- Requires self-calibrating baseline learning (24-48 hours)
- I2C bus shared with Notecard (0x17) and A0602 (0x64) — contention risk

**POC Accuracy:** 80-90% standalone, 85-95% with learning/calibration

### Option C: Combined CT + Accelerometer (Dual-Factor)

Uses both a 4-20mA CT on the motor **and** an ADXL355 on the walking beam. Pump-off is confirmed only when **both** sensors agree (current drop + vibration change), dramatically reducing false positives.

**Pros:**
- Highest accuracy of non-invasive approaches (92-97%)
- False positive events from one sensor are rejected by the other
- Redundancy — if one sensor fails, the other still works in degraded mode

**Cons:**
- Two sensors to install, wire, and maintain
- More complex algorithm (sensor fusion)
- Higher total cost ($85-155)

**POC Accuracy:** 92-97%

### Option D: Polished Rod Load Cell (Dynamometer)

**How it works:** A strain gauge load cell mounted at the polished rod measures the actual force on the rod string. This generates a surface dynamometer card (load vs. position) — the gold standard for pump diagnosis.

**Hardware:**
| Component | Example | Cost |
|---|---|---|
| Polished Rod Load Cell (4-20mA) | Lufkin, Weatherford, or custom | $500-2,000 |
| Position encoder (optional) | Rotary encoder on crank | $50-150 |

**Pros:**
- Industry gold standard — the same method used by $10K+ commercial controllers
- Generates actual dynamometer cards (load vs. position over one stroke)
- Can detect pump-off, gas lock, fluid pound, rod parting, traveling valve leak, standing valve leak, worn pump barrel — essentially everything
- 4-20mA output reads directly on A0602

**Cons:**
- Expensive ($500-2,000)
- Physical installation on the polished rod is involved (may require brief shutdown)
- Needs position reference (encoder or estimated from RPM) for full dynacard generation

**POC Accuracy:** 95-99%

### Option E: Wellhead Pressure Sensor (Supplementary)

**How it works:** A standard 4-20mA pressure transmitter on the tubing or casing head measures wellhead pressure. During pump-off, tubing pressure drops and casing pressure may change.

**Hardware:**
| Component | Example | Cost | Link |
|---|---|---|---|
| Pressure Transmitter (0-500 PSI, 4-20mA) | Dwyer 626-series | $80-150 | [Dwyer](http://www.dwyer-inst.com/) |

**Pros:**
- Uses existing current loop infrastructure — zero new code (it's just another `CURRENT_LOOP_PRESSURE` monitor)
- Provides well integrity data beyond POC (casing pressure trends, flowing vs. shut-in pressure)
- Can be added to any A0602 spare channel via normal server configuration UI

**Cons:**
- Slow response to pump-off (pressure changes lag by several strokes)
- Not definitive alone — pressure changes have many causes
- Best as supplemental confirmation signal, not primary POC sensor

**POC Accuracy:** 60-70% standalone (supplementary only)

---

## 4. Gas Engine Pumpjack Options

Gas engine pumpjacks (such as the [Arrow Engine C-Series](https://arrowengine.com/products/arrow-original-equipment/engines/c-series/all-pages)) do not have motor current to measure. Speed (RPM) and vibration become the primary detection signals. These installations are typically solar-powered and remote.

### Key Difference: Gas Engine Pump-Off Signature

When pump-off occurs on a gas engine pumpjack, the downstroke suddenly has no fluid load. The engine momentarily speeds up (before the governor compensates). This RPM transient is the primary pump-off signature.

### Option F: Inductive Proximity Sensor (RPM via Pulse Counting)

**How it works:** An inductive proximity sensor (or magnetic pickup) aimed at the engine flywheel teeth, belt sheave bolts, or a target on the walking beam counts pulses to measure RPM. Uses the existing `SENSOR_PULSE` firmware interface with the `PulseSamplerContext` cooperative state machine.

**Hardware:**
| Component | Example | Cost | Link |
|---|---|---|---|
| Inductive Proximity Sensor (NPN, 12-24V) | Omron E2E-X5ME1 or similar | $15-40 | [Omron](https://www.omron.com/) |
| Magnetic Pickup (MPU, for flywheel teeth) | Altronic 791054 | $30-60 | [Altronic](https://www.altronic-llc.com/) |

**Pros:**
- **Uses existing `SENSOR_PULSE` interface — zero new driver code**
- Existing `PulseSamplerContext` cooperative state machine handles non-blocking pulse counting
- Inexpensive ($15-60)
- Extremely reliable in harsh environments (no moving parts, sealed sensor)
- RPM data also enables stroke counting → production estimation
- Can detect overspeed conditions for safety shutdown

**Cons:**
- RPM alone misses subtle pump-off events where governor compensates quickly
- Single-cylinder engines have inherent RPM fluctuation (combustion pulses)
- Need to distinguish pump load RPM variation from normal engine RPM variation
- Mounting proximity sensor requires bracket fabrication

**POC Accuracy:** 70-80% (simple threshold), 75-85% (with pattern analysis)

### Option G: 4-20mA RPM Transmitter

**How it works:** A dedicated RPM-to-4-20mA transmitter (combines proximity sensor + signal conditioner) outputs a continuous analog signal proportional to engine speed. Reads directly on A0602 current loop input.

**Hardware:**
| Component | Example | Cost |
|---|---|---|
| RPM Transmitter (4-20mA output) | Monarch ROLS-P or Electro-Sensors 800 | $80-150 |

**Pros:**
- Clean 4-20mA output reads directly on A0602 (existing `CURRENT_LOOP` path)
- Continuous RPM value without pulse counting jitter
- Can add as `CURRENT_LOOP_RPM` type — minimal firmware changes

**Cons:**
- More expensive than raw proximity sensor ($80-150 vs. $15-40)
- Adds another device that needs power and wiring
- Duplicates capability available from Option F

**POC Accuracy:** 75-85%

### Option H: Combined Proximity + ADXL355 Accelerometer

RPM monitoring (Option F) plus vibration analysis (Option B). Same dual-factor approach as Option C but for gas engines.

**POC Accuracy:** 85-92%

### Option I: Altronic CD200 Ignition System (RPM via Modbus RTU) ⭐

**How it works:** The [Altronic CD200](https://www.altronic-llc.com/product/ignition-systems/cd200/) is a high-energy, digital, capacitor-discharge ignition system designed for 1- to 16-cylinder industrial gas engines. It replaces the standard Starfire CDI ignition on the Arrow C-Series. Critically, it includes **standard Modbus-RTU communications** — meaning the TankAlarm Client Opta can read RPM, diagnostics, and timing data directly from the ignition system over the existing RS-485 bus, with no separate proximity sensor or RPM transmitter needed.

Arrow explicitly lists Altronic ignition as an option on the C-Series:
> *"Also available as options are high-tension or solid-state magnetos, or Altronic ignition systems."*

**Hardware:**
| Component | Example | Cost | Link |
|---|---|---|---|
| Altronic CD200 Ignition (unshielded 70 Series) | CD200-70 | Call for pricing | [Altronic CD200](https://www.altronic-llc.com/product/ignition-systems/cd200/) |
| Altronic CD200 Ignition (shielded 80/90 Series) | CD200-80/90 | Call for pricing | [Altronic CD200](https://www.altronic-llc.com/product/ignition-systems/cd200/) |
| Magnetic Pickup (included or paired) | Altronic MPU | Included/bundled | [Altronic](https://www.altronic-llc.com/) |

**Key Specs:**
| Spec | Detail |
|---|---|
| **Cylinder Count** | 1 to 16 (Arrow C-Series = single cylinder ✅) |
| **Fuel Type** | Natural gas industrial engines ✅ |
| **Power** | DC powered (12V battery system ✅) |
| **Communications** | **Modbus-RTU (standard)** — RS-485, same bus as SunSaver solar charger |
| **Timing Reference** | Magnetic pickup on crankshaft/flywheel disc |
| **Features** | Adjustable output energy, automatic timing curves (RPM or analog input), overspeed setpoint, primary/secondary discharge diagnostics, LED diagnostics, Windows configuration tool |
| **Series 90** | Also supports Hall-effect pickup with magnet disc |

**Pros:**
- **Free RPM data over Modbus** — no separate proximity sensor or RPM transmitter needed; reads RPM registers from CD200 on the same RS-485 bus as SunSaver (different slave ID)
- **Arrow-compatible** — Altronic ignition is explicitly listed as an option on Arrow C-Series engines
- Replaces maintenance-intensive Starfire CDI with higher-energy capacitor-discharge system
- **Built-in overspeed setpoint** — ignition-level overspeed protection independent of Opta firmware (additional safety layer for PID throttle control)
- **Automatic timing adjustment curves** — can optimize ignition timing based on RPM or analog input, improving fuel efficiency during slow-pump POC mode
- **Analog control input** — could be driven by A0602 DAC (0-10V) to influence timing advance during different POC operating modes
- Extends spark plug life 3-5x vs. inductive ignition
- No moving parts (replaces mechanical distributor systems)
- Field-serviceable: Windows config tool + flashing LED diagnostics
- Modbus diagnostics enable remote ignition health monitoring via TankAlarm telemetry

**Cons:**
- More expensive than a simple proximity sensor ($15-40) — this is a full ignition system replacement
- Modbus register map not on public product page — need to request CD200 Modbus Protocol Document from Altronic to confirm register addresses, baud rate, and slave ID conventions
- Physical installation requires replacing the existing Starfire ignition and fabricating/installing a timing disc on the crankshaft or flywheel
- RS-485 bus shared with SunSaver solar charger — need distinct slave IDs and potentially managed polling intervals

**Integration with existing firmware:**
```cpp
// Read RPM from CD200 via Modbus RTU — same bus as SunSaver, different slave ID
// (Register address TBD — request CD200 Modbus Protocol Document from Altronic)
#define CD200_SLAVE_ID        2    // SunSaver is slave ID 1
#define CD200_RPM_REGISTER    0x00 // TBD from Altronic Modbus map

float readCd200Rpm(uint8_t slaveId) {
  if (!ModbusRTUClient.requestFrom(slaveId, HOLDING_REGISTERS, CD200_RPM_REGISTER, 1)) {
    return -1.0f;  // Read failed
  }
  uint16_t raw = ModbusRTUClient.read();
  return (float)raw;  // RPM value
}
```

**Connection diagram:**
```
Arrow C-Series Engine
  └─ Flywheel/Crankshaft ─── Timing Disc + Magnetic Pickup ─── CD200 Ignition
                                                                 │
                                                          RS-485 A/B wires
                                                                 │
                                                        ┌────────┴────────┐
                                                        │  RS-485 Bus     │
                                                        │  Slave 1: SunSaver
                                                        │  Slave 2: CD200  │
                                                        └────────┬────────┘
                                                                 │
                                                          Client Opta
                                                          (Modbus Master)
```

**Why this is compelling:** If a customer is already planning to upgrade their Arrow C-Series ignition to an Altronic system (common in the field for reliability), the CD200's Modbus interface provides RPM telemetry as a free bonus — the RS-485 wiring and Modbus polling infrastructure already exists in TankAlarm firmware for the SunSaver solar charger. This makes the GOOD tier for gas engine POC essentially **$0 incremental cost** on top of the ignition upgrade.

**POC Accuracy:** 75-85% (RPM threshold from Modbus data, same as Option F but with cleaner digital RPM vs. pulse counting)

---

## 5. Throttle Control — PID Speed Modulation

Instead of shutting the engine down completely when pump-off is detected, **PID control** modulates the engine speed continuously. This avoids:
- Thermal stress from cold-start cycling
- Belt/chain shock from stop/start
- Loss of wellbore control during shutdown
- Starting system wear
- Dead time (30-120s restart lag)

### A0602 Output Capabilities (Already In Hardware)

The A0602 expansion module has **unused output capabilities**:

| Output Type | Channels | Resolution | Range | API |
|---|---|---|---|---|
| Voltage DAC | Ch0-Ch7 (any channel) | 12-bit (8191 steps) | 0-11V | `pinVoltage(ch, volts)` |
| Current DAC | Ch0-Ch7 (any channel) | 12-bit (8191 steps) | 0-25mA | `pinCurrent(ch, mA)` |
| PWM | PWM0-PWM3 (4 channels) | Period + pulse (µs) | Configurable | `setPwm(ch, period, pulse)` |

**Safety feature:** The A0602 has a hardware timeout — if the Opta stops communicating, outputs revert to safe defaults:
```cpp
expansion.setTimeoutForDefaultValues(5000);       // 5-second timeout
expansion.setDefaultPinCurrent(ch, 4.0f);          // Revert to 4mA = idle
```

### Throttle Actuator Options

| Actuator | Interface | Cost | Notes |
|---|---|---|---|
| **Woodward L-150 Linear** | 4-20mA | $300-500 | Industry standard for gas engines |
| **Barber-Colman DYNA Rotary** | 4-20mA | $200-400 | Mounts on throttle shaft |
| **Thomson Electrak HD Linear** | 0-10V or PWM | $150-400 | Overkill force but cheap |
| **Savox SC-1256TG Servo** | PWM (50Hz) | $50-70 | Budget/prototype option |
| **Actuonix L16-P Linear Servo** | PWM (50Hz) | $70-120 | Linear, with feedback, IP54 |

A 4-20mA actuator driven directly from A0602 `pinCurrent()` is the cleanest path — no adapter needed.

### PID Operating Modes

| Mode | SPM Target | Throttle | Trigger |
|---|---|---|---|
| **NORMAL** | 15-20 SPM | PID maintains target | Pump running normally |
| **SLOW_PUMP** | 5-8 SPM | PID reduces speed | Pump-off detected |
| **IDLE** | 3-5 SPM | Minimum throttle | Extended pump-off (>5 min) |
| **RECOVERY** | Ramp 5→18 SPM over 30-60s | PID ramps gradually | Fluid load returns |
| **EMERGENCY_STOP** | 0 | Kill relay (digital) | Safety fault |

### Safety Layers

| Layer | Mechanism | Response |
|---|---|---|
| 1 | PID rate limiter | Max throttle change rate prevents slam |
| 2 | A0602 timeout default | Hardware reverts to idle if Opta hangs |
| 3 | Software watchdog | Opta resets → boots into safe idle |
| 4 | Kill relay (D2, normally-energized) | Power loss = engine stops (fail-safe) |
| 5 | Client Opta remote kill | Operator command via Notecard cellular |
| 6 | Mechanical governor (engine) | Engine's own governor as backstop |

---

## 6. Altronic Electronic Governor Integration

[Altronic LLC](https://www.altronic-llc.com/) (Girard, OH) manufactures ignition systems and engine controls specifically for natural gas-fueled industrial engines. They have an existing relationship with Arrow Engine (Altronic ignition is listed as an option on Arrow C-Series engines).

### Relevant Altronic Products

#### AGV5 Smart Gas Control Valve — **Best Opta Integration** ⭐

[Product Page](https://www.altronic-llc.com/product/controls/agv5-smart-gas-control-valve/)

> *"The AGV5 is a single-stage, electronically-actuated, balanced poppet fuel valve designed to act as an actuation device for a supervisory (typically PLC-based) speed control system. All valve control is derived from an industry-standard 4-20mA output signal generated by the governing controller."*

| Spec | Detail |
|---|---|
| **Interface** | **4-20mA input** (industry standard) |
| **Designed for** | External PLC/controller integration |
| **Function** | Controls gas fuel flow → controls engine speed |
| **Sizing** | Standard and extra-large, up to 10,000 HP |
| **Integration** | `A0602 Ch (current DAC) → 4-20mA → AGV5 → fuel line → engine` |

**Pros:**
- 4-20mA input maps **directly** to A0602 current DAC output — native interface
- Purpose-built for gas engine fuel control
- Designed for PLC integration (exactly our architecture)
- All governing logic stays in our firmware — AGV5 is a "dumb" proportional actuator
- Altronic is ISO 9001 certified, CSA rated, oilfield-proven

**Cons:**
- Designed for engines up to 10,000 HP — may be physically oversized for Arrow C-Series (5-32 HP)
- Likely expensive for a small engine application (call Altronic for pricing: +1 330-545-9768)
- Need to verify fuel flow sizing for low-HP single-cylinder engines

**Connection diagram:**
```
POC Opta → A0602 Ch7 (current DAC) → 4-20mA wiring → AGV5 → gas fuel line → Arrow carburetor
```

#### ActuCOM R8 — Engine Governor and Digital Actuator

[Product Page](https://www.altronic-llc.com/product/controls/actucom-r8/)

> *"While also suitable for service as an actuation device for a third-party engine governing control system or PLC, the ActuCOM R8 can be applied as a smart actuator for virtually any appropriate engine, compressor, or pump-related function."*

| Spec | Detail |
|---|---|
| **Type** | Rotary actuator with integral splined shaft |
| **Function** | Physically rotates throttle plate/butterfly |
| **Mounting** | Universal mount, drop-in replacement for existing governors |
| **Control** | Standalone governor OR accepts external PLC commands |
| **Integration** | Mounts on Impco carburetor throttle shaft |

**Pros:**
- Physically replaces mechanical governor on Arrow C-Series Impco carburetor
- Can operate as standalone governor (backup if Opta is offline)
- Robust machined aluminum construction for demanding environments

**Cons:**
- External control interface not confirmed as 4-20mA (may be serial/proprietary — need datasheet)
- More complex physical installation (throttle shaft coupling)
- May be overkill for 5-32 HP single-cylinder engine

#### GOV10/50 — NOT Applicable

[Product Page](https://www.altronic-llc.com/product/controls/gov10-50/)

Designed for fuel-injected engines 3,500-10,000 HP (Cooper Bessemer, Clark, White Superior). Not applicable to carbureted Arrow C-Series.

#### EPC-50/50e — Air/Fuel Ratio Controller (Supplementary)

[Product Page](https://www.altronic-llc.com/product/controls/epc-50-50e/)

Not speed control, but relevant for emissions compliance on small carbureted gas engines. Uses stepper motor valve + O2 sensor for closed-loop A/F ratio. 24VDC power (12-30V). Low horsepower, carbureted — matches Arrow C-Series exactly.

#### CD200 — Capacitor-Discharge Ignition with Modbus RTU ⭐

[Product Page](https://www.altronic-llc.com/product/ignition-systems/cd200/)

> *"The Altronic CD200 Series are high energy, digital, capacitor-discharge ignition systems designed for use on 1- to 16-cylinder industrial gas engines. Available in unshielded (70 Series) and shielded (80, 90 Series), these DC-powered systems eliminate maintenance-intensive mechanical distributor ignition systems."*

| Spec | Detail |
|---|---|
| **Cylinders** | 1 to 16 (single-cylinder Arrow C-Series ✅) |
| **Fuel** | Natural gas industrial engines ✅ |
| **Power** | DC (12V ✅) |
| **Comms** | **Modbus-RTU (standard)** — RS-485 |
| **Timing** | Magnetic pickup on crankshaft/flywheel timing disc |
| **Features** | Adjustable output energy, automatic timing curves (RPM or analog input), overspeed setpoint, primary/secondary diagnostics, LED diagnostics, Windows config tool |
| **Series 90** | Hall-effect pickup support (magnet disc) — replacement for Altronic DISN series |

**Pros:**
- **Provides RPM data over Modbus-RTU** — reads on the same RS-485 bus as the SunSaver, eliminating the need for a separate proximity sensor or RPM transmitter
- Direct replacement for Arrow C-Series Starfire CDI ignition (Altronic is an Arrow-listed option)
- Built-in overspeed setpoint adds an ignition-level safety layer for PID throttle control
- Automatic timing advance curves can optimize efficiency during slow-pump POC mode
- Analog control input could accept A0602 DAC 0-10V signal to adjust timing per POC operating mode
- Extends spark plug life 3-5x
- Remote diagnostics via Modbus — ignition health data flows through TankAlarm telemetry

**Cons:**
- Full ignition system replacement, not just a sensor — higher cost and installation effort
- Modbus register map must be requested from Altronic (not on public product page)
- RS-485 bus shared with SunSaver — needs distinct slave IDs and managed polling

**Integration:** `Client Opta (Modbus master) → RS-485 → CD200 (slave 2) → RPM register read`

#### DE-1550 — Small Engine/Compressor Monitoring System

[Product Page](https://www.altronic-llc.com/product/controls/de-1550/)

20-input monitoring system with digital, analog, and thermocouple inputs. Includes analog outputs. Could serve as an alternative engine monitoring platform, but our Opta-based approach is more flexible and integrated.

### Arrow C-Series Specific Notes

From [Arrow Engine C-Series](https://arrowengine.com/products/arrow-original-equipment/engines/c-series/all-pages):
- Models: C-46, C-66, C-96, C-101, C-106 (5-32 HP, 300-800 RPM)
- Single cylinder, belt drive, continuous duty, natural gas fueled
- Impco carburetors (cylinder heads modified for direct mounting)
- Starfire CDI ignition standard; **Altronic ignition optional** (confirms compatibility)
- Mechanical governor standard; electronic governor "for AC units only" (generator sets)
- 12V electric ring gear starter
- [Arrow Autostart NS-2](https://arrowengine.com/useful-info/autostart-ns-2) — Arrow's own start/stop automation panel (our PID approach is the upgrade path from this)

---

## 7. Dedicated POC Opta Architecture

### Why a Separate Opta?

The Client Opta's cooperative polling loop (10 Hz, 100ms sleep) is designed for tank monitoring. Adding high-frequency POC sampling creates timing conflicts:

| Factor | Single Opta | Dedicated POC Opta |
|---|---|---|
| Sampling rate | Limited by Notecard I2C blocking (50-200ms gaps) | Uninterrupted 50-200 Hz |
| Main loop jitter | Notecard, alarms, telemetry all compete | Clean, predictable timing |
| Failure isolation | POC bug can crash Notecard comms | POC failure doesn't affect telemetry |
| I2C contention | Notecard + A0602 + ADXL355 on one bus | Dedicated bus for sensors only |
| Code complexity | POC interleaved with everything | Clean separation of concerns |
| Cost | $0 extra | +$151 (Opta Lite) |

### Communication Between POC Opta and Client Opta

| Method | Feasible | Latency | Complexity | Notes |
|---|---|---|---|---|
| **Ethernet (direct cable)** | ✅ | <1ms | Medium | Both have RJ45; UDP on private subnet |
| **GPIO (relay → digital input)** | ✅ | <1ms | Very Low | POC relay = Client input; 2-4 wires |
| **Blues Notecard (cellular)** | ✅ | 2-30s | High | Two Notecards, two data plans |
| **RS-485 Modbus** | ✅* | 5-20ms | Low | *Requires Opta RS485 model (AFX00002) |
| **I2C Expansion Bus** | ❌ | N/A | N/A | Module-only, not Opta-to-Opta |
| **Serial/UART** | ❌ | N/A | N/A | No exposed UART ports on Opta Lite |

### Recommended: Ethernet + GPIO Hybrid

```
┌──────────────────────────────────┐     ┌──────────────────────────────────┐
│        POC OPTA (Dedicated)      │     │      CLIENT OPTA (Blues)         │
│        Opta Lite ($151)          │     │      Opta Lite ($151)            │
│                                  │     │                                  │
│  A0602 Expansion:                │     │  I2C Bus:                        │
│   ├─ Ch0-5: Sensor INPUTS       │     │   ├─ Notecard @ 0x17             │
│   │  (RPM xmitter, EGT, etc.)   │ ETH │   └─ A0602 @ 0x64               │
│   ├─ Ch7: 4-20mA DAC OUTPUT     │◄───►│                                  │
│   │  (throttle actuator)         │Direct│  Digital Inputs:                │
│   └─ PWM0: alt servo output     │Cable │   ├─ I1: POC Status (from POC)  │
│                                  │     │   ├─ I2: POC Fault (from POC)   │
│  Digital Inputs:                 │     │   └─ I3-I8: Float switches, etc. │
│   └─ I1: Proximity sensor (RPM) │     │                                  │
│                                  │     │  Relay Outputs:                  │
│  I2C:                            │     │   └─ D0-D3: Pump/valve/alarm     │
│   └─ ADXL355 @ 0x1D (vibration) │     │                                  │
│                                  │     │  Blues Notecard:                  │
│  Relays:                         │     │   └─ Cellular telemetry           │
│   ├─ D0 → Client I1 (status)    │     └──────────────────────────────────┘
│   ├─ D1 → Client I2 (fault)     │
│   └─ D2 → Engine kill circuit   │
│                                  │
│  Firmware:                       │
│   ├─ 5 Hz PID control loop      │
│   ├─ RPM measurement            │
│   ├─ Vibration FFT (ADXL355)    │
│   ├─ Pump-off state machine     │
│   └─ UDP status to Client Opta  │
└──────────────────────────────────┘
```

**GPIO channel (fail-safe, zero firmware complexity):**
- POC Relay D0 energized = pump running normal; de-energized = pump-off detected
- POC Relay D1 energized = POC system healthy; de-energized = sensor fault

**Ethernet channel (rich data, UDP port 5000):**
```json
{
  "type": "poc_status",
  "ts": 1735000000,
  "pump_state": "running",
  "confidence": 0.92,
  "rpm": 18.5,
  "rpm_baseline": 19.2,
  "vib_rms": 0.45,
  "vib_dominant_hz": 0.31,
  "strokes_today": 12847,
  "pumpoff_count_24h": 3,
  "throttle_ma": 12.4,
  "pid_mode": "NORMAL",
  "uptime_s": 86400
}
```

### Solar Power Budget (Both Optas, Remote Gas Engine Site)

| Component | Avg Power |
|---|---|
| Client Opta Lite | 500 mW |
| Blues Notecard (idle + bursts) | ~33 mW |
| A0602 expansion (client) | 100 mW |
| POC Opta Lite | 500 mW |
| A0602 expansion (POC) | 100 mW |
| ADXL355 accelerometer | 0.2 mW |
| Proximity sensor | 10 mW |
| **Total** | **~1.25 W** |

Daily consumption: 1.25W × 24h = **30 Wh/day**. A 50W panel with 4-5 peak sun hours produces 200-250 Wh/day — **~7-8x surplus**. A 30W panel is sufficient. Days of autonomy on 100Ah 12V battery: **~40 days**.

---

## 8. GOOD / BETTER / BEST Tier Options

### Electric Motor Pumpjack (Grid Power)

#### 🟢 GOOD — Single 4-20mA CT Sensor, Detection + Alarm Only

| | |
|---|---|
| **Sensors** | 1× 4-20mA split-core CT (Magnelab SCT-0750-100) |
| **Hardware** | Existing Client Opta + A0602 (no new Opta) |
| **Interface** | A0602 current loop input → new `CURRENT_LOOP_DIRECT_AMPS` type |
| **Action** | Detection + alarm via existing alarm/SMS pipeline |
| **Pump Control** | None — operator responds manually or existing timer does stop/start |
| **Firmware Changes** | Add `CURRENT_LOOP_DIRECT_AMPS` subtype + simple threshold algorithm |
| **Web UI Changes** | Add "Direct Amps" to current loop sensor subtype dropdown |
| **Added BOM** | ~$60-120 (CT sensor) |
| **Accuracy** | 75-85% |
| **Implementation** | ~100-150 lines of firmware changes, 1-2 weeks |
| **Best for** | Quick win, monitoring-only use case, no pump control authority needed |

#### 🟡 BETTER — CT + Accelerometer, Detection + Relay Control

| | |
|---|---|
| **Sensors** | 1× 4-20mA CT + 1× ADXL355 accelerometer |
| **Hardware** | Existing Client Opta + A0602 (no new Opta) |
| **Interface** | CT on A0602 channel + ADXL355 on I2C @ 0x1D |
| **Action** | Dual-factor detection → alarm + relay trigger (start/stop pump) |
| **Pump Control** | Binary stop/start via relay output (existing relay infrastructure) |
| **Firmware Changes** | ADXL355 driver, vibration RMS calculation, sensor fusion, relay trigger logic |
| **Web UI Changes** | New "POC Sensor" type, ADXL355 config fields, pump-off alarm config |
| **Added BOM** | ~$85-155 (CT + ADXL355 + breakout board + wiring) |
| **Accuracy** | 90-95% |
| **Implementation** | ~400-600 lines, 3-5 weeks |
| **Best for** | Reliable detection with automatic stop/start, moderate cost |

#### 🔵 BEST — Dedicated POC Opta + CT + Accelerometer + VFD Speed Control

| | |
|---|---|
| **Sensors** | 1× 4-20mA CT + 1× ADXL355 + (optional) polished rod load cell |
| **Hardware** | Dedicated POC Opta Lite ($151) + A0602 + Client Opta + VFD on motor |
| **Interface** | POC Opta A0602 DAC output → VFD analog speed reference (4-20mA or 0-10V) |
| **Action** | Continuous PID speed control — motor speed modulated to match inflow rate |
| **Pump Control** | Proportional speed control via VFD (pump never fully stops) |
| **Firmware Changes** | New POC Opta firmware, PID controller, Ethernet UDP comm, state machine |
| **Web UI Changes** | POC Opta as external device in config, PID tuning parameters, dynacard display |
| **Added BOM** | ~$500-1,500+ (POC Opta + A0602 + sensors + VFD + wiring) |
| **Accuracy** | 95-99% |
| **Implementation** | ~1,500-2,500 lines (new firmware), 6-10 weeks |
| **Best for** | Maximum production optimization, minimal mechanical stress, premium installations |

> **Note on VFD:** For electric motor speed control, a Variable Frequency Drive (VFD) is the standard approach. The POC Opta A0602 drives the VFD's 4-20mA or 0-10V analog speed reference input. The VFD handles motor power conversion. VFDs for pumpjack-size motors (5-50 HP) range from $300-2,000.

---

### Gas Engine Pumpjack (Solar Power)

#### 🟢 GOOD — Proximity Sensor (RPM), Detection + Alarm Only

| | |
|---|---|
| **Sensors** | 1× Inductive proximity sensor on flywheel/sheave |
| **Hardware** | Existing Client Opta (no new Opta, no A0602 needed for this) |
| **Interface** | Opta digital input → existing `SENSOR_PULSE` / `PulseSamplerContext` |
| **Action** | RPM monitoring + pump-off alarm via SMS/telemetry |
| **Pump Control** | None — operator responds or existing Arrow Autostart NS-2 handles stop/start |
| **Firmware Changes** | Add pump-off detection algorithm to pulse sensor evaluation |
| **Web UI Changes** | Add "Pump-Off Detection" checkbox to RPM monitor config |
| **Added BOM** | ~$15-40 (proximity sensor + bracket) |
| **Accuracy** | 70-80% |
| **Implementation** | ~50-100 lines (within existing pulse sensor framework), 1 week |
| **Best for** | Cheapest possible POC, leverages 100% of existing firmware |

#### 🟡 BETTER — Proximity + Accelerometer + Kill/Restart Relay

| | |
|---|---|
| **Sensors** | 1× Proximity sensor + 1× ADXL355 |
| **Hardware** | Existing Client Opta + A0602 (for ADXL355 power/signal or I2C direct) |
| **Interface** | Proximity on digital input + ADXL355 on I2C |
| **Action** | Dual-factor detection → alarm + engine kill relay → timed restart |
| **Pump Control** | Binary stop/start via relay (kill circuit + starter relay + timer) |
| **Firmware Changes** | ADXL355 driver, sensor fusion, engine stop/start state machine |
| **Web UI Changes** | POC config section, restart delay timer, ADXL355 settings |
| **Added BOM** | ~$40-75 (proximity + ADXL355 + wiring) |
| **Accuracy** | 85-92% |
| **Implementation** | ~400-600 lines, 3-5 weeks |
| **Best for** | Good accuracy with automatic stop/start, still uses single Opta |

#### 🔵 BEST — Dedicated POC Opta + AGV5 Fuel Valve + PID Speed Control

| | |
|---|---|
| **Sensors** | 1× Proximity sensor + 1× ADXL355 + (optional) EGT thermocouple |
| **Hardware** | Dedicated POC Opta Lite ($151) + A0602 ($229) + Altronic AGV5 fuel valve |
| **Interface** | POC Opta A0602 Ch7 current DAC → 4-20mA → AGV5 → fuel line → engine |
| **Action** | Continuous PID throttle control — engine speed modulated, never fully stops |
| **Pump Control** | Proportional fuel valve control (AGV5), fail-safe kill relay on Opta D2 |
| **Firmware Changes** | New POC Opta firmware, PID controller, Ethernet + GPIO comm, safety layers |
| **Web UI Changes** | POC Opta as external device, PID tuning, throttle status display |
| **Added BOM** | ~$400-1,000+ (POC Opta + A0602 + AGV5 + proximity + ADXL355 + wiring) |
| **Accuracy** | 92-97% |
| **Implementation** | ~1,500-2,500 lines (new firmware), 6-10 weeks |
| **Best for** | Maximum uptime, no engine cycling stress, professional-grade POC |

> **Alternative to AGV5:** If the AGV5 is oversized/overpriced for a small Arrow C-Series, substitute a Woodward L-150 linear actuator ($300-500) or Barber-Colman DYNA rotary actuator ($200-400) on the carburetor throttle linkage, both accepting 4-20mA. Or, if the engine already has an electronic governor with 4-20mA setpoint input, wire the A0602 DAC directly to it — $0 actuator cost.

---

## 9. Client Web UI Integration Plan

### Approach: POC as a New Sensor Type Within the Existing Monitor Framework

The existing configuration page (`/config-generator`) uses dropdown selects for sensor type, object type, and current loop subtype. POC integrates by extending these enums and adding conditional UI fields.

### 9.1 Enum Extensions

```cpp
// Extend SensorInterface — add new types
enum SensorInterface : uint8_t {
  SENSOR_DIGITAL = 0,
  SENSOR_ANALOG = 1,
  SENSOR_CURRENT_LOOP = 2,
  SENSOR_PULSE = 3,
  SENSOR_ACCELEROMETER = 4,   // NEW: ADXL355 vibration sensor
  SENSOR_EXTERNAL_OPTA = 5    // NEW: Dedicated POC Opta (Ethernet/GPIO)
};

// Extend CurrentLoopSensorType — add direct amps and RPM
enum CurrentLoopSensorType : uint8_t {
  CURRENT_LOOP_PRESSURE = 0,
  CURRENT_LOOP_ULTRASONIC = 1,
  CURRENT_LOOP_DIRECT_AMPS = 2,  // NEW: Direct 4-20mA CT (motor current)
  CURRENT_LOOP_RPM = 3           // NEW: 4-20mA RPM transmitter
};

// Extend ObjectType — add pumpjack
enum ObjectType : uint8_t {
  OBJECT_TANK = 0,
  OBJECT_ENGINE = 1,
  OBJECT_PUMP = 2,
  OBJECT_GAS = 3,
  OBJECT_FLOW = 4,
  OBJECT_PUMPJACK = 5,    // NEW: Pumpjack (pump + prime mover combined)
  OBJECT_CUSTOM = 255
};
```

### 9.2 Server Web UI — JavaScript Dropdown Updates

```javascript
// Updated constants in CONFIG_GENERATOR_HTML
const sensorTypes = [
  {value: 0, label: 'Digital Input (Float Switch)'},
  {value: 1, label: 'Analog Input (0-10V)'},
  {value: 2, label: 'Current Loop (4-20mA)'},
  {value: 3, label: 'Hall Effect RPM'},
  {value: 4, label: 'Accelerometer (ADXL355)'},      // NEW
  {value: 5, label: 'External POC Opta'}              // NEW
];

const currentLoopTypes = [
  {value: 'pressure',    label: 'Pressure Sensor (Bottom-Mounted)'},
  {value: 'ultrasonic',  label: 'Ultrasonic Sensor (Top-Mounted)'},
  {value: 'direct_amps', label: 'Motor Current (CT Sensor)'},     // NEW
  {value: 'rpm',         label: 'RPM Transmitter (4-20mA)'}       // NEW
];

const monitorTypes = [
  {value: 'tank',     label: 'Tank Level'},
  {value: 'gas',      label: 'Gas Pressure'},
  {value: 'rpm',      label: 'RPM Sensor'},
  {value: 'pumpjack', label: 'Pumpjack (POC)'}                   // NEW
];
```

### 9.3 Conditional UI Sections

When monitor type is "Pumpjack (POC)", show additional configuration fields:

```javascript
// Pump-Off Control configuration section (shown when monitorType === 'pumpjack')
function createPocConfigHtml(id) {
  return `
    <fieldset class="poc-config" style="display:none;">
      <legend>Pump-Off Control Settings</legend>

      <label class="field">
        <span>Prime Mover</span>
        <select class="poc-prime-mover" onchange="updatePocFields(${id})">
          <option value="electric">Electric Motor</option>
          <option value="gas_engine">Gas Engine</option>
        </select>
      </label>

      <label class="field">
        <span>POC Response</span>
        <select class="poc-response">
          <option value="alarm_only">Alarm Only (no pump control)</option>
          <option value="stop_start">Stop / Restart Cycling</option>
          <option value="speed_control">PID Speed Control</option>
        </select>
      </label>

      <label class="field">
        <span>Pump-Off Sensitivity</span>
        <select class="poc-sensitivity">
          <option value="low">Low (fewer alarms, may miss some events)</option>
          <option value="medium" selected>Medium (balanced)</option>
          <option value="high">High (more responsive, more false positives)</option>
        </select>
      </label>

      <label class="field poc-restart-field" style="display:none;">
        <span>Restart Delay (minutes)</span>
        <input type="number" class="poc-restart-delay" min="1" max="120" value="15">
      </label>

      <label class="field poc-pid-field" style="display:none;">
        <span>Normal Speed (SPM)</span>
        <input type="number" class="poc-normal-spm" min="1" max="30" value="18">
      </label>

      <label class="field poc-pid-field" style="display:none;">
        <span>Slow Pump Speed (SPM)</span>
        <input type="number" class="poc-slow-spm" min="1" max="15" value="6">
      </label>

      <label class="field poc-pid-field" style="display:none;">
        <span>Idle Speed (SPM)</span>
        <input type="number" class="poc-idle-spm" min="1" max="10" value="3">
      </label>

      <label class="field poc-external-field" style="display:none;">
        <span>POC Opta IP Address</span>
        <input type="text" class="poc-opta-ip" placeholder="10.0.0.1" value="10.0.0.1">
      </label>
    </fieldset>
  `;
}
```

### 9.4 External POC Opta as a "Virtual Sensor"

When `SENSOR_EXTERNAL_OPTA` is selected, the Client Opta reads POC status from the dedicated POC Opta via:
1. **GPIO input** (I1/I2) — binary pump status (always available)
2. **Ethernet UDP** — rich JSON data (RPM, vibration, confidence, throttle position)

The server configuration page would have an "External POC Opta" section:

```javascript
// External POC Opta settings (shown when sensorType === SENSOR_EXTERNAL_OPTA)
function createExternalOptaHtml(id) {
  return `
    <fieldset class="external-opta-config" style="display:none;">
      <legend>External POC Opta Connection</legend>

      <label class="field">
        <span>Status Input Pin</span>
        <select class="poc-status-pin">
          <option value="0">Opta I1</option>
          <option value="1">Opta I2</option>
        </select>
      </label>

      <label class="field">
        <span>Fault Input Pin</span>
        <select class="poc-fault-pin">
          <option value="1">Opta I2</option>
          <option value="2">Opta I3</option>
        </select>
      </label>

      <label class="field">
        <span>POC Opta IP Address</span>
        <input type="text" class="poc-opta-ip" value="10.0.0.1">
      </label>

      <label class="field">
        <span>UDP Port</span>
        <input type="number" class="poc-udp-port" value="5000" min="1024" max="65535">
      </label>
    </fieldset>
  `;
}
```

### 9.5 Server Dashboard — POC Display Widget

The server dashboard already displays monitors by `objectType` with icons. Adding pumpjack display:

```javascript
// In renderMonitorCard() — add pumpjack icon and POC status
case 'pumpjack':
  icon = '⛽';  // or custom SVG
  statusHtml = `
    <div class="poc-status">
      <span class="poc-state ${data.pump_state}">${data.pump_state}</span>
      <span class="poc-rpm">${data.rpm} SPM</span>
      <span class="poc-confidence">${(data.confidence * 100).toFixed(0)}%</span>
    </div>
  `;
  break;
```

### 9.6 Telemetry Payload Extension

```cpp
// New telemetry fields for POC monitors
case SENSOR_EXTERNAL_OPTA:
  doc["st"] = "poc";
  doc["ps"] = pocState;           // "running", "pumpoff", "idle", "recovery"
  doc["rpm"] = roundTo(rpm, 1);
  doc["conf"] = roundTo(confidence, 2);
  doc["sc"] = strokesToday;       // Stroke counter
  doc["poc"] = pumpoffCount24h;   // Pump-off events in 24h
  if (hasPidControl) {
    doc["thr"] = roundTo(throttleMa, 1);  // Throttle position (mA)
    doc["pm"] = pidMode;           // "NORMAL", "SLOW", "IDLE", "RECOVERY"
  }
  break;
```

---

## 10. Firmware Implementation Plan

### Phase 1: Detection Only — GOOD Tier (v1.2.0)

**Target: Simplest possible POC using existing firmware patterns**

1. Add `CURRENT_LOOP_DIRECT_AMPS` to `CurrentLoopSensorType` enum
2. Add `CURRENT_LOOP_RPM` to `CurrentLoopSensorType` enum
3. Add pump-off detection logic in `evaluateAlarms()` for current/RPM monitors
4. Update `readCurrentLoopSensor()` to handle direct amps mapping (4mA=0A, 20mA=100A)
5. Add `OBJECT_PUMPJACK` to `ObjectType` enum
6. Update server web UI dropdowns
7. Update telemetry to include POC-specific fields

**Estimated changes:**
- Client: ~150 lines (new sensor subtype handling + threshold logic)
- Server: ~50 lines (JS dropdown updates + display card)
- Common: ~10 lines (new enum values)

### Phase 2: Dual-Factor Detection + Relay Control — BETTER Tier (v1.2.1)

1. Add ADXL355 I2C driver in new `TankAlarm_Accelerometer.h`
2. Add `SENSOR_ACCELEROMETER` to `SensorInterface` enum
3. Add `readAccelerometerSensor()` to `readMonitorSensor()` dispatch
4. Implement vibration RMS and dominant frequency calculation
5. Implement sensor fusion (CT/RPM + vibration → pump-off confidence)
6. Add pump-off → relay trigger logic (engine kill, pump stop)
7. Add stop/restart timer state machine
8. Update server config UI with POC-specific fields

**Estimated changes:**
- Client: ~400-600 lines
- Server: ~100-150 lines (config UI + display)
- New file: `TankAlarm_Accelerometer.h` (~200-300 lines)

### Phase 3: Dedicated POC Opta + PID Control — BEST Tier (v1.3.0+)

1. Create new `TankAlarm-POC-Opta/` project directory
2. Implement POC Opta firmware:
   - A0602 initialization (input channels + output DAC channel)
   - ADXL355 I2C driver
   - Proximity sensor pulse counting
   - PID controller (setpoint tracking, anti-windup, rate limiting)
   - Pump-off detection state machine
   - Ethernet UDP server (status broadcast)
   - GPIO relay control (status + fault + kill)
   - Safety layers (watchdog, overspeed, A0602 timeout defaults)
   - Self-calibrating baseline learning (24-48h)
3. Add `SENSOR_EXTERNAL_OPTA` to Client firmware
4. Implement Ethernet UDP client on Client Opta
5. Implement GPIO input reading for POC status
6. Update server config UI with External Opta settings
7. Update server dashboard with POC display widget

**Estimated changes:**
- New project: ~1,500-2,500 lines (POC Opta firmware)
- Client: ~200-300 lines (external Opta comm)
- Server: ~200-300 lines (config UI + dashboard)

---

## 11. Code Examples

### 11.1 New CurrentLoopSensorType Handling (Phase 1)

```cpp
// In readCurrentLoopSensor() — add direct amps and RPM subtypes
static float readCurrentLoopSensor(const MonitorConfig &cfg, uint8_t idx) {
  MonitorRuntime &state = gMonitorState[idx];

  float mA = tankalarm_readCurrentLoopMilliamps(
    cfg.currentLoopChannel, gConfig.currentLoopI2cAddress);

  if (mA < 3.5f) {
    state.consecutiveFailures++;
    return state.hasLastValidReading ? state.lastValidReading : 0.0f;
  }
  state.consecutiveFailures = 0;
  state.currentSensorMa = mA;

  float result = 0.0f;

  switch (cfg.currentLoopType) {
    case CURRENT_LOOP_PRESSURE: {
      // Existing: map mA → PSI → inches of water
      float pressure = linearMap(mA, 4.0f, 20.0f, cfg.sensorRangeMin, cfg.sensorRangeMax);
      result = pressure * PSI_TO_INCHES_WATER;
      break;
    }
    case CURRENT_LOOP_ULTRASONIC: {
      // Existing: map mA → distance → level
      float distance = linearMap(mA, 4.0f, 20.0f, cfg.sensorRangeMin, cfg.sensorRangeMax);
      result = cfg.sensorMountHeight - distance;
      break;
    }
    case CURRENT_LOOP_DIRECT_AMPS: {
      // NEW: map mA → motor current in amps
      // sensorRangeMin = 0 (amps at 4mA), sensorRangeMax = 100 (amps at 20mA)
      result = linearMap(mA, 4.0f, 20.0f, cfg.sensorRangeMin, cfg.sensorRangeMax);
      break;
    }
    case CURRENT_LOOP_RPM: {
      // NEW: map mA → RPM
      // sensorRangeMin = 0 (RPM at 4mA), sensorRangeMax = 1000 (RPM at 20mA)
      result = linearMap(mA, 4.0f, 20.0f, cfg.sensorRangeMin, cfg.sensorRangeMax);
      break;
    }
    default:
      result = 0.0f;
      break;
  }

  state.lastValidReading = result;
  state.hasLastValidReading = true;
  return result;
}
```

### 11.2 Pump-Off Detection Algorithm (Phase 1 — Simple Threshold)

```cpp
// Add to MonitorConfig struct
struct PocConfig {
  bool pocEnabled;                // true = this monitor is used for pump-off detection
  float pocBaselineValue;         // Learned baseline (amps or RPM) during normal pumping
  float pocThresholdPercent;      // Trigger when value drops below baseline * (1 - threshold/100)
  uint8_t pocDebounceStrokes;     // Number of consecutive low-load strokes before triggering
  uint32_t pocRestartDelayMs;     // Delay before restart after pump-off (0 = alarm only)
  uint8_t pocRelayMask;           // Relays to trigger on pump-off (bit 0=D0, etc.)
  bool pocAutoLearn;              // true = automatically update baseline
};

// Simple pump-off detection (runs inside evaluateAlarms or sampleMonitors)
static void evaluatePumpOff(uint8_t idx) {
  const MonitorConfig &cfg = gConfig.monitors[idx];
  MonitorRuntime &state = gMonitorState[idx];

  if (!cfg.pocConfig.pocEnabled) return;

  float baseline = cfg.pocConfig.pocBaselineValue;
  if (baseline <= 0.0f) return;  // Not yet learned

  float threshold = baseline * (1.0f - cfg.pocConfig.pocThresholdPercent / 100.0f);
  float current = state.currentInches;  // Holds amps or RPM depending on sensor type

  if (current < threshold) {
    state.pocConsecutiveLow++;
    if (state.pocConsecutiveLow >= cfg.pocConfig.pocDebounceStrokes) {
      if (!state.pocTriggered) {
        state.pocTriggered = true;
        state.pocTriggerMillis = millis();
        // Send pump-off alarm
        sendAlarm(idx, "pumpoff", "Pump-off detected");
        // Trigger relay if configured
        if (cfg.pocConfig.pocRelayMask != 0) {
          triggerRelays(cfg.pocConfig.pocRelayMask, RELAY_MODE_UNTIL_CLEAR);
        }
      }
    }
  } else {
    state.pocConsecutiveLow = 0;
    if (state.pocTriggered) {
      state.pocTriggered = false;
      // Send pump recovery alarm
      sendAlarm(idx, "poc_clear", "Pump recovered");
      // Clear relay if configured
      if (cfg.pocConfig.pocRelayMask != 0) {
        clearRelays(cfg.pocConfig.pocRelayMask);
      }
    }
  }
}
```

### 11.3 ADXL355 I2C Driver Skeleton (Phase 2)

```cpp
// TankAlarm_Accelerometer.h — ADXL355 driver for vibration analysis

#ifndef TANKALARM_ACCELEROMETER_H
#define TANKALARM_ACCELEROMETER_H

#include <Wire.h>

#define ADXL355_DEFAULT_ADDR  0x1D  // ASEL pin LOW
#define ADXL355_ALT_ADDR      0x53  // ASEL pin HIGH

// Key registers
#define ADXL355_REG_DEVID_AD    0x00  // Should read 0xAD
#define ADXL355_REG_DEVID_MST   0x01  // Should read 0x1D
#define ADXL355_REG_PARTID      0x02  // Should read 0xED (ADXL355)
#define ADXL355_REG_STATUS      0x04
#define ADXL355_REG_XDATA3      0x08  // 20-bit X data (3 bytes)
#define ADXL355_REG_YDATA3      0x0B
#define ADXL355_REG_ZDATA3      0x0E
#define ADXL355_REG_RANGE       0x2C
#define ADXL355_REG_POWER_CTL   0x2D
#define ADXL355_REG_FILTER      0x28

struct AccelReading {
  float x_g;  // Acceleration in g
  float y_g;
  float z_g;
};

struct VibrationStats {
  float rms_g;           // RMS vibration magnitude
  float peak_g;          // Peak vibration in window
  float dominant_hz;     // Dominant frequency (from zero-crossing analysis)
  uint32_t sample_count; // Samples in this window
};

class ADXL355Driver {
public:
  bool begin(uint8_t addr = ADXL355_DEFAULT_ADDR) {
    _addr = addr;
    Wire.beginTransmission(_addr);
    if (Wire.endTransmission() != 0) return false;

    // Verify device ID
    uint8_t devid = readRegister(ADXL355_REG_DEVID_AD);
    if (devid != 0xAD) return false;

    // Set ±2g range (maximum sensitivity)
    writeRegister(ADXL355_REG_RANGE, 0x01);

    // Set ODR to 125 Hz (sufficient for pumpjack 0.3-1 Hz stroke rate + harmonics)
    writeRegister(ADXL355_REG_FILTER, 0x05);

    // Enable measurement mode
    writeRegister(ADXL355_REG_POWER_CTL, 0x00);

    return true;
  }

  AccelReading read() {
    AccelReading r = {0, 0, 0};
    uint8_t buf[9];
    readRegisters(ADXL355_REG_XDATA3, buf, 9);

    int32_t raw_x = ((int32_t)buf[0] << 12) | ((int32_t)buf[1] << 4) | (buf[2] >> 4);
    int32_t raw_y = ((int32_t)buf[3] << 12) | ((int32_t)buf[4] << 4) | (buf[5] >> 4);
    int32_t raw_z = ((int32_t)buf[6] << 12) | ((int32_t)buf[7] << 4) | (buf[8] >> 4);

    // Sign extend 20-bit to 32-bit
    if (raw_x & 0x80000) raw_x |= 0xFFF00000;
    if (raw_y & 0x80000) raw_y |= 0xFFF00000;
    if (raw_z & 0x80000) raw_z |= 0xFFF00000;

    // Convert to g (±2g range: 256000 LSB/g)
    const float scale = 1.0f / 256000.0f;
    r.x_g = raw_x * scale;
    r.y_g = raw_y * scale;
    r.z_g = raw_z * scale;

    return r;
  }

  // Compute vibration statistics over a sampling window
  // Call repeatedly from main loop; returns true when window is complete
  bool updateVibrationStats(VibrationStats &stats, uint32_t windowMs = 2000) {
    if (_windowStart == 0) {
      _windowStart = millis();
      _sumSq = 0.0f;
      _peak = 0.0f;
      _zeroCrossings = 0;
      _lastMag = 0.0f;
      _sampleCount = 0;
    }

    AccelReading r = read();
    float mag = sqrtf(r.x_g * r.x_g + r.y_g * r.y_g + r.z_g * r.z_g) - 1.0f; // Remove gravity
    _sumSq += mag * mag;
    if (fabsf(mag) > _peak) _peak = fabsf(mag);
    if (_sampleCount > 0 && ((_lastMag >= 0 && mag < 0) || (_lastMag < 0 && mag >= 0))) {
      _zeroCrossings++;
    }
    _lastMag = mag;
    _sampleCount++;

    if (millis() - _windowStart >= windowMs) {
      stats.rms_g = sqrtf(_sumSq / _sampleCount);
      stats.peak_g = _peak;
      float windowSec = (millis() - _windowStart) / 1000.0f;
      stats.dominant_hz = _zeroCrossings / (2.0f * windowSec); // Approximate
      stats.sample_count = _sampleCount;
      _windowStart = 0;  // Reset for next window
      return true;
    }
    return false;
  }

private:
  uint8_t _addr;
  unsigned long _windowStart = 0;
  float _sumSq = 0, _peak = 0, _lastMag = 0;
  uint32_t _zeroCrossings = 0, _sampleCount = 0;

  uint8_t readRegister(uint8_t reg) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(_addr, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
  }

  void readRegisters(uint8_t reg, uint8_t *buf, uint8_t len) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(_addr, len);
    for (uint8_t i = 0; i < len && Wire.available(); i++) {
      buf[i] = Wire.read();
    }
  }

  void writeRegister(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
  }
};

#endif // TANKALARM_ACCELEROMETER_H
```

### 11.4 PID Controller (Phase 3 — POC Opta Firmware)

```cpp
// PID configuration — stored in POC Opta config
struct PidConfig {
  float kp;                    // Proportional gain (start: 1.0)
  float ki;                    // Integral gain (start: 0.1)
  float kd;                    // Derivative gain (start: 0.02)
  float outputMin;             // Minimum throttle mA (e.g., 4.0 = idle)
  float outputMax;             // Maximum throttle mA (e.g., 20.0 = full)
  float setpointNormal;        // Normal pumping SPM (e.g., 18)
  float setpointSlowPump;      // Pump-off SPM (e.g., 6)
  float setpointIdle;          // Extended pump-off SPM (e.g., 3)
  float rampRateSpmPerSec;     // Max setpoint change rate (e.g., 0.5)
  uint32_t pumpoffSlowDelayMs; // Time in pump-off before IDLE (e.g., 300000 = 5 min)
  uint32_t recoveryRampMs;     // Ramp time slow → normal (e.g., 60000 = 60s)
  float integralWindupLimit;   // Anti-windup clamp (e.g., 50.0)
  float deadband;              // SPM error deadband (e.g., 0.5)
};

struct PidState {
  float integral;
  float prevError;
  float prevOutput;
  unsigned long lastUpdateMs;
};

float updatePid(const PidConfig &cfg, PidState &state, float setpoint, float measured) {
  unsigned long now = millis();
  float dt = (now - state.lastUpdateMs) / 1000.0f;
  if (dt <= 0.0f || dt > 2.0f) {
    state.lastUpdateMs = now;
    return state.prevOutput;
  }
  state.lastUpdateMs = now;

  float error = setpoint - measured;

  // Deadband
  if (fabsf(error) < cfg.deadband) error = 0.0f;

  // Proportional
  float pTerm = cfg.kp * error;

  // Integral with anti-windup
  state.integral += error * dt;
  state.integral = constrain(state.integral, -cfg.integralWindupLimit, cfg.integralWindupLimit);
  float iTerm = cfg.ki * state.integral;

  // Derivative (on measurement to avoid setpoint kick)
  float derivative = -(measured - (setpoint - state.prevError)) / dt;
  float dTerm = cfg.kd * derivative;

  // Sum and clamp output
  float output = pTerm + iTerm + dTerm;
  output = constrain(output, cfg.outputMin, cfg.outputMax);

  // Rate limit — prevent throttle slam
  float maxDelta = cfg.rampRateSpmPerSec * dt *
                   (cfg.outputMax - cfg.outputMin) / cfg.setpointNormal;
  output = constrain(output, state.prevOutput - maxDelta, state.prevOutput + maxDelta);

  state.prevError = error;
  state.prevOutput = output;
  return output;
}

// Write PID output to A0602 DAC for throttle control
void setThrottle(float mA) {
  // expansion is the AnalogExpansion object from Arduino_Opta_Blueprint
  expansion.pinCurrent(THROTTLE_DAC_CHANNEL, mA);
}
```

### 11.5 A0602 DAC Output Initialization (Phase 3)

```cpp
#include <Arduino_Opta_Blueprint.h>

#define THROTTLE_DAC_CHANNEL  7       // A0602 Ch8 (0-indexed)
#define THROTTLE_IDLE_MA      4.0f    // 4mA = engine idle
#define THROTTLE_MAX_MA       20.0f   // 20mA = full speed
#define A0602_SAFETY_TIMEOUT  5000    // 5-second comm failure timeout

void initThrottleOutput() {
  // Configure Ch7 as 4-20mA current DAC output
  OptaController.begin();
  AnalogExpansion expansion = OptaController.getExpansion(0);

  expansion.beginChannelAsCurrentDac(THROTTLE_DAC_CHANNEL);

  // Set safety timeout — if POC Opta crashes, A0602 reverts to idle
  expansion.setTimeoutForDefaultValues(A0602_SAFETY_TIMEOUT);
  expansion.setDefaultPinCurrent(THROTTLE_DAC_CHANNEL, THROTTLE_IDLE_MA);

  // Set initial output to idle
  expansion.pinCurrent(THROTTLE_DAC_CHANNEL, THROTTLE_IDLE_MA);

  Serial.println(F("Throttle DAC initialized on A0602 Ch8 (4-20mA output)"));
  Serial.print(F("  Safety timeout: "));
  Serial.print(A0602_SAFETY_TIMEOUT);
  Serial.println(F(" ms → revert to idle on comm failure"));
}
```

### 11.6 Ethernet UDP Communication (Phase 3 — POC Opta → Client Opta)

```cpp
// POC Opta side — broadcast status via UDP
#include <PortentaEthernet.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

#define POC_UDP_PORT     5000
#define CLIENT_IP        IPAddress(10, 0, 0, 2)
#define POC_IP           IPAddress(10, 0, 0, 1)
#define POC_SUBNET       IPAddress(255, 255, 255, 0)

EthernetUDP udp;

void initEthernet() {
  Ethernet.begin(nullptr, POC_IP, IPAddress(0,0,0,0), IPAddress(0,0,0,0), POC_SUBNET);
  udp.begin(POC_UDP_PORT);
}

void sendPocStatus(const char *pumpState, float confidence, float rpm,
                   float rpmBaseline, float vibRms, uint32_t strokesToday,
                   uint8_t pumpoffCount, float throttleMa, const char *pidMode) {
  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"type\":\"poc\",\"ts\":%lu,\"ps\":\"%s\",\"conf\":%.2f,"
    "\"rpm\":%.1f,\"rbl\":%.1f,\"vrms\":%.3f,"
    "\"sc\":%lu,\"poc\":%u,\"thr\":%.1f,\"pm\":\"%s\"}",
    (unsigned long)(millis() / 1000),
    pumpState, confidence, rpm, rpmBaseline, vibRms,
    (unsigned long)strokesToday, pumpoffCount, throttleMa, pidMode
  );

  udp.beginPacket(CLIENT_IP, POC_UDP_PORT);
  udp.write(buf);
  udp.endPacket();
}

// Client Opta side — receive POC status
void pollPocOpta() {
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    char buf[256];
    int len = udp.read(buf, sizeof(buf) - 1);
    buf[len] = '\0';

    // Parse JSON and update monitor state
    JsonDocument doc;
    if (deserializeJson(doc, buf) == DeserializationError::Ok) {
      gPocState.pumpState = doc["ps"].as<const char*>();
      gPocState.confidence = doc["conf"].as<float>();
      gPocState.rpm = doc["rpm"].as<float>();
      gPocState.vibRms = doc["vrms"].as<float>();
      gPocState.strokesToday = doc["sc"].as<uint32_t>();
      gPocState.lastUpdateMs = millis();
    }
  }
}
```

---

## 12. Bill of Materials Summary

### Electric Motor — by Tier

| Tier | Components | Est. Cost | Notes |
|---|---|---|---|
| **GOOD** | 4-20mA CT sensor | $60-120 | No new Opta; uses existing A0602 |
| **BETTER** | CT sensor + ADXL355 breakout + wiring | $85-155 | Same Opta; adds I2C sensor |
| **BEST** | POC Opta ($151) + A0602 ($229) + CT + ADXL355 + VFD ($300-2,000) | $825-2,555+ | Separate firmware; PID speed control |

### Gas Engine — by Tier

| Tier | Components | Est. Cost | Notes |
|---|---|---|---|
| **GOOD** | Inductive proximity sensor + bracket | $15-40 | No new Opta; uses existing `SENSOR_PULSE` |
| **GOOD** (alt) | Altronic CD200 ignition upgrade (RPM via Modbus) | $0 incremental* | *If already upgrading ignition; RPM data is free over RS-485 |
| **BETTER** | Proximity + ADXL355 + wiring | $40-75 | Same Opta; stop/start cycling |
| **BEST** | POC Opta ($151) + A0602 ($229) + proximity + ADXL355 + AGV5 or actuator ($200-500+) | $620-955+ | Separate firmware; PID throttle control |

### Altronic Products (Gas Engine — BEST Tier Add-Ons)

| Product | Function | Est. Cost | Link |
|---|---|---|---|
| AGV5 Smart Gas Control Valve | 4-20mA proportional fuel valve | Call for pricing | [Altronic AGV5](https://www.altronic-llc.com/product/controls/agv5-smart-gas-control-valve/) |
| ActuCOM R8 | Rotary throttle actuator/governor | Call for pricing | [Altronic ActuCOM R8](https://www.altronic-llc.com/product/controls/actucom-r8/) |
| EPC-50/50e | A/F ratio controller (emissions) | Call for pricing | [Altronic EPC-50](https://www.altronic-llc.com/product/controls/epc-50-50e/) |
| CD200 Ignition System | CD ignition + Modbus RPM telemetry | Call for pricing | [Altronic CD200](https://www.altronic-llc.com/product/ignition-systems/cd200/) |
| Magnetic Pickup (MPU) | RPM sensing on flywheel | $30-60 | [Altronic Controls](https://www.altronic-llc.com/product-category/controls/) |

> Contact Altronic: +1 (330) 545-9768 / info@altronic-llc.com

### Generic Throttle Actuators (Budget Alternative to Altronic)

| Actuator | Interface | Cost | Notes |
|---|---|---|---|
| Woodward L-150 Linear | 4-20mA | $300-500 | Industry standard |
| Barber-Colman DYNA Rotary | 4-20mA | $200-400 | Mounts on throttle shaft |
| Thomson Electrak HD Linear | 0-10V | $150-400 | High force, cheap |
| Actuonix L16-P Linear Servo | PWM (50Hz) | $70-120 | Budget, linear, IP54 |

### Sensors

| Sensor | Interface | Cost | Link |
|---|---|---|---|
| Magnelab SCT-0750-100 CT (0-100A, 4-20mA) | A0602 current loop | $60-90 | [Magnelab](https://www.magnelab.com/) |
| CR Magnetics CR4395-50 CT (0-50A, 4-20mA) | A0602 current loop | $70-100 | [CR Magnetics](https://www.crmagnetics.com/) |
| NK Technologies AT1-010-24L-SP CT (0-20A) | A0602 current loop | $80-120 | [NK Technologies](https://www.nktechnologies.com/) |
| ADXL355 Breakout Board | I2C @ 0x1D | $25-35 | [Analog Devices ADXL355](https://www.analog.com/en/products/adxl355.html) |
| Omron E2E-X5ME1 Proximity Sensor | Digital pulse | $15-40 | [Omron](https://www.omron.com/) |
| Monarch ROLS-P RPM Transmitter (4-20mA) | A0602 current loop | $80-150 | [Monarch Instruments](https://www.monarchinstrument.com/) |
| Dwyer 626-series Pressure Transmitter | A0602 current loop | $80-150 | [Dwyer](http://www.dwyer-inst.com/) |

---

## 13. Recommendations

### Immediate Priority (v1.2.0)

1. **Start with the GOOD tier for both electric and gas.** This is achievable in 1-2 weeks, requires minimal firmware changes, and validates the concept in the field before investing in more complex solutions.

2. **Add `CURRENT_LOOP_DIRECT_AMPS` and `CURRENT_LOOP_RPM` to the enum** — this gives users the ability to monitor motor current or engine RPM through the existing configuration UI. Even without pump-off detection logic, having the data in telemetry is valuable.

3. **Add `OBJECT_PUMPJACK` to ObjectType** — enables proper display and filtering on the server dashboard.

4. **For gas engine sites, the proximity sensor + existing `SENSOR_PULSE` is a free win** — the firmware already has the cooperative pulse sampler state machine. Users can monitor engine RPM with zero firmware changes beyond updating the server UI dropdowns.

### Medium Term (v1.2.1)

5. **Add the ADXL355 driver** — this is the highest-value new sensor investment. It works for both electric and gas, adds vibration trending for predictive maintenance, and the driver is reusable for any future vibration-based monitoring.

6. **Implement baseline learning and pump-off detection in the alarm evaluation path** — use the existing `evaluateAlarms()` debounce pattern (3 consecutive triggers) applied to current/RPM deviation from learned baseline.

### Longer Term (v1.3.0+)

7. **The dedicated POC Opta with PID control is the correct architecture for production-grade POC**, but it's a separate firmware project. Consider this for premium installations where the customer is willing to pay for the additional hardware.

8. **Contact Altronic re: AGV5 sizing for Arrow C-Series** — confirm a model is available for 5-32 HP single-cylinder engines before committing to that actuator path. The ActuCOM R8 may be the better physical fit.

9. **Request the CD200 Modbus Protocol Document from Altronic** — this is needed to confirm register addresses for RPM, timing advance, and diagnostics. If the customer is already upgrading to Altronic ignition, the CD200 Modbus interface makes the GOOD tier for gas engine POC essentially free (RPM data over the existing RS-485 bus). Also confirm baud rate and slave ID defaults to verify coexistence with SunSaver on the same bus.

10. **Production estimation from stroke counting** is a low-effort, high-value feature that falls out naturally from RPM monitoring — consider including this in v1.2.0 even without full POC detection.

### What NOT to Do

- **Don't use the KEMET C-CT-1216 raw CT** — it requires external signal conditioning. Use a self-powered 4-20mA CT transmitter instead.
- **Don't try to run high-speed POC sampling on the Client Opta alongside Notecard communication** — the I2C bus contention will cause unreliable readings. The dedicated POC Opta exists for this reason.
- **Don't implement PID speed control without the hardware safety layers** (A0602 timeout default, kill relay, overspeed shutdown). Controlling a gas engine throttle is safety-critical.

---

*Document prepared from analysis of TankAlarm firmware v1.1.9, Arduino Opta Lite hardware capabilities, A0602 expansion module (Arduino_Opta_Blueprint library), Arrow Engine C-Series specifications, and Altronic LLC product catalog (controls + CD200 ignition system).*
