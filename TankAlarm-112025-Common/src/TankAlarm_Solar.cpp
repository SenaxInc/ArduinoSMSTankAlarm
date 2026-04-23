/**
 * TankAlarm_Solar.cpp
 * 
 * SunSaver MPPT Solar Charger Monitoring Implementation
 * Uses ArduinoModbus library for Modbus RTU communication over RS-485
 * 
 * Copyright (c) 2025-2026 Senax Inc. All rights reserved.
 */

#include "TankAlarm_Solar.h"
#include "TankAlarm_Battery.h"

#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)

#include <ArduinoRS485.h>
#include <ArduinoModbus.h>

// Static message buffers for descriptions (avoid dynamic allocation)
static char _faultDescBuffer[128];
static char _alarmDescBuffer[128];

static bool readHoldingRegisters(uint8_t slaveId, uint16_t startAddress, uint8_t count, uint16_t *buffer) {
  if (!ModbusRTUClient.requestFrom(slaveId, HOLDING_REGISTERS, startAddress, count)) {
    return false;
  }

  for (uint8_t index = 0; index < count; ++index) {
    buffer[index] = (uint16_t)ModbusRTUClient.read();
  }

  return true;
}

static bool readInputRegisters(uint8_t slaveId, uint16_t startAddress, uint8_t count, uint16_t *buffer) {
  if (!ModbusRTUClient.requestFrom(slaveId, INPUT_REGISTERS, startAddress, count)) {
    return false;
  }

  for (uint8_t index = 0; index < count; ++index) {
    buffer[index] = (uint16_t)ModbusRTUClient.read();
  }

  return true;
}

static bool readRegistersWithFallback(uint8_t slaveId, uint16_t startAddress, uint8_t count, uint16_t *buffer) {
  if (readHoldingRegisters(slaveId, startAddress, count, buffer)) {
    return true;
  }
  return readInputRegisters(slaveId, startAddress, count, buffer);
}

SolarManager::SolarManager() 
  : _initialized(false), _lastPollMillis(0) {
  memset(&_data, 0, sizeof(SolarData));
  memset(&_config, 0, sizeof(SolarConfig));
  
  // Set defaults
  _config.enabled = false;
  _config.modbusSlaveId = SOLAR_DEFAULT_SLAVE_ID;
  _config.modbusBaudRate = SOLAR_DEFAULT_BAUD_RATE;
  _config.modbusTimeoutMs = SOLAR_DEFAULT_TIMEOUT_MS;
  _config.pollIntervalSec = SOLAR_DEFAULT_POLL_INTERVAL_SEC;
  _config.batteryLowVoltage = BATTERY_VOLTAGE_LOW;
  _config.batteryCriticalVoltage = BATTERY_VOLTAGE_CRITICAL;
  _config.batteryHighVoltage = BATTERY_VOLTAGE_HIGH;
  _config.alertOnLowBattery = true;
  // 2026-04-22: alertOnFault default flipped to FALSE because the status/fault
  // register addresses on this firmware revision are not yet verified and
  // returned implausible values during bench testing. Re-enable in user config
  // (or per-build override) once SS_REG_FAULTS / SS_REG_ALARMS are confirmed.
  _config.alertOnFault = false;
  _config.alertOnCommFailure = false;
  _config.includeInDailyReport = true;
}

bool SolarManager::begin(const SolarConfig& config) {
  if (!config.enabled) {
    _initialized = false;
    return true;  // Not an error, just disabled
  }
  
  setConfig(config);
  
  // Initialize Modbus RTU Client
  // Arduino Opta RS485 uses the built-in RS485 interface.
  // Modbus RTU spec requires 8N2 when no parity is used; SunSaver MPPT
  // (and Morningstar MRC-1 adapter) expect 8N2. ArduinoModbus default is
  // 8N1, which often appears to work but produces intermittent CRC errors.
  if (!ModbusRTUClient.begin(_config.modbusBaudRate, SERIAL_8N2)) {
    Serial.println(F("Solar: Failed to initialize Modbus RTU Client"));
    _initialized = false;
    return false;
  }
  
  // Set read timeout. Clamp to >=500 ms because SunSaver MPPT can take several
  // hundred ms to assemble a reply over the MRC-1 MeterBus->RS-485 bridge,
  // and the default 200 ms in older saved configs causes the client to
  // give up before the reply arrives.
  if (_config.modbusTimeoutMs < 500) {
    _config.modbusTimeoutMs = 500;
  }
  ModbusRTUClient.setTimeout(_config.modbusTimeoutMs);

  // RS-485 timing fix per Arduino forum thread #1421875 post #18:
  // The Opta needs a post-TX delay of one full character time before DE drops,
  // otherwise the last byte of the Modbus query is corrupted on the wire and
  // the slave silently rejects it. setDelays(pre, post) is in microseconds.
  // Use 1200 us as a safe upper bound (covers 9600 8N1=1042 us, 9600 8N2=1146 us).
  // The previous (50, 50) value was 50 us post-delay -- ~20x too short.
  RS485.setDelays(0, 1200);
  
  Serial.print(F("Solar: Modbus RTU initialized at "));
  Serial.print(_config.modbusBaudRate);
  Serial.print(F(" baud 8N2, slave ID "));
  Serial.print(_config.modbusSlaveId);
  Serial.print(F(", timeout "));
  Serial.print(_config.modbusTimeoutMs);
  Serial.println(F(" ms"));
  
  _initialized = true;
  _data.communicationOk = false;
  _data.consecutiveErrors = 0;
  
  // Probe both holding and input register models before first full poll.
  uint16_t startupProbe = 0;
  bool startupHolding = readHoldingRegisters(_config.modbusSlaveId, SS_REG_BATTERY_VOLTAGE, 1, &startupProbe);
  bool startupInput = false;
  if (!startupHolding) {
    startupInput = readInputRegisters(_config.modbusSlaveId, 0x0008, 1, &startupProbe);
  }

  if (startupHolding) {
    Serial.println(F("Solar: Startup probe OK via FC03 (holding)"));
  } else if (startupInput) {
    Serial.println(F("Solar: Startup probe OK via FC04 (input)"));
  } else {
    Serial.println(F("Solar: Startup probe failed for both FC03 and FC04"));
  }

  // Do an initial full read to populate the data cache when possible.
  if (!readRegisters()) {
    Serial.println(F("Solar: Modbus transport initialized, but full initial read failed"));
  }
  
  return true;
}

void SolarManager::end() {
  if (_initialized) {
    ModbusRTUClient.end();
    _initialized = false;
  }
}

void SolarManager::setConfig(const SolarConfig& config) {
  _config = config;
}

bool SolarManager::poll(unsigned long nowMillis) {
  if (!_config.enabled || !_initialized) {
    return false;  // Not active — no data
  }
  
  // Check if it's time to poll
  unsigned long intervalMs = (unsigned long)_config.pollIntervalSec * 1000UL;
  if (nowMillis - _lastPollMillis < intervalMs) {
    return false;  // Not due yet — use isCommunicationOk() to distinguish from failure
  }
  
  _lastPollMillis = nowMillis;
  readRegisters();
  return true;
}

bool SolarManager::readRegisters() {
  bool success = true;
  SolarData nextData = _data;

  // ==========================================================================
  // 2026-04-22 FIELD-READY POLICY:
  // Only the live/filtered ADC block at 0x0008..0x000C has been bench-verified
  // on this SunSaver MPPT firmware revision. The previously-coded addresses
  // for temperature (0x001B/0x001C), status (0x002B/0x002C/0x002E/0x002F),
  // daily Ah (0x0034) and daily V min/max (0x003D/0x003E) all returned either
  // zeros or values that look like extra voltage snapshots, NOT the documented
  // SunSaver semantics. Reading them and feeding the result into derived
  // health flags (chargeState/faults/alarms/heatsinkTemp) caused false
  // "FAULT" indications in the bench capture (e.g. faults=0x4235 with no
  // physical fault). Until the addresses are confirmed against a Morningstar
  // datasheet for this exact firmware, we skip those reads entirely.
  //
  // To re-enable for bench experimentation, define SOLAR_ENABLE_UNVERIFIED_REGISTERS.
  // ==========================================================================

  // Real-time block: 5 contiguous filtered ADC registers starting at adc_vb_f
  //   0x0008 batt V, 0x0009 array V, 0x000A load V, 0x000B charge I, 0x000C load I.
  uint16_t realtimeRegs[5];
  if (readRegistersWithFallback(_config.modbusSlaveId, SS_REG_BATTERY_VOLTAGE, 5, realtimeRegs)) {
    nextData.batteryVoltage = scaleVoltage(realtimeRegs[0]);
    nextData.arrayVoltage   = scaleVoltage(realtimeRegs[1]);
    // realtimeRegs[2] is load voltage (adc_vl_f) -- not currently exposed in SolarData.
    nextData.chargeCurrent  = scaleCurrent(realtimeRegs[3]);
    nextData.loadCurrent    = scaleCurrent(realtimeRegs[4]);
  } else {
    success = false;
  }

  // Charge setpoints — read once per session after first successful realtime
  // read, used by verifyChemistry() to confirm the SunSaver DIP-switch chemistry
  // matches what the user selected in the web UI. The setpoints reflect the
  // controller's active battery service (Sealed/Gel/Flooded/Custom) and only
  // change at boot or DIP-switch change. Best-effort: failure does not mark the
  // overall poll as failed.
  if (success && !nextData.setpointsValid) {
    uint16_t setpointRegs[4];  // V_reg, (gap), V_float, V_eq
    if (readRegistersWithFallback(_config.modbusSlaveId, SS_REG_V_REG, 4, setpointRegs)) {
      float vReg   = scaleVoltage(setpointRegs[0]);
      float vFloat = scaleVoltage(setpointRegs[2]);
      float vEq    = scaleVoltage(setpointRegs[3]);
      // Sanity: setpoints must look like plausible battery voltages.
      // Accept anything in 8..32V (covers 12V and 24V chargers, allows L16/equalize).
      if (vReg >= 8.0f && vReg <= 32.0f && vFloat >= 8.0f && vFloat <= 32.0f) {
        nextData.vRegSetpoint   = vReg;
        nextData.vFloatSetpoint = vFloat;
        nextData.vEqSetpoint    = (vEq >= 8.0f && vEq <= 32.0f) ? vEq : 0.0f;
        nextData.setpointsValid = true;
        Serial.print(F("Solar: setpoints read V_reg="));
        Serial.print(vReg, 2);
        Serial.print(F(" V_float="));
        Serial.print(vFloat, 2);
        Serial.print(F(" V_eq="));
        Serial.println(nextData.vEqSetpoint, 2);
      } else {
        Serial.print(F("Solar: setpoint read returned implausible values, skipping verify (V_reg="));
        Serial.print(vReg, 2);
        Serial.println(F(")"));
      }
    }
  }

  // Suspect register blocks: only read when explicitly enabled for bench work.
  // In production these stay zeroed so derived health logic does not false-trip.
#ifdef SOLAR_ENABLE_UNVERIFIED_REGISTERS
  uint16_t temperatureRegs[2];
  if (success && readRegistersWithFallback(_config.modbusSlaveId, SS_REG_HEATSINK_TEMP, 2, temperatureRegs)) {
    nextData.heatsinkTemp = (int8_t)((int16_t)temperatureRegs[0]);
    nextData.batteryTemp = (int8_t)((int16_t)temperatureRegs[1]);
  }

  uint16_t statusRegs[5];
  if (success && readRegistersWithFallback(_config.modbusSlaveId, SS_REG_CHARGE_STATE, 5, statusRegs)) {
    nextData.chargeState = (SolarChargeState)(statusRegs[0] & 0xFF);
    nextData.faults = statusRegs[1];
    nextData.alarms = statusRegs[3];
    nextData.loadOn = (statusRegs[4] != 0);
  }

  uint16_t dailyChargeRegs[1];
  if (success && readRegistersWithFallback(_config.modbusSlaveId, SS_REG_AH_DAILY, 1, dailyChargeRegs)) {
    nextData.ampHoursDaily = dailyChargeRegs[0] * 0.1f;  // Scale: 0.1 Ah per count
  }

  uint16_t dailyVoltageRegs[2];
  if (success && readRegistersWithFallback(_config.modbusSlaveId, SS_REG_BATTERY_V_MIN_DAILY, 2, dailyVoltageRegs)) {
    nextData.batteryVoltageMinDaily = scaleVoltage(dailyVoltageRegs[0]);
    nextData.batteryVoltageMaxDaily = scaleVoltage(dailyVoltageRegs[1]);
  }
#else
  // Force unverified fields to safe defaults every poll so a stale value from
  // a prior bench session can never leak into health logic or daily reports.
  nextData.heatsinkTemp = 0;
  nextData.batteryTemp = 0;
  nextData.chargeState = CHARGE_STATE_START;
  nextData.faults = 0;
  nextData.alarms = 0;
  nextData.loadOn = false;
  nextData.ampHoursDaily = 0.0f;
  nextData.wattHoursDaily = 0.0f;
  nextData.batteryVoltageMinDaily = nextData.batteryVoltage;
  nextData.batteryVoltageMaxDaily = nextData.batteryVoltage;
#endif
  
  // Update communication status
  if (success) {
    _data = nextData;
    _data.communicationOk = true;
    _data.lastReadMillis = millis();
    _data.consecutiveErrors = 0;
  } else {
    _data.consecutiveErrors++;
    if (_data.consecutiveErrors >= SOLAR_COMM_FAILURE_THRESHOLD) {
      _data.communicationOk = false;
      Serial.print(F("Solar: Modbus communication failure ("));
      Serial.print(_data.consecutiveErrors);
      Serial.println(F(" consecutive errors)"));
    }
  }
  
  // Update derived status
  updateHealthStatus();
  
  return success;
}

float SolarManager::scaleVoltage(uint16_t raw) const {
  // Formula: Voltage = (Raw * 100) / 32768 for 12V system
  return (raw * SS_SCALE_VOLTAGE_12V) / SS_SCALE_DIVISOR;
}

float SolarManager::scaleCurrent(uint16_t raw) const {
  // Formula: Current = (Raw * 79.16) / 32768 for 12V system
  return (raw * SS_SCALE_CURRENT_12V) / SS_SCALE_DIVISOR;
}

void SolarManager::updateHealthStatus() {
  // Update derived flags
  _data.hasFault = (_data.faults != 0);
  _data.hasAlarm = (_data.alarms != 0);
  
  // Charge state indicators
  _data.isCharging = (_data.chargeState == CHARGE_STATE_BULK || 
                      _data.chargeState == CHARGE_STATE_ABSORPTION ||
                      _data.chargeState == CHARGE_STATE_EQUALIZE);
  _data.isFullyCharged = (_data.chargeState == CHARGE_STATE_FLOAT);
  
  // Battery health assessment
  _data.batteryHealthy = (_data.batteryVoltage >= _config.batteryLowVoltage) &&
                         (_data.batteryVoltage <= _config.batteryHighVoltage) &&
                         !_data.hasFault;
  
  // Overall solar system health
  _data.solarHealthy = _data.batteryHealthy && 
                       _data.communicationOk && 
                       !_data.hasFault && 
                       !_data.hasAlarm;
}

SolarAlertType SolarManager::checkAlerts() const {
  if (!_config.enabled) {
    return SOLAR_ALERT_NONE;
  }
  
  // Priority order: most critical first
  
  // Critical battery voltage
  if (_data.batteryVoltage < _config.batteryCriticalVoltage && _data.batteryVoltage > 0) {
    return SOLAR_ALERT_BATTERY_CRITICAL;
  }
  
  // Hardware faults
  if (_data.hasFault && _config.alertOnFault) {
    return SOLAR_ALERT_FAULT;
  }
  
  // Communication failure
  if (!_data.communicationOk && _config.alertOnCommFailure) {
    return SOLAR_ALERT_COMM_FAILURE;
  }
  
  // Low battery voltage (warning level)
  if (_data.batteryVoltage < _config.batteryLowVoltage && _data.batteryVoltage > 0) {
    return SOLAR_ALERT_BATTERY_LOW;
  }
  
  // High battery voltage (overcharge)
  if (_data.batteryVoltage > _config.batteryHighVoltage) {
    return SOLAR_ALERT_BATTERY_HIGH;
  }
  
  // Heatsink temperature warning
  if (_data.heatsinkTemp > 60) {  // 60°C is typical warning threshold
    return SOLAR_ALERT_HEATSINK_TEMP;
  }
  
  // Alarm conditions
  if (_data.hasAlarm && _config.alertOnFault) {
    return SOLAR_ALERT_ALARM;
  }
  
  return SOLAR_ALERT_NONE;
}

const char* SolarManager::getAlertDescription(SolarAlertType alert) const {
  switch (alert) {
    case SOLAR_ALERT_NONE:
      return "OK";
    case SOLAR_ALERT_BATTERY_LOW:
      return "Battery voltage low";
    case SOLAR_ALERT_BATTERY_CRITICAL:
      return "Battery voltage CRITICAL";
    case SOLAR_ALERT_BATTERY_HIGH:
      return "Battery overvoltage";
    case SOLAR_ALERT_FAULT:
      return getFaultDescription();
    case SOLAR_ALERT_ALARM:
      return getAlarmDescription();
    case SOLAR_ALERT_COMM_FAILURE:
      return "Solar charger communication failure";
    case SOLAR_ALERT_HEATSINK_TEMP:
      return "Solar charger overheating";
    case SOLAR_ALERT_NO_CHARGE:
      return "No solar charging detected";
    default:
      return "Unknown alert";
  }
}

const char* SolarManager::getChargeStateDescription() const {
  switch (_data.chargeState) {
    case CHARGE_STATE_START:
      return "Starting";
    case CHARGE_STATE_NIGHT_CHECK:
      return "Night Check";
    case CHARGE_STATE_DISCONNECT:
      return "Disconnected";
    case CHARGE_STATE_NIGHT:
      return "Night";
    case CHARGE_STATE_FAULT:
      return "FAULT";
    case CHARGE_STATE_BULK:
      return "Bulk";
    case CHARGE_STATE_ABSORPTION:
      return "Absorption";
    case CHARGE_STATE_FLOAT:
      return "Float";
    case CHARGE_STATE_EQUALIZE:
      return "Equalize";
    default:
      return "Unknown";
  }
}

const char* SolarManager::getFaultDescription() const {
  if (_data.faults == 0) {
    return "No faults";
  }
  
  _faultDescBuffer[0] = '\0';
  
  if (_data.faults & SS_FAULT_OVERCURRENT)    strlcat(_faultDescBuffer, "Overcurrent ", sizeof(_faultDescBuffer));
  if (_data.faults & SS_FAULT_FET_SHORT)      strlcat(_faultDescBuffer, "FET-Short ", sizeof(_faultDescBuffer));
  if (_data.faults & SS_FAULT_SOFTWARE)       strlcat(_faultDescBuffer, "SW-Fault ", sizeof(_faultDescBuffer));
  if (_data.faults & SS_FAULT_BATT_HVD)       strlcat(_faultDescBuffer, "Batt-HVD ", sizeof(_faultDescBuffer));
  if (_data.faults & SS_FAULT_ARRAY_HVD)      strlcat(_faultDescBuffer, "Array-HVD ", sizeof(_faultDescBuffer));
  if (_data.faults & SS_FAULT_DIP_SW_FAULT)   strlcat(_faultDescBuffer, "DIP-SW ", sizeof(_faultDescBuffer));
  if (_data.faults & SS_FAULT_RESET_FAULT)    strlcat(_faultDescBuffer, "Reset ", sizeof(_faultDescBuffer));
  if (_data.faults & SS_FAULT_RTS_DISCONN)    strlcat(_faultDescBuffer, "RTS-Disc ", sizeof(_faultDescBuffer));
  if (_data.faults & SS_FAULT_RTS_SHORT)      strlcat(_faultDescBuffer, "RTS-Short ", sizeof(_faultDescBuffer));
  if (_data.faults & SS_FAULT_HEATSINK_LIMIT) strlcat(_faultDescBuffer, "Heatsink-Limit ", sizeof(_faultDescBuffer));
  
  // Trim trailing space
  size_t len = strlen(_faultDescBuffer);
  if (len > 0 && _faultDescBuffer[len - 1] == ' ') {
    _faultDescBuffer[len - 1] = '\0';
  }
  
  return _faultDescBuffer;
}

const char* SolarManager::getAlarmDescription() const {
  if (_data.alarms == 0) {
    return "No alarms";
  }
  
  _alarmDescBuffer[0] = '\0';
  
  if (_data.alarms & SS_ALARM_RTS_OPEN)        strlcat(_alarmDescBuffer, "RTS-Open ", sizeof(_alarmDescBuffer));
  if (_data.alarms & SS_ALARM_RTS_SHORT)       strlcat(_alarmDescBuffer, "RTS-Short ", sizeof(_alarmDescBuffer));
  if (_data.alarms & SS_ALARM_RTS_DISCONN)     strlcat(_alarmDescBuffer, "RTS-Disc ", sizeof(_alarmDescBuffer));
  if (_data.alarms & SS_ALARM_HEATSINK_LIMIT)  strlcat(_alarmDescBuffer, "Heatsink ", sizeof(_alarmDescBuffer));
  if (_data.alarms & SS_ALARM_CURRENT_LIMIT)   strlcat(_alarmDescBuffer, "I-Limit ", sizeof(_alarmDescBuffer));
  if (_data.alarms & SS_ALARM_CURRENT_OFFSET)  strlcat(_alarmDescBuffer, "I-Offset ", sizeof(_alarmDescBuffer));
  if (_data.alarms & SS_ALARM_BATT_SENSE)      strlcat(_alarmDescBuffer, "Batt-Sense ", sizeof(_alarmDescBuffer));
  if (_data.alarms & SS_ALARM_BATT_SENSE_DISC) strlcat(_alarmDescBuffer, "Sense-Disc ", sizeof(_alarmDescBuffer));
  if (_data.alarms & SS_ALARM_UNCALIBRATED)    strlcat(_alarmDescBuffer, "Uncal ", sizeof(_alarmDescBuffer));
  if (_data.alarms & SS_ALARM_RTS_MISWIRE)     strlcat(_alarmDescBuffer, "RTS-Miswire ", sizeof(_alarmDescBuffer));
  if (_data.alarms & SS_ALARM_HVD)             strlcat(_alarmDescBuffer, "HVD ", sizeof(_alarmDescBuffer));
  if (_data.alarms & SS_ALARM_LOG_TIMEOUT)     strlcat(_alarmDescBuffer, "Log-Timeout ", sizeof(_alarmDescBuffer));
  if (_data.alarms & SS_ALARM_EEPROM)          strlcat(_alarmDescBuffer, "EEPROM ", sizeof(_alarmDescBuffer));
  
  // Trim trailing space
  size_t len = strlen(_alarmDescBuffer);
  if (len > 0 && _alarmDescBuffer[len - 1] == ' ') {
    _alarmDescBuffer[len - 1] = '\0';
  }
  
  return _alarmDescBuffer;
}

void SolarManager::resetDailyStats() {
  // Note: The SunSaver resets its own daily stats at midnight (based on its RTC)
  // This just resets our local tracking
  _data.batteryVoltageMinDaily = _data.batteryVoltage;
  _data.batteryVoltageMaxDaily = _data.batteryVoltage;
  _data.ampHoursDaily = 0.0f;
  _data.wattHoursDaily = 0.0f;
}

SolarManager::ChemistryCheck SolarManager::verifyChemistry(uint8_t expectedType,
                                                            uint8_t nominalVoltage,
                                                            char* outDescription,
                                                            size_t descriptionLen) const {
  if (!_data.setpointsValid) {
    if (outDescription && descriptionLen) {
      strncpy(outDescription, "setpoints not yet read from controller", descriptionLen - 1);
      outDescription[descriptionLen - 1] = '\0';
    }
    return CHEMISTRY_CHECK_PENDING;
  }

  if (nominalVoltage == 0) nominalVoltage = 12;
  const float scale = (float)nominalVoltage / 12.0f;
  const float vReg   = _data.vRegSetpoint;
  const float vFloat = _data.vFloatSetpoint;
  const float vEq    = _data.vEqSetpoint;

  // Pack-voltage sanity: V_float should be within ~30% of nominal*1.13 (e.g. 13.6V on 12V).
  // If it's roughly half or double, the user picked the wrong pack voltage.
  const float expectedFloat = 13.6f * scale;
  if (vFloat < expectedFloat * 0.6f || vFloat > expectedFloat * 1.4f) {
    if (outDescription && descriptionLen) {
      snprintf(outDescription, descriptionLen,
               "V_float=%.1fV does not match %dV pack (expected ~%.1fV)",
               vFloat, (int)nominalVoltage, expectedFloat);
    }
    return CHEMISTRY_CHECK_VOLTAGE_MISMATCH;
  }

  // Chemistry-specific checks. Only flag clear mismatches.
  // Reference (12V baseline; 24V doubles):
  //   Sealed/AGM/Gel: V_reg ~14.15V, V_float ~13.4V, V_eq = 0
  //   Flooded:        V_reg ~14.4V,  V_float ~13.4V, V_eq ~15.1V (non-zero)
  //   LiFePO4 (custom): V_reg ~14.4V, V_float ~13.6V, V_eq = 0
  bool eqExpected = false;
  bool eqAllowed  = true;
  switch ((BatteryType)expectedType) {
    case BATTERY_TYPE_FLOODED:
      eqExpected = true;
      break;
    case BATTERY_TYPE_AGM:
    case BATTERY_TYPE_SLA:
    case BATTERY_TYPE_GEL:
    case BATTERY_TYPE_LIFEPO4:
    case BATTERY_TYPE_LI_ION:
      eqAllowed = false;
      break;
    case BATTERY_TYPE_CUSTOM:
    case BATTERY_TYPE_NONE:
    case BATTERY_TYPE_LIPO:
    default:
      // Cannot meaningfully verify — accept anything.
      if (outDescription && descriptionLen) outDescription[0] = '\0';
      return CHEMISTRY_CHECK_OK;
  }

  const bool eqActive = (vEq > 1.0f);  // anything > 1V counts as enabled
  if (eqExpected && !eqActive) {
    if (outDescription && descriptionLen) {
      snprintf(outDescription, descriptionLen,
               "Flooded selected but V_eq=%.1fV (expected non-zero); check DIP switches",
               vEq);
    }
    return CHEMISTRY_CHECK_MISMATCH;
  }
  if (!eqAllowed && eqActive) {
    if (outDescription && descriptionLen) {
      snprintf(outDescription, descriptionLen,
               "%s selected but V_eq=%.1fV is enabled; check DIP switches (Sealed/Gel)",
               batteryTypeLabel((BatteryType)expectedType), vEq);
    }
    return CHEMISTRY_CHECK_MISMATCH;
  }

  if (outDescription && descriptionLen) {
    snprintf(outDescription, descriptionLen,
             "OK: V_reg=%.2f V_float=%.2f V_eq=%.2f matches %s/%dV",
             vReg, vFloat, vEq,
             batteryTypeLabel((BatteryType)expectedType), (int)nominalVoltage);
  }
  return CHEMISTRY_CHECK_OK;
}

#else // Platform not supported

// Stub implementation for non-RS485 platforms
SolarManager::SolarManager() : _initialized(false), _lastPollMillis(0) {
  memset(&_data, 0, sizeof(SolarData));
  memset(&_config, 0, sizeof(SolarConfig));
}

bool SolarManager::begin(const SolarConfig& config) {
  (void)config;
  Serial.println(F("Solar: RS485 not available on this platform"));
  return false;
}

void SolarManager::end() {}
void SolarManager::setConfig(const SolarConfig& config) { _config = config; }
bool SolarManager::poll(unsigned long nowMillis) { (void)nowMillis; return false; }
SolarAlertType SolarManager::checkAlerts() const { return SOLAR_ALERT_NONE; }
const char* SolarManager::getAlertDescription(SolarAlertType alert) const { (void)alert; return "N/A"; }
const char* SolarManager::getChargeStateDescription() const { return "N/A"; }
const char* SolarManager::getFaultDescription() const { return "N/A"; }
const char* SolarManager::getAlarmDescription() const { return "N/A"; }
void SolarManager::resetDailyStats() {}
bool SolarManager::readRegisters() { return false; }
float SolarManager::scaleVoltage(uint16_t raw) const { (void)raw; return 0.0f; }
float SolarManager::scaleCurrent(uint16_t raw) const { (void)raw; return 0.0f; }
void SolarManager::updateHealthStatus() {}
SolarManager::ChemistryCheck SolarManager::verifyChemistry(uint8_t expectedType,
                                                            uint8_t nominalVoltage,
                                                            char* outDescription,
                                                            size_t descriptionLen) const {
  (void)expectedType; (void)nominalVoltage;
  if (outDescription && descriptionLen) outDescription[0] = '\0';
  return CHEMISTRY_CHECK_PENDING;
}

#endif // Platform check
