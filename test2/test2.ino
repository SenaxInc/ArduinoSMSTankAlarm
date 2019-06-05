#include <SoftwareSerial.h>
//Click here to get the library: http://librarymanager/All#SparkFun_LTE_Shield_Arduino_Library
#include <SparkFun_LTE_Shield_Arduino_Library.h>
//#include <ArduinoJson.h>

// Create a SoftwareSerial object to pass to the LTE_Shield library
SoftwareSerial lteSerial(8, 9);

// Create a LTE_Shield object to use throughout the sketch
LTE_Shield lte;


// Hologram device key. Used to send messages:
String HOLOGRAM_DEVICE_KEY = "12345678";

// Hologram Server constants. Shouldn't have to change:
const char HOLOGRAM_URL[] = "cloudsocket.hologram.io";
const unsigned int HOLOGRAM_PORT = 9999;
const unsigned int HOLOGRAM_LISTEN_PORT = 4010;

void setup() {
}

void loop() {
  Serial.begin(9600);
  if ( lte.begin(lteSerial, 9600) ) {
    Serial.println(F("LTE Shield connected!"));
  }

String message = "High Tank";


//"{"k":"XxXxXxXx","d":"Hello, World!","t":"TOPIC1"}";

sendHologramMessage(message);

message = ""; // Clear message string


lte.poll();

} //end loop

void sendHologramMessage(String message)
{
  int socket = -1;
  String hologramMessage;
  String topic;

  topic = "ALERT1";

  // New lines are not handled well
  message.replace('\r', ' ');
  message.replace('\n', ' ');

  topic.replace('\r', ' ');
  topic.replace('\n', ' ');



  // Construct a JSON-encoded Hologram message string:
//  hologramMessage = "{\"k\":\"" + HOLOGRAM_DEVICE_KEY + "\",\"d\":\"" + message + "\"}";  
    hologramMessage = "{\"k\":\"" + HOLOGRAM_DEVICE_KEY + "\",\"d\":\"" + message + "\",\"t\":\"" + topic + "\"}";  

  
  // Open a socket
  socket = lte.socketOpen(LTE_SHIELD_TCP);
  // On success, socketOpen will return a value between 0-5. On fail -1.
  if (socket >= 0) {
    // Use the socket to connec to the Hologram server
    Serial.println("Connecting to socket: " + String(socket));
    if (lte.socketConnect(socket, HOLOGRAM_URL, HOLOGRAM_PORT) == LTE_SHIELD_SUCCESS) {
      // Send our message to the server:
      Serial.println("Sending: " + String(hologramMessage));
      if (lte.socketWrite(socket, hologramMessage) == LTE_SHIELD_SUCCESS)
      {
        // On succesful write, close the socket.
        if (lte.socketClose(socket) == LTE_SHIELD_SUCCESS) {
          Serial.println("Socket " + String(socket) + " closed");
        }
      } else {
        Serial.println(F("Failed to write"));

        //add tick to try 10 times
        
      }
    }
  }
} //end sendHologramMessage
