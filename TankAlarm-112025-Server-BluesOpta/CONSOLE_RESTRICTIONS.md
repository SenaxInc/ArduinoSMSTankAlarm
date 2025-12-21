# Console Restrictions & Hardware Configuration

The **Client Console** web interface allows remote configuration of alarm thresholds, reporting schedules, and site metadata. However, settings that define the **physical hardware interface** (wiring, sensors, pins) are **locked** in the console to prevent accidental misconfiguration that could disable monitoring or require a site visit to fix.

These settings must be defined in the initial configuration file uploaded to the device (e.g., via USB or SD card) or compiled into the firmware.

## Locked Settings (Hardware Dependent)

The following settings are visible in the console but cannot be edited:

### 1. Clear Button Configuration
*   **Clear Button Pin**: The physical GPIO pin connected to the external push-button.
*   **Button Active State**: The electrical logic level (Active HIGH vs Active LOW) determined by the wiring (pull-up vs pull-down resistors).

### 2. Tank Sensor Configuration
*   **Sensor Type**: The class of sensor hardware installed (e.g., `Analog` 4-20mA, `Digital` Float Switch, `Current Loop`). Changing this requires different physical wiring and often different driver circuitry.
*   **Primary Pin**: The main GPIO or Analog pin where the sensor signal is read.
*   **Secondary Pin**: Used for differential readings or complex sensors.
*   **Loop Channel**: For multiplexed current loop sensors (e.g., using an external ADC or Mux).

## How to Change Hardware Settings

To modify these settings, you must update the device configuration file directly.

1.  **Create/Edit Config File**: Generate a `client_config.json` file with the correct hardware parameters.
2.  **Upload**:
    *   **Option A (SD Card)**: Save the file to an SD card and insert it into the device. Reboot to load.
    *   **Option B (USB)**: Connect via USB Serial and use the CLI command `config set <json_payload>`.
    *   **Option C (Firmware)**: If using a fixed fleet deployment, update the default configuration in `TankAlarm-112025-Client-BluesOpta.ino` and re-flash the device.

## Why are these locked?

*   **Safety**: Changing a pin assignment remotely to a pin that is driving a relay or other actuator could cause hardware damage.
*   **Reliability**: Incorrect sensor types will result in false alarms or zero readings.
*   **Stability**: These settings rarely change after installation. Locking them prevents "fat-finger" errors during routine threshold adjustments.
