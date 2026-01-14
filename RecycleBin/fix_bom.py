import os

file_path = r"c:\Users\dorkm\Documents\GitHub\ArduinoSMSTankAlarm\TankAlarm-112025-Server-BluesOpta\TankAlarm-112025-Server-BluesOpta.ino"

try:
    with open(file_path, 'rb') as f:
        content = f.read()

    bom = b'\xef\xbb\xbf'
    if content.startswith(bom):
        print("BOM found. Removing...")
        new_content = content[3:]
        with open(file_path, 'wb') as f:
            f.write(new_content)
        print("BOM removed successfully.")
    else:
        print("No BOM found. The file starts with: " + str(content[:10]))

except Exception as e:
    print(f"Error: {e}")
