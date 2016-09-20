// import the GSM library
#include <GSM.h>

// PIN Number
#define PINNUMBER ""

// initialize the library instance
GSM gsmAccess;
GSM_SMS sms;

// char array of the telephone number to send SMS
char remoteNumber[20]= "1918XXXXXXX";

// char array of the message
char txtMsg[200]="High Tank Alarm - Testing 1 2 3";

// connection state   -- JWW doesnt know what this does
boolean notConnected = true;

// this is the threshold used for reading -- JWW sensor trigger
const int threshold = 10;

void setup() {
}

void loop() {
  for (int sensorNumber = 0; sensorNumber < 3; sensorNumber++) {
    // read a sensor
    int readvalue = analogRead(sensorNumber);
    // if the sensor is pressed hard enough:
    if (readvalue > threshold) {
      // send SMS
      // Start GSM SHIELD
      // If your SIM has PIN, pass it as a parameter of begin() in quotes
      while(notConnected) {
        if(gsmAccess.begin(PINNUMBER)==GSM_READY)
          notConnected = false;
        else
        {
          delay(1000);
        }
      }
    }
  sms.beginSMS(remoteNumber);
  sms.print(txtMsg);
  sms.endSMS();
  gsmAccess.shutdown();
  // would like to include something here to make arduino sleep for an hour
 }
}
