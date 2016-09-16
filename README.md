# Arduino SMS Tank Alarm
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
