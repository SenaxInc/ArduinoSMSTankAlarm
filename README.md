# Arduino SMS Tank Alarm

***In Process of Switching to LTE***

Arduino GSM code that will send SMS alert of high tank sensor data.

Arduino will check sensor data on a schedualed basis. If sensor data is over a set number the arduino will send an SMS text message to a defined phone number. It will also send a daily reading of the sensor data at a set time of day.

# General Logic

	
	Read sensor
		
		If over X
		
			Connect to Network
			
				Text #1 - Data Logger
			
				Text #2 - Emergency contact
				
				Text #3 - Emergency contact
				
			Disconnect
			
			Sleep 1Hr
			
		If under X
		
			If time = 5-6AM
			
				Connect to Network
				
					Text #1 - Data Logger
				
				Disconnect
			
				Sleep 1Hr
				
			If time =/= 5-6AM
			
				Sleep 1Hr

# Bill of Materials

<a href="https://github.com/dorkmo/ArduinoSMSTankAlarm/blob/master/BillOfMaterials.md">Bill Of Materials</a>

# Links - Arduino

http://www.arduino.org/learning/getting-started/getting-started-with-arduino-gsm-shield-2

http://www.arduino.org/learning/tutorials/ide-examples

https://github.com/arduino-org/Arduino/tree/master/hardware/arduino/avr/libraries/GSM

http://www.arduino.org/learning/tutorials/advanced-guides/how-to-use-the-arduino-gsm-shield-with-arduino-leonardo-arduino-yun-and-arduino-mega

http://www.arduino.org/learning/reference/GSM

http://www.arduino.org/learning/tutorials/boards-tutorials/play-with-force-sensor-example

http://www.arduino.org/learning/tutorials/boards-tutorials/gsm-shield-2-voice-call-example

http://www.arduino.org/learning/reference/gsm-constructor

http://www.arduino.org/learning/reference/std-standby

http://www.arduino.org/learning/reference/begin-mode

http://playground.arduino.cc/Learning/arduinoSleepCode

https://www.arduino.cc/en/Tutorial/Files

https://www.arduino.cc/en/Hacking/BuildProcess

https://www.arduino.cc/en/Guide/Environment

https://create.arduino.cc/projecthub/feilipu/battery-powered-arduino-applications-through-freertos-3b7401

https://www.arduino.cc/en/Reference/GSMSMSRead

https://www.arduino.cc/en/Reference/Char

https://www.arduino.cc/en/Reference/Array

https://www.arduino.cc/en/Tutorial/GSMExamplesReceiveSMS

https://forum.arduino.cc/index.php?topic=163636.0

# Links - Digi Xbee


Sleep

https://www.digi.com/resources/documentation/Digidocs/90002258/#Reference/r_cmd_D8.htm

SMS

https://www.digi.com/resources/documentation/Digidocs/90002258/Reference/r_cmd_IP.htm

https://www.u-blox.com/sites/default/files/SARA-R4-SARA-N4_ATCommands_(UBX-17003787).pdf - Page 67


