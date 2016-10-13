// import the GSM and rtos library
#include <GSM.h>
#include <avr/sleep.h>

// PIN Number
#define PINNUMBER ""

// initialize the library instance
GSM gsmAccess;
GSM_SMS sms;

volatile int time_tick = 0;

const int sleep_hours = 1;
const int ticks_per_sleep = (sleep_hours*60*60)/8;

// char array of the telephone number to send SMS
char remoteNumber[20]= "1918XXXXXXX";

// char array of the message
char txtMsg[200]="High Tank Alarm - Testing 1 2 3";

// connection state   -- JWW doesnt know what this does
boolean notConnected = true;

// this is the threshold used for reading -- JWW sensor trigger
const int threshold = 500;

void setup() {
  
  int readvalue = analogRead(A0);
  sms.beginSMS(remoteNumber);
  sms.print("Power On",readvalue);
  sms.endSMS();
  gsmAccess.shutdown();
       //sleep
  watchdogSTART();
}

void loop() {
       // read a sensor
       int readvalue = analogRead(A0);
       // if the sensor is over height 
       if (readvalue > threshold) {{
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
  sms.print(readvalue);
  sms.endSMS();
  gsmAccess.shutdown();
  // would like to include something here to make arduino sleep for an hour
            delay(3600000);

 }
}


void watchdogSTART() {
  MCUSR = MCUSR & B11110111;
  WDTCSR = WDTCSR | B00011000; 
  WDTCSR = B00100001;
  WDTCSR = WDTCSR | B01000000;
  MCUSR = MCUSR & B11110111;
}
