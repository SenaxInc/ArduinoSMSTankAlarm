filepath = r'c:\Users\Mike\Documents\GitHub\ArduinoSMSTankAlarm\TankAlarm-112025-Client-BluesOpta\TankAlarm-112025-Client-BluesOpta.ino'
content = open(filepath, 'r', encoding='utf-8').read()

# Replace ALL patterns including both gConfig and cfg prefixes
replacements = [
    ('.tankCount', '.monitorCount'),
    ('.tanks[', '.monitors['),
    ('gTankState[', 'gMonitorState['),
    ('cfg.sensorType', 'cfg.sensorInterface'),
    ('cfg.tankNumber', 'cfg.monitorNumber'),
    ('cfg.rpmPin', 'cfg.pulsePin'),
    ('cfg.pulsesPerRevolution', 'cfg.pulsesPerUnit'),
    ('cfg.rpmSampleDurationMs', 'cfg.pulseSampleDurationMs'),
    ('cfg.rpmAccumulatedMode', 'cfg.pulseAccumulatedMode'),
    ('SENSOR_HALL_EFFECT_RPM', 'SENSOR_PULSE'),
    ('TankConfig', 'MonitorConfig'),
    ('TankRuntime', 'MonitorRuntime'),
    ('getTankHeight', 'getMonitorHeight'),
]

for old, new in replacements:
    content = content.replace(old, new)

open(filepath, 'w', encoding='utf-8').write(content)
print('Done')
