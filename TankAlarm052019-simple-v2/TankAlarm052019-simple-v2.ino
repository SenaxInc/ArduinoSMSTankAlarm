#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <SoftwareSerial.h>   //Use SoftwareSerial to communicate with LTEshield

//Click here to get the library: http://librarymanager/All#SparkFun_LTE_Shield_Arduino_Library
#include <SparkFun_LTE_Shield_Arduino_Library.h>

// Create a SoftwareSerial object to pass to the LTE_Shield library
SoftwareSerial lteSerial(8, 9);
// Create a LTE_Shield object to use throughout the sketch
#define LTE_SHIELD_POWER_PULSE_PERIOD 3200
#define POWER_PIN 5
#define RESET_PIN 6
LTE_Shield lte;

int lvlpin = 11; //liquid level switch set to Pin 10
int lvlstate = digitalRead(lvlpin);  //to handle data of current State of a switch
int levelState = HIGH;

int pwrPin2 = 12; //liquid level switch set to Pin 10 OUTPUT
int pwrPin3 = 13; //liquid level switch set to Pin 10 OUTPUT

int readPin2 = A2;
int readPin3 = A3;
                    // outside leads to ground and +5V
int val = 0;  // variable to store the value read

// Plug in your Hologram device key here:
String HOLOGRAM_DEVICE_KEY = "12345678";

// These values should remain the same:
const char HOLOGRAM_URL[] = "cloudsocket.hologram.io";
const unsigned int HOLOGRAM_PORT = 9999;

// PIN Number
#define PINNUMBER ""

volatile int time_tick_hours = 1; //start tick count at 1
volatile int time_tick_report = 1; //start tick count at 1
volatile int tick_socket = 0; //integer to count socket retries

const int sleep_hours = 1;
const int ticks_per_sleep = (sleep_hours*60*60)/8;
int ticks_per_report = 442;  //default 59 min of 8 second ticks 59*60/8

//Define topicType as "S" for SETUP, "A" for ALARM, "D" for DAILY
String topicType;

int readfresh;

/////////////////////////////////////////////////////




void setup() {
  blinky();  

  //start up settings
  wdt_disable(); //disable for setup
//  sei();  //enable interrupts
//  ADCSRA |= (1<<ADEN); //ADC hex code set to on
//  power_adc_enable(); //enable ADC module    
  
  ticks_per_report = ((24*60*60/8)-225);  //subtract 30 minutes to account for shifts  

  //send startup level
  levelState = lvlState();
  topicType="";
  topicType="S";  //S == startup
  sendData(levelState, topicType); //connect LTE and send
     
  //define watchdog settings
  watchdogSET();  

  //prepare for sleep - turn off some settings
  power_adc_disable(); //disable the clock to the ADC module
  ADCSRA &= ~(1<<ADEN);  //ADC hex code set to off
}//end setup




void loop() {
  //sleep for 8 seconds  
  tickSleep();
    
  //check for daily trigger                          
  if (time_tick_report > ticks_per_report && time_tick_hours > ticks_per_sleep) {   //if number of ticks has reached 24 hours worth send text no matter what
    //turn off watchdog timer while communicating
    wdt_disable();

    //send daily text
    dailyTEXT();
          
    //reset ticks
    time_tick_report = 1;
    time_tick_hours = 1;

    //turn on watchdog timer
    watchdogSET();          
  }//end daily text/check

  //if day has not elapsed then check hourly ticks
  //hourly wake up to check for alarm trigger
  else if (time_tick_hours > ticks_per_sleep) {  //if number of ticks has reach hour goal send text 
    //turn off watchdog timer while communicating
    wdt_disable();

    //send alarm if level HIGH              
    sleepyTEXT();

    //reset ticks
    time_tick_hours = 1;   //rest ticks

    //turn on watchdog timer
     watchdogSET();                                    
  } //end hourly text/check
}//end loop




void sleepyTEXT() {
  //prepare to read sensor
  ADCSRA |= (1<<ADEN); //ADC hex code set to on
  power_adc_enable(); //enable ADC module    

  //define level state
  levelState = lvlState();  //transplant into if statement below

  if (levelState == HIGH) {    // if the sensor is over height
    topicType="";
    topicType="A";  //A=Alarm
    sendData(levelState, topicType); //connect LTE and send
  }

  //prepare for sleep
  power_adc_disable(); //disable the clock to the ADC module
  ADCSRA &= ~(1<<ADEN);  //ADC hex code set to off
}//end sleepytext void
 



void dailyTEXT() {
  //prepare to read sensor
  ADCSRA |= (1<<ADEN); //ADC hex code set to on
  power_adc_enable(); //enable ADC module    

  //send level state daily        
  levelState = lvlState();
  topicType="";
  topicType="D";  //D=Daily
  sendData(levelState, topicType); //connect LTE and send

  //prepare for sleep
  power_adc_disable(); //disable the clock to the ADC module
  ADCSRA &= ~(1<<ADEN);  //ADC hex code set to off
} //end dailyTEXT 




void sendData(int levelState, String topic) {
  //define message
  static String message;
  message = "SWT=";
  if(levelState == HIGH){
    message = "Level HIGH";
  }
  else
  {
    message = "Level Nominal";
  }

message +=":T2=";
message +=lvlInch(pwrPin2,readPin2);
message +=":T3=";
message +=lvlInch(pwrPin3,readPin3);

  // New lines are not handled well
  message.replace('\r', ' ');
  message.replace('\n', ' ');
  topic.replace('\r', ' ');
  topic.replace('\n', ' ');
  
  //connect to network
  int socket = -1;
  String hologramMessage;

  // Construct a JSON-encoded Hologram message string:
//  hologramMessage = "{\"k\":\"" + HOLOGRAM_DEVICE_KEY + "\",\"d\":\"" + message + "\"}";

  hologramMessage = "{\"k\":\"" + HOLOGRAM_DEVICE_KEY + "\",\"d\":\"" +
    message + "\",\"t\":[\"" + topic + "\"]}";

  // Power On LTE SHIELD
do{
  blinky(); //blink arduino led
  lte.begin(lteSerial, 9600);   //begin lte communication
} while (lte.getNetwork() == MNO_INVALID);

  tick_socket = 0;
  while(tick_socket < 10) {
    // Open a socket
    socket = lte.socketOpen(LTE_SHIELD_TCP);
    // On success, socketOpen will return a value between 0-5. On fail -1.
    if (socket >= 0) {
      // Use the socket to connect to the Hologram server
      if (lte.socketConnect(socket, HOLOGRAM_URL, HOLOGRAM_PORT) == LTE_SHIELD_SUCCESS) {
        // Send our message to the server:
        if (lte.socketWrite(socket, hologramMessage) == LTE_SHIELD_SUCCESS)
        {
          // On succesful write, close the socket.
          if (lte.socketClose(socket) == LTE_SHIELD_SUCCESS) {
            tick_socket = 11;
          }
        } else {
          tick_socket ++; //add one to tick
          lte.poll();
        }
      }
    }//end socket if
  }//end socket while
  
  message = ""; // Clear message string
  topic = "";   // Clear topic string

  lte.poll();

  //Press power button to turn off LTE Radio
  pinMode(POWER_PIN, OUTPUT);
  digitalWrite(POWER_PIN, LOW);
  delay(LTE_SHIELD_POWER_PULSE_PERIOD);
  pinMode(POWER_PIN, INPUT); // Return to high-impedance, rely on SARA module internal pull-up
  //lte.powerOn(); ?         
} //end sendData




int lvlState() {
  static int checkState;
  pinMode(lvlpin,INPUT); //define liquid level pin mode
  digitalWrite(lvlpin,HIGH);
   
  // read the state of the switch into a local variable:
  digitalWrite(lvlpin, HIGH);
  delay(1000);
  checkState = digitalRead(lvlpin);
  delay(10);
  checkState = digitalRead(lvlpin);
  delay(1000);
  digitalWrite(lvlpin, LOW); 

  return checkState;
} //end checkLevel



int lvlInch(int pwrpin, int readpin) {
  int lvlval = 0;
  int lvlinch = 0;
  pinMode(pwrpin,OUTPUT); //define liquid level pin mode
  digitalWrite(pwrpin,HIGH);
  delay(1000);
  lvlval = analogRead(readpin);
  delay(1000);
  lvlval = analogRead(readpin);
  delay(500);
  digitalWrite(pwrpin,LOW);
  lvlinch = (10*(lvlval-102))/(8180/(10000/((433)/12))); //converts to inches
  return lvlinch;
}

    
void tickSleep() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); 
  sleep_enable();
  sei();  //enable interrupts
  sleep_mode();
  //after about 8 seconds the Watchdog Interupt will progress the code to the disable sleep command
  sleep_disable();             
}



void blinky() {
  digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
  delay(2000);                       // wait for a second
  digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
  delay(1000);                       // wait for a second
  digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
  delay(3000);                       // wait for a second
  digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
  delay(1000);                       // wait for a second
}




void watchdogSET() {
  wdt_reset();   //reset watchdog
  WDTCSR |= 0b00011000; 
  WDTCSR = 0b00100001;
  WDTCSR = WDTCSR | 0b01000000;  //put watchdog in interupt mode (interupt will happen every 8 seconds)
  wdt_reset();   //reset watchdog
}




ISR(WDT_vect) {
    time_tick_hours ++; //for each Watchdog Interupt, adds 1 to the number of 8 second ticks counted so far
    time_tick_report ++; //seperate tick total for each day
}
