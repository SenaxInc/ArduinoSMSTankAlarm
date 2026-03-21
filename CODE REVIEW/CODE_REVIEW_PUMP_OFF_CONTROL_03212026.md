# Pump-Off Control (POC) Options - Architecture Review

**Date:** March 21, 2026
**Reviewer:** GitHub Copilot
**Target Document:** PUMP_OFF_CONTROL_OPTIONS_03202026.md

This document is a technical review of the proposed Pump-Off Control strategies for the TankAlarm platform.

## 1. Errors and Technical Pitfalls in the Current Proposal

### 💥 CRITICAL PITFALL: I2C over Long Cables (Option B & H)
**The Proposal:** Using an ADXL355 (an I2C sensor) on the walking beam via an I2C extension cable.
**The Reality:** I2C (Inter-Integrated Circuit) is strictly designed for short, on-board PCB traces (typically < 1 foot). Running raw I2C cables across a massive moving pumpjack in an electrically noisy industrial environment (motor VFDs, ignition coils) will result in massive data corruption, bus lockups, and total unreliability. Sharing this bus with the Notecard and A0602 is a recipe for system-wide hangs.
**The Fix:** Never run raw I2C off-board in this environment. If vibration sensing is required, use an industrial 4-20mA loop-powered accelerometer or an RS-485/Modbus vibration sensor. 

### ⚠️ PITFALL: Modbus Bus Sharing Compatibility (Option I)
**The Proposal:** Sharing the RS-485 bus between the SunSaver solar charger and the Altronic CD200 Ignition System.
**The Reality:** Modbus RTU requires all devices on a single daisy-chain bus to use the **exact same baud rate, parity, and stop bits**. If the SunSaver is fixed (e.g., 9600-8-N-1) and the CD200 requires a different baud rate (e.g., 19200) and cannot be changed, they cannot share the same RS-485 port on the Opta.
**The Fix:** Verify the serial configuration limits of both the SunSaver and Altronic CD200 before committing to a shared bus. A Modbus gateway or dual RS-485 ports may be required if they mismatch.

### ⚠️ PITFALL: Polling Jitter & Resolution (Option A & F)
**The Proposal:** Reading A0602 analog inputs via I2C polling (~50ms delay) for load signatures or using `PulseSamplerContext` for RPM.
**The Reality:** Pump-off signatures can involve fast transients (sudden RPM spikes or load drops). Polling an I2C-attached ADC module every 50ms yields only 20 samples per second, subject to I2C bus traffic jitter from the Notecard. This is too slow/noisy to build a high-fidelity "dynamometer-like" profile.
**The Fix:** For pulse counting (RPM), use the Opta's native hardware interrupts to guarantee timing accuracy instead of cooperative state machine polling. For current/load profiling, limit expectations strictly to macro-averages rather than micro-transient detection unless utilizing continuous ADC DMA buffers.

## 2. Better Ideas & Alternative Suggestions

### 💡 Better Idea 1: True Power (kW) instead of Current (Amps) for Electric Motors
For Option A (Electric Motors), measuring only motor current (Amps) via a CT is a legacy approach. As an AC induction motor unloads during a pump-off downstroke, its power factor drops significantly, meaning current stays artificially high even when the mechanical load vanishes.
*   **Recommendation:** Use a Modbus Power Meter or an analog True Power (kW) transducer instead of a simple CT. True Power (kW) factors in voltage and power factor, providing a dramatically sharper, more linear representation of the pump load.

### 💡 Better Idea 2: TinyML / Edge AI for Pattern Recognition
Instead of hardware-coding manual threshold values for RPM spikes or current drops (which require annoying manual tuning for every single well's unique geometry/depth), use the Arduino Opta's STM32H7 processor to run a lightweight machine learning model (e.g., via Edge Impulse). 
*   **Recommendation:** Feed a rolling window of normalized motor load or RPM into a TinyML anomaly detection model. The device can establish a baseline of a "normal fluid stroke" over 24 hours and automatically detect the mechanical deviations caused by fluid pound without manual threshold setup.

### 💡 Better Idea 3: Smart Motor Protection Relays (Electric)
Instead of processing raw analog 4-20mA current loops to deduce pump-off, rely on standard industrial smart relays (like an Allen-Bradley E300 or similar motor management relay) that already calculate load percentage and harmonic distortions natively. The Opta can simply read the "Load %" register via Modbus, offloading the high-speed data sampling entirely.

## 3. Recommended True Power (kW) Hardware Options

Here are the best standard industrial options for measuring True Power (kW) that integrate easily with the Arduino Opta and A0602 expansion architecture:

### Option A: Modbus RTU Power Meters (Connects via RS-485 to Opta)
These connect directly to the Opta's RS-485 port, bypassing the A0602 entirely. They provide digital precision and give access to kW, voltage, power factor, and frequency.
*   **Accuenergy Acuvim-L or Acuvim II Series (~$200 - $400):** Extremely popular, reliable Modbus RTU output, split-core CTs available. Requires panel space.
*   **NK Technologies APN Series (~$250 - $350):** Designed specifically as a blind remote sensor (no display). Outputs Modbus RTU. Very compact.
*   **Schneider Electric iEM3000 Series (~$150 - $300):** World-class reliability, DIN-rail mountable, native Modbus RTU.

*Integration Note:* Must ensure whichever meter is chosen can be set to the exact same baud rate, parity, and stop bits as any other device (like the SunSaver) on the Opta's single RS-485 bus.

### Option B: 4-20mA True Power Transducers (Connects to A0602 Analog Input)
These take the voltage and current lines, internally calculate True Power, and output a simple 4-20mA signal. This uses existing `readCurrentLoopSensor()` firmware with zero new protocol work.
*   **NK Technologies AP Series (~$180 - $250):** Outputs standard 4-20mA representing 0-100% full scale kW. Simple, robust, single-piece design.
*   **CR Magnetics CR6200 Series (~$150 - $250):** Bulletproof industrial sensor, outputs 4-20mA DC proportional to active power. Great for 1-phase or 3-phase.
*   **Ohio Semitronics PC5 / GW5 Series (~$200 - $400):** The gold standard for analog power transducers. Extremely fast response times (good for transient pump-off detection).

## 4. Recommended Path Forward

1.  **For Electric Motors:** Skip the raw CT (Option A) and instead use a **True Power (kW) Modbus meter** (or an analog Watts-transducer like the NK Technologies AP Series) feeding the A0602.
2.  **For Gas Engines (Arrow C-Series):** The **Altronic CD200 (Option I)** is brilliant for its rich diagnostics, assuming baud rates match the solar charger. As a fallback, use the **Inductive Proximity Sensor (Option F)** but tie it to a **hardware interrupt pin** on the Opta, NOT a software polling loop.
3.  **For Vibration Control:** Strongly discard the ADXL355 concept. If vibration dual-factor confirmation is required, upgrade to an industrial 4-20mA loop-powered vibration sensor.