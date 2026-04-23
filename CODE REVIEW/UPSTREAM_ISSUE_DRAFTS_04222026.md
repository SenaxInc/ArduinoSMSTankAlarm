# Upstream Library Issue Drafts — 2026-04-22

Two drafts to file with the official Arduino library maintainers, capturing the
RS-485 / Modbus issues we hit during SunSaver MPPT bring-up on the Arduino Opta.

File order: file the **ArduinoRS485** issue first (root cause), then reference
it from the **ArduinoModbus** issue.

---

## DRAFT 1 — `arduino-libraries/ArduinoRS485`

**Repo:** https://github.com/arduino-libraries/ArduinoRS485
**Title:** `Opta / mbed boards: last TX byte truncated because endTransmission() drops DE before UART shift register empties`
**Labels (suggested):** `bug`, `mbed`, `Opta`

### Body

#### Summary

On boards using the Arduino mbed core (Arduino Opta WiFi, Opta RS485, Portenta H7,
Nano 33 BLE, etc.), `RS485.endTransmission()` drops the DE (driver-enable) line
before the UART has finished clocking the final byte out of the shift register.
The result: the last byte of every transmission is truncated on the wire, the
slave's CRC check fails, and the slave silently rejects the frame.

The default `_postDelay` of `0` µs is the trigger. The library does call
`_serial->flush()`, but on the mbed core `Serial1.flush()` returns when the
TX **buffer** is empty — *before* the UART transmit-complete (TXC) flag asserts.
DE then drops mid-byte.

This affects every Modbus RTU user on the Opta. The workaround is well-known
in the community but not discoverable without burning a day on a logic analyzer.

#### Affected boards (confirmed or strongly suspected)

- Arduino Opta WiFi (AFX00002) — **confirmed**
- Arduino Opta RS485 (AFX00001) — same RS-485 hardware, expected same behavior
- Likely also: Portenta H7 + Portenta Machine Control, Nano 33 BLE shields,
  any board where `flush()` does not block on TXC

#### Reproduction

Minimal sketch (Opta WiFi, any Modbus RTU slave at 9600 8N2 — we used a
SunSaver MPPT via Morningstar MRC-1):

```cpp
#include <ArduinoRS485.h>
#include <ArduinoModbus.h>

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  ModbusRTUClient.begin(9600, SERIAL_8N2);
  ModbusRTUClient.setTimeout(500);
  // <-- comment this line out to reproduce the bug
  // RS485.setDelays(0, 1200);
}

void loop() {
  uint16_t v = 0;
  if (ModbusRTUClient.requestFrom(1, HOLDING_REGISTERS, 0x0008, 1) &&
      ModbusRTUClient.available()) {
    v = ModbusRTUClient.read();
    Serial.print("OK 0x"); Serial.println(v, HEX);
  } else {
    Serial.print("FAIL err="); Serial.println(ModbusRTUClient.lastError());
  }
  delay(1000);
}
```

**Without `setDelays(0, 1200)`:** every poll fails with `lastError() == 4`
(timeout). Logic-analyzer capture shows the final byte of the request is
truncated to ~half its normal width.

**With `setDelays(0, 1200)`:** every poll succeeds.

#### Root cause analysis

In `RS485.cpp`:

```cpp
size_t RS485Class::endTransmission() {
  _serial->flush();
  if (_postDelay > 0) {
    delayMicroseconds(_postDelay);
  }
  if (_dePin != -1) {
    digitalWrite(_dePin, LOW);
  }
  // ...
}
```

On AVR cores, `HardwareSerial::flush()` polls the UART data-register-empty
**and** the transmit-complete flag, so DE drops cleanly after the last byte
clears the shift register. On the mbed core, `arduino::UART::flush()` (and the
underlying `mbed::SerialBase`) only wait for the software TX buffer to drain;
the hardware shift register is still clocking when the call returns.

The current default of `_postDelay = 0` therefore works on AVR but breaks on
mbed.

#### Suggested fixes (in order of preference)

1. **Block on hardware TX-complete inside `endTransmission()` on mbed.**
   Use the mbed `SerialBase` API or write directly to the peripheral's TXC
   flag rather than relying on `Serial1.flush()` semantics. This is the
   correct, baud-rate-independent fix.

2. **Set a sane default `_postDelay` for mbed/Opta boards.**
   Computed from baud rate, e.g. one character time + margin:
   ```cpp
   #if defined(ARDUINO_OPTA) || defined(ARDUINO_PORTENTA_H7_M7) || \
       defined(ARDUINO_NANO33BLE)
     // 11 bits/char @ baud, in microseconds, plus 10% margin
     _postDelay = (11UL * 1100000UL) / baudrate;
   #endif
   ```
   At 9600 baud this is ~1260 µs (matches our empirically-validated 1200 µs);
   at 115200 it is ~105 µs.

3. **At minimum:** document this in the README under a "Known issues" or
   "Board-specific notes" section so users find it before the bug-hunt.

#### Workaround for current users

After `ModbusRTUClient.begin()` (which re-initializes the serial port and
overwrites RS485 settings), call:

```cpp
RS485.setDelays(0, 1200);   // 1200 µs covers 9600 8N1 (1042 µs) and 8N2 (1146 µs)
```

Note: it must be called **after** `ModbusRTUClient.begin()`, not before,
because begin() re-applies its own RS485 configuration.

#### References

- Arduino forum thread #1421875 post #18 — the post that finally pointed at
  `setDelays()` as the missing piece
- Our production fix in a SunSaver MPPT integration:
  https://github.com/SenaxInc/SenaxTankAlarm/blob/master/TankAlarm-112025-Common/src/TankAlarm_Solar.cpp
  (see `SolarManager::begin()`, line ~110)
- Bench-validated workaround commit: SenaxInc/SenaxTankAlarm@977ca70

#### Environment

- arduino-cli 1.x
- Core: `arduino:mbed_opta` (latest)
- Library: ArduinoRS485 (latest from master)
- Library: ArduinoModbus (latest from master)
- Board: Arduino Opta WiFi (AFX00002), FQBN `arduino:mbed_opta:opta`

---

## DRAFT 2 — `arduino-libraries/ArduinoModbus`

**Repo:** https://github.com/arduino-libraries/ArduinoModbus
**Title:** `Modbus RTU on Opta needs RS485.setDelays() — please document and/or expose a passthrough`
**Labels (suggested):** `documentation`, `enhancement`, `mbed`, `Opta`

### Body

#### Summary

`ModbusRTUClient.begin()` does not currently expose any way to set the
underlying `RS485.setDelays(preDelay, postDelay)` values, even though on
mbed-core boards (Arduino Opta especially) the default `postDelay` of `0`
causes the last byte of every Modbus query to be truncated on the wire and
the slave silently rejects the frame.

The result is a confusing failure mode: `ModbusRTUClient.requestFrom()`
returns false, `lastError()` returns `4` (timeout), and there is nothing in
the API or README that points users at the fix.

This issue is **upstream of the underlying RS485 bug** filed at
`arduino-libraries/ArduinoRS485#<NUMBER_TO_LINK>`. Even after that bug is
fixed in ArduinoRS485, the ArduinoModbus side is worth improving:

#### Symptoms (current behavior, Opta + SunSaver MPPT, default settings)

```
ModbusRTUClient.begin(9600, SERIAL_8N2);   // returns true
ModbusRTUClient.requestFrom(1, HOLDING_REGISTERS, 0x0008, 1);  // returns false
ModbusRTUClient.lastError();               // returns 4 (timeout)
```

Every call fails. The slave never sees a valid CRC because the last byte of
the request is corrupted.

#### Required workaround (today)

```cpp
ModbusRTUClient.begin(9600, SERIAL_8N2);
ModbusRTUClient.setTimeout(500);
RS485.setDelays(0, 1200);   // <-- MUST be after begin(), not before
```

Calling `RS485.setDelays()` *before* `ModbusRTUClient.begin()` does nothing,
because begin() re-applies its own RS485 init and overwrites the delays.
This ordering requirement is also undocumented.

#### Suggested fixes (any of these would help)

1. **Add a passthrough method.** Something like:
   ```cpp
   ModbusRTUClient.setRS485Delays(0, 1200);
   ```
   that internally calls `RS485.setDelays(...)` after begin() has finished.

2. **Apply a sensible default postDelay on mbed/Opta boards inside
   `ModbusRTUClient.begin()`.** Computed from baud rate (~11 bit-times):
   ```cpp
   #if defined(ARDUINO_OPTA) || defined(ARDUINO_PORTENTA_H7_M7)
     RS485.setDelays(0, (11UL * 1100000UL) / baudrate);
   #endif
   ```

3. **Document the requirement in the README** under "Using on Opta /
   mbed boards" with a code snippet, and call out the begin()-then-setDelays()
   ordering explicitly.

#### Why this matters

ArduinoModbus is the recommended Modbus library for the Opta, which Arduino
markets specifically for industrial RS-485 / Modbus applications. Having the
out-of-the-box configuration silently fail with a generic "timeout" error is
a significant onboarding obstacle — we lost ~2 days of bench time to this
before a forum thread pointed at `setDelays()`.

#### References

- Underlying RS-485 bug: `arduino-libraries/ArduinoRS485#<NUMBER_TO_LINK>`
- Arduino forum thread #1421875 post #18
- Working Opta + SunSaver MPPT integration:
  https://github.com/SenaxInc/SenaxTankAlarm/blob/master/TankAlarm-112025-Common/src/TankAlarm_Solar.cpp
- Minimal repro sketch:
  https://github.com/SenaxInc/SenaxTankAlarm/blob/master/firmware/sunsaver-modbus-test/sunsaver-modbus-test.ino

#### Environment

- arduino-cli 1.x
- Core: `arduino:mbed_opta` (latest)
- Library: ArduinoModbus (latest from master)
- Library: ArduinoRS485 (latest from master)
- Board: Arduino Opta WiFi (AFX00002), FQBN `arduino:mbed_opta:opta`
- Slave: Morningstar SunSaver MPPT via MRC-1 MeterBus->RS-485 adapter,
  9600 8N2, slave ID 1

---

## Filing checklist

Before posting:

- [ ] Search both repos' open + closed issues for "setDelays", "Opta",
      "postDelay", "last byte", "truncated", "CRC" — link or close as duplicate
      if any exist
- [ ] File the ArduinoRS485 issue first; note the issue number
- [ ] Substitute that issue number into the two
      `<NUMBER_TO_LINK>` placeholders in DRAFT 2
- [ ] File the ArduinoModbus issue
- [ ] (Optional) post a link to both issues on Arduino forum thread #1421875
      so the next person Googling the symptom finds the canonical fix
