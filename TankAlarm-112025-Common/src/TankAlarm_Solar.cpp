/**
 * TankAlarm_Solar.cpp
 * 
 * SunSaver MPPT Solar Charger Monitoring Implementation
 * Uses ArduinoModbus library for Modbus RTU communication over RS-485
 * 
 * Copyright (c) 2025-2026 Senax Inc. All rights reserved.
 */

#include "TankAlarm_Solar.h"

#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)

#include <ArduinoRS485.h>
#include <ArduinoModbus.h>

// Static message buffers for descriptions (avoid dynamic allocation)
static char _faultDescBuffer[128];
static char _alarmDescBuffer[128];

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
  _config.alertOnFault = true;
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
  // Arduino Opta RS485 uses the built-in RS485 interface
  if (!ModbusRTUClient.begin(_config.modbusBaudRate)) {
    Serial.println(F("Solar: Failed to initialize Modbus RTU Client"));
    _initialized = false;
    return false;
  }
  
  // Set read timeout
  ModbusRTUClient.setTimeout(_config.modbusTimeoutMs);
  
  Serial.print(F("Solar: Modbus RTU initialized at "));
  Serial.print(_config.modbusBaudRate);
  Serial.print(F(" baud, slave ID "));
  Serial.println(_config.modbusSlaveId);
  
  _initialized = true;
  _data.communicationOk = false;
  _data.consecutiveErrors = 0;
  
  // Do an initial read
  readRegisters();
  
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
    return false;
  }
  
  // Check if it's time to poll
  unsigned long intervalMs = (unsigned long)_config.pollIntervalSec * 1000UL;
  if (nowMillis - _lastPollMillis < intervalMs) {
    return false;
  }
  
  _lastPollMillis = nowMillis;
  return readRegisters();
}

bool SolarManager::readRegisters() {
  bool success = true;
  
  // Read battery voltage (register 19, address 0x0012)
  if (ModbusRTUClient.requestFrom(_config.modbusSlaveId, HOLDING_REGISTERS, SS_REG_BATTERY_VOLTAGE, 1)) {
    uint16_t raw = ModbusRTUClient.read();
    _data.batteryVoltage = scaleVoltage(raw);
  } else {
    success = false;
  }
  
  // Read array voltage (register 20, address 0x0013)
  if (success && ModbusRTUClient.requestFrom(_config.modbusSlaveId, HOLDING_REGISTERS, SS_REG_ARRAY_VOLTAGE, 1)) {
    uint16_t raw = ModbusRTUClient.read();
    _data.arrayVoltage = scaleVoltage(raw);
  } else {
    success = false;
  }
  
  // Read charge current (register 17, address 0x0010)
  if (success && ModbusRTUClient.requestFrom(_config.modbusSlaveId, HOLDING_REGISTERS, SS_REG_CHARGE_CURRENT, 1)) {
    uint16_t raw = ModbusRTUClient.read();
    _data.chargeCurrent = scaleCurrent(raw);
  } else {
    success = false;
  }
  
  // Read load current (register 18, address 0x0011)
  if (success && ModbusRTUClient.requestFrom(_config.modbusSlaveId, HOLDING_REGISTERS, SS_REG_LOAD_CURRENT, 1)) {
    uint16_t raw = ModbusRTUClient.read();
    _data.loadCurrent = scaleCurrent(raw);
  } else {
    success = false;
  }
  
  // Read heatsink temperature (register 28, address 0x001B)
  if (success && ModbusRTUClient.requestFrom(_config.modbusSlaveId, HOLDING_REGISTERS, SS_REG_HEATSINK_TEMP, 1)) {
    int16_t raw = (int16_t)ModbusRTUClient.read();
    _data.heatsinkTemp = (int8_t)raw;  // Direct signed value in °C
  } else {
    success = false;
  }
  
  // Read charge state (register 44, address 0x002B)
  if (success && ModbusRTUClient.requestFrom(_config.modbusSlaveId, HOLDING_REGISTERS, SS_REG_CHARGE_STATE, 1)) {
    uint16_t raw = ModbusRTUClient.read();
    _data.chargeState = (SolarChargeState)(raw & 0xFF);
  } else {
    success = false;
  }
  
  // Read faults (register 45, address 0x002C)
  if (success && ModbusRTUClient.requestFrom(_config.modbusSlaveId, HOLDING_REGISTERS, SS_REG_FAULTS, 1)) {
    _data.faults = ModbusRTUClient.read();
  } else {
    success = false;
  }
  
  // Read alarms (register 47, address 0x002E)
  if (success && ModbusRTUClient.requestFrom(_config.modbusSlaveId, HOLDING_REGISTERS, SS_REG_ALARMS, 1)) {
    _data.alarms = ModbusRTUClient.read();
  } else {
    success = false;
  }
  
  // Read daily min battery voltage (register 62, address 0x003D)
  if (success && ModbusRTUClient.requestFrom(_config.modbusSlaveId, HOLDING_REGISTERS, SS_REG_BATTERY_V_MIN_DAILY, 1)) {
    uint16_t raw = ModbusRTUClient.read();
    _data.batteryVoltageMinDaily = scaleVoltage(raw);
  } else {
    success = false;
  }
  
  // Read daily max battery voltage (register 63, address 0x003E)
  if (success && ModbusRTUClient.requestFrom(_config.modbusSlaveId, HOLDING_REGISTERS, SS_REG_BATTERY_V_MAX_DAILY, 1)) {
    uint16_t raw = ModbusRTUClient.read();
    _data.batteryVoltageMaxDaily = scaleVoltage(raw);
  } else {
    success = false;
  }
  
  // Read amp-hours today (register 53, address 0x0034)
  if (success && ModbusRTUClient.requestFrom(_config.modbusSlaveId, HOLDING_REGISTERS, SS_REG_AH_DAILY, 1)) {
    uint16_t raw = ModbusRTUClient.read();
    _data.ampHoursDaily = raw * 0.1f;  // Scale: 0.1 Ah per count
  } else {
    success = false;
  }
  
  // Update communication status
  if (success) {
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
  
  // Load state
  _data.loadOn = (_data.chargeState != CHARGE_STATE_FAULT);  // Simplified; could read load state register
  
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

#endif // Platform check
