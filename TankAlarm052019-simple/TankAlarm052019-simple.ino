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

int lvlpin = 10; //liquid level switch set to Pin 10
int lvlstate = digitalRead(lvlpin);  //to handle data of current State of a switch

// Plug in your Hologram device key here:
String HOLOGRAM_DEVICE_KEY = "Ab12CdE4";

// These values should remain the same:
const char HOLOGRAM_URL[] = "cloudsocket.hologram.io";
const unsigned int HOLOGRAM_PORT = 9999;

// PIN Number
#define PINNUMBER ""

volatile int time_tick_hours = 1; //start tick count at 1
volatile int time_tick_report = 1; //start tick count at 1

const int sleep_hours = 1;
const int ticks_per_sleep = (sleep_hours*60*60)/8;
int ticks_per_report = 442;  //default 59 min of 8 second ticks 59*60/8

// char array of the telephone number to send SMS

//USE "S" for SETUP, "A" for ALARM, "D" for DAILY
//String topic;

int readfresh;

/////////////////////////////////////////////////////

void setup() {

//start up routine 
  wdt_disable(); //recomended
  sei();  //enable interrupts
  ADCSRA |= (1<<ADEN); //ADC hex code set to on
  power_adc_enable(); //enable ADC module    
//always off power saving settings

defineSETTINGS();

//

checkLevel();  

  topicType="";
  topicType="S";  //S == startup
  sendData(levelState, topicType); //connect LTE and send
    
//define watchdog settings
watchdogSET();  

//prepare for sleep - turn off some settings
    power_adc_disable(); //disable the clock to the ADC module
    ADCSRA &= ~(1<<ADEN);  //ADC hex code set to off
                         //can USART be turned off here? power_usart_disable()
}

void loop() {
  
    tickSleep();  //puts arduino to sleep for about 8 seconds
  
//wake from sleep interput here//
  
//check for daily trigger                          

if (time_tick_report > ticks_per_report && time_tick_hours > ticks_per_sleep) {   //if number of ticks has reached 24 hours worth send text no matter what
  wdt_disable();
            dailyTEXT();
          
            time_tick_report = 1;  //daily tick reset
            time_tick_hours = 1;  //hourly tick reset
  watchdogSET();          
        } //end daily text/check

//if day has not elapsed then check hourly ticks
  //hourly wake up to check for alarm trigger
else if (time_tick_hours > ticks_per_sleep) {  //if number of ticks has reach hour goal send text 
  wdt_disable();              
            sleepyTEXT();
                                    
            time_tick_hours = 1;   //rest ticks
  watchdogSET();                                    
        } //end hourly text/check
    
}
    
void tickSleep()   
{
    set_sleep_mode(SLEEP_MODE_PWR_DOWN); 
    sleep_enable();
    sei();  //enable interrupts
    sleep_mode();
//after about 8 seconds the Watchdog Interupt will progress the code to the disable sleep command
    sleep_disable();             
}

void watchdogSET()
{
    wdt_reset();   //reset watchdog
    WDTCSR |= 0b00011000; 
    WDTCSR = 0b00100001;
    WDTCSR = WDTCSR | 0b01000000;  //put watchdog in interupt mode (interupt will happen every 8 seconds)
    wdt_reset();   //reset watchdog
}

void sleepyTEXT()
{
         // turn off interupts durring sesnsor read and transmission noInterrupts ();
//prepare to read sensor
        ADCSRA |= (1<<ADEN); //ADC hex code set to on
        power_adc_enable(); //enable ADC module    

checkLevel();  //transplant into if statement below

if (levelState == HIGH) {    // if the sensor is over height
  topicType="";
  topicType="A";  //A=Alarm
  sendData(levelState, topicType); //connect LTE and send
}

//prepare for sleep
        power_adc_disable(); //disable the clock to the ADC module
        ADCSRA &= ~(1<<ADEN);  //ADC hex code set to off
        //turn interupts back on interrupts (); 
    }  //end hourly text 
} //end sleepytext void


void dailyTEXT()
{
// turn off interupts durring sesnsor read and transmission noInterrupts ();
         
//prepare to read sensor
        ADCSRA |= (1<<ADEN); //ADC hex code set to on
        power_adc_enable(); //enable ADC module    
        
checkLevel();  //pull state from void?

  topicType="";
  topicType="D";  //D=Daily
  sendData(levelState, topicType); //connect LTE and send

//apply DAILY topic to message
sendData(levelState, String topic)
// SEND DATA HERE

//prepare for sleep
        power_adc_disable(); //disable the clock to the ADC module
        ADCSRA &= ~(1<<ADEN);  //ADC hex code set to off
        //turn interupts back on interrupts (); 
      //end hourly text 
}


  
void defineSETTINGS()
{
    ticks_per_report = (((EEPROM.read(0))*60*60/8)-225);  //subtract 30 minutes to account for shifts  
  //tank one variables - maybe use a FOR statement in the future

   //subtract height of sensor from alarm height to adjust for actual reading
  alarm_one = ((((EEPROM.read(10)-EEPROM.read(40))*(8180/(10000/((EEPROM.read(20)*4)/12))))/10)+102); //converts from inches to arduino value.
}




int levelState()
{
static int checkState;
pinMode(lvlpin,INPUT); //define liquid level pin mode
digitalWrite(lvlpin,HIGH);
   
   // read the state of the switch into a local variable:
digitalWrite(lvlpin, HIGH);
delay(2000);
  checkState = digitalRead(lvlpin);
  delay(10);
  checkState = digitalRead(lvlpin);
delay(2000);
digitalWrite(lvlpin, LOW); 

return checkState;
} //end checkLevel




void sendData(int levelState, String topic)
{
message = "";
if(levelState == HIGH){
message = "Level HIGH";
}
else
{
  message = "Level Nominal";
}  

  // Power On LTE SHIELD
  lte.begin(lteSerial, 9600);   //begin lte communication
  delay(1000); //wait for power signal to work   
  //apply ALERT topic to message
  // New lines are not handled well
  message.replace('\r', ' ');
  message.replace('\n', ' ');
  topic.replace('\r', ' ');
  topic.replace('\n', ' ');
  //send message

  //connect to network
  int socket = -1;
  String hologramMessage;

  // New lines are not handled well
  message.replace('\r', ' ');
  message.replace('\n', ' ');

  // Construct a JSON-encoded Hologram message string:
  hologramMessage = "{\"k\":\"" + HOLOGRAM_DEVICE_KEY + "\",\"d\":\"" +
    message + "\"}";
  
  // Open a socket
  socket = lte.socketOpen(LTE_SHIELD_TCP);
  // On success, socketOpen will return a value between 0-5. On fail -1.
  if (socket >= 0) {
    // Use the socket to connec to the Hologram server
    if (lte.socketConnect(socket, HOLOGRAM_URL, HOLOGRAM_PORT) == LTE_SHIELD_SUCCESS) {
      // Send our message to the server:
      if (lte.socketWrite(socket, hologramMessage) == LTE_SHIELD_SUCCESS)
      {
        // On succesful write, close the socket.
        if (lte.socketClose(socket) == LTE_SHIELD_SUCCESS) {
        }
      } else {
          // TODO - tick to retry 10 times
            lte.poll();
        }
    }
  }
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

ISR(WDT_vect)
{
    time_tick_hours ++; //for each Watchdog Interupt, adds 1 to the number of 8 second ticks counted so far
    time_tick_report ++; //seperate tick total for each day
}
