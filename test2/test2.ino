#include <SoftwareSerial.h>
//Click here to get the library: http://librarymanager/All#SparkFun_LTE_Shield_Arduino_Library
#include <SparkFun_LTE_Shield_Arduino_Library.h>
//#include <ArduinoJson.h>

// Create a SoftwareSerial object to pass to the LTE_Shield library
SoftwareSerial lteSerial(8, 9);

// Create a LTE_Shield object to use throughout the sketch
LTE_Shield lte;


// Hologram device key. Used to send messages:
String HOLOGRAM_DEVICE_KEY = "Ab12CdE4";

// Hologram Server constants. Shouldn't have to change:
const char HOLOGRAM_URL[] = "cloudsocket.hologram.io";
const unsigned int HOLOGRAM_PORT = 9999;
const unsigned int HOLOGRAM_LISTEN_PORT = 4010;

void setup() {
  // put your setup code here, to run once:
  
            digitalWrite(9, HIGH);  //pin nine powers on XBee
            pinMode(9, OUTPUT);
            delay(500); //wait for power signal to work   
            digitalWrite(9, LOW); // turn off power signal LOW = on, HIGH = sleep

  XBeeSerial.begin(9600);
delay(2000);
}

void loop() {
  // put your main code here, to run repeatedly:
  if (XBeeSerial.isListening())
  {
XBeeSerial.println('{"k":"(t87tF,x","d":"Hello, World!","t":"TOPIC1"}');
delay(60000);
  }
}
