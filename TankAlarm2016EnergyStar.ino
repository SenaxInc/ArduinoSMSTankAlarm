// import the GSM and rtos library
#include <GSM.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <EEPROM.h>

// PIN Number
#define PINNUMBER ""

// initialize the library instance
GSM gsmAccess;
GSM_SMS sms;

volatile int time_tick_hours = 1; //start tick count at 1
volatile int time_tick_report = 1; //start tick count at 1

const int sleep_hours = 1;
const int ticks_per_sleep = (sleep_hours*60*60)/8;
int ticks_per_report = 10575;  //23.5 hours of 8 second ticks to account for shifts in ticks total


// char array of the telephone number to send SMS
char remoteNumber[20]= "1918XXXXXXX";

// char array of the message
char txtMsg[200]="High Tank Alarm - Testing 1 2 3"; //not using right now
char myString[] = "This is the first line"
" this is the second line"
" etcetera";

String string_settingtext_raw;
String stringOne = "Power ON. Height = ";  //not sure why, but string causes output of 1 in text message

// set connection state variable
boolean notConnected = true;

// this is the threshold used for reading
int alarm_one = 310; //default value for 5 feet
int readvalue_one;
int constant_one;
int contents_one;

int settingtext_tanknumber;
int settingtext_value;
int settingtext_tankcontents;
int readfresh;

void setup() {
//start up routine 
  wdt_disable(); //recomended
  sei();  //enable interrupts
  ADCSRA |= (1<<ADEN); //ADC hex code set to on
  power_adc_enable(); //enable ADC module    
//always off power saving settings

  
//power up and read sensor - GSM shield uses pins 0,1,2,3,7 + 8 for mega, 10 for yun 
        digitalWrite(5, HIGH);  //pin #5 powers 5V to sensor
        pinMode(5, OUTPUT);
        delay(10000); //wait for sensor signal to normalize    
        readfresh = analogRead(A1);  //dummy read to refresh adc after wake up
        delay(2000);
        readvalue_one = analogRead(A1);  // read a sensor from analog pin #A1
        delay(1000);
        digitalWrite(5, LOW); // turn off sensor
  
  
  // Power On GSM SHIELD          
            digitalWrite(7, HIGH);  //pin seven powers on GSM shield
            pinMode(7, OUTPUT);
            delay(500); //wait for power signal to work   
            digitalWrite(7, LOW); // turn off power signal
  
  //connect to network
              while(notConnected) {  //when not connected check for connection
                if(gsmAccess.begin(PINNUMBER)==GSM_READY) //check for a GSM connection to network
                   notConnected = false;   //when connected, move on 
                else {
                      delay(1000); //if not connected, wait another second to check again
                }
            }
  sms.beginSMS(remoteNumber);
  sms.print(readvalue_one); 
  sms.endSMS();
  delay(10000);
  //READ RECIEVED TEXTS HERE

  receiveSETTINGS();
  
  //SHUTDOWN GSM
  gsmAccess.shutdown();
  notConnected = true;


//SET START UP EEPROM VARIABLES  
  
//On Startup - DEFINE ALARM TRIGGER FROM EEPROM DATA


contents_one = EEPROM.read(30);

if(contents_one == 1)
{
  constant_one = (EEPROM.read(20))/100;
}
if(contents_one == 2)
{
  constant_one = 3+((EEPROM.read(20))/1000);
}
alarm_one = ((constant_one*EEPROM.read(10))+114); //also convert from inches to arduino value

ticks_per_report = (((EEPROM.read(0))*60*60/8)-225);  //subtract 30 minutes to account for shifts
  
  
watchdogSET();  //define watchdog settings
  

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

else{  //if day has not elapsed then check hourly ticks
  //hourly wake up to check for alarm trigger
        if (time_tick_hours > ticks_per_sleep) {  //if number of ticks has reach hour goal send text 
  wdt_disable();              
            sleepyTEXT();
                                    
            time_tick_hours = 1;   //rest ticks
  watchdogSET();                                    
        } //end hourly text/check
    }
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
//power up sensor - GSM shield uses pins 0,1,2,3,7 + 8 for mega, 10 for yun 
        digitalWrite(5, HIGH);  //pin #5 powers 5V to sensor
        pinMode(5, OUTPUT);
        delay(10000); //wait for sensor signal to normalize    
        readfresh = analogRead(A1);  //dummy read to refresh adc after wake up
        delay(2000);
        readvalue_one = analogRead(A1);  // read a sensor from analog pin #A1
        delay(1000);
        digitalWrite(5, LOW); // turn off sensor
    
        if (readvalue_one > alarm_one) {{ // if the sensor is over height
// prepare to send SMS
            //turn on USART to be ready for GSM
          
            delay(6000);    //delay to normalize
// Power On GSM SHIELD          
            digitalWrite(7, HIGH);  //pin seven powers on GSM shield
            pinMode(7, OUTPUT);
            delay(500); //wait for power signal to work   
            digitalWrite(7, LOW); // turn off power signal
          

// Connect to GSM network
            while(notConnected) {  //when not connected check for connection
                if(gsmAccess.begin(PINNUMBER)==GSM_READY) //check for a GSM connection to network
                   notConnected = false;   //when connected, move on 
                else {
                      delay(1000); //if not connected, wait another second to check again
                }
            }
        }
//Send SMS                                    
        sms.beginSMS(remoteNumber);
        sms.print(readvalue_one);
        sms.endSMS();
        //Check for settings messages here
        receiveSETTINGS();                          
                                  
        gsmAccess.shutdown(); //turn off GSM once text sent
        notConnected = true;                                    
        delay(4000);
//prepare for sleep
        power_adc_disable(); //disable the clock to the ADC module
        ADCSRA &= ~(1<<ADEN);  //ADC hex code set to off
        //turn interupts back on interrupts (); 
    }  //end hourly text 
}


void dailyTEXT()
{
         // turn off interupts durring sesnsor read and transmission noInterrupts ();
//prepare to read sensor
        ADCSRA |= (1<<ADEN); //ADC hex code set to on
        power_adc_enable(); //enable ADC module    
//power up sensor - GSM shield uses pins 0,1,2,3,7 + 8 for mega, 10 for yun 
        digitalWrite(5, HIGH);  //pin five powers 5V to sensor
        pinMode(5, OUTPUT);
        delay(6000); //wait for sensor signal to normalize    
        readfresh = analogRead(A1);  //dummy read to refresh adc after wake up
        delay(2000);
        readvalue_one = analogRead(A0);  // read a sensor from analog pin 0
        digitalWrite(5, LOW); // turn off sensor
    
// prepare to send SMS
            //turn on USART to be ready for GSM
          
            delay(6000);    //delay to normalize
// Power On GSM SHIELD          
            digitalWrite(7, HIGH);  //pin seven powers on GSM shield
            pinMode(7, OUTPUT);
            delay(500); //wait for power signal to work   
            digitalWrite(7, LOW); // turn off power signal
          

// Connect to GSM network
            while(notConnected) {  //when not connected check for connection
                if(gsmAccess.begin(PINNUMBER)==GSM_READY) //check for a GSM connection to network
                   notConnected = false;   //when connected, move on 
                else {
                      delay(1000); //if not connected, wait another second to check again
                }
            }
        
//Send SMS                                    
        sms.beginSMS(remoteNumber);
        sms.print(readvalue_one); //INCLUDE CURRENT EEPROM SETTINGS IN TEXT
        sms.endSMS();
        gsmAccess.shutdown(); //turn off GSM once text sent
        notConnected = true;                                    
        delay(4000);
//prepare for sleep
        power_adc_disable(); //disable the clock to the ADC module
        ADCSRA &= ~(1<<ADEN);  //ADC hex code set to off
        //turn interupts back on interrupts (); 
      //end hourly text 
}

void receiveSETTINGS()
{
   //READ RECIEVED TEXTS HERE
  while (sms.available() > 0)
  {
        if(sms.peek() == 'A') //A = ALARM
    {
          //extract text stream into string
          string_settingtext_raw = sms.readString();  
          
          //delete "A" from begining of string here
          string_settingtext_raw.remove(1,1);

          //READ NEXT DIGIT AFTER "A" TO ASSIGN TO TANK # 1-9
          settingtext_tanknumber = string_settingtext_raw.charAt(1);
          
          //delete tank number from begining of string here
          string_settingtext_raw.remove(1,1); 
          
          //convert the remaining digits in the string to the trigger intiger
          settingtext_value = string_settingtext_raw.toInt();
          
          //clear out string for fun
          string_settingtext_raw = "";          

          //WRITE NEW TRIGGER NUMBER TO EEPROM HERE
          EEPROM.update(9+settingtext_tanknumber, settingtext_value); 
          
          //wait for eeprom just for fun
          delay(1000);
    }
            if(sms.peek() == 'C') //C = Constant
    {
          //extract text stream into string
          string_settingtext_raw = sms.readString();  
          
          //delete "C" from begining of string here
          string_settingtext_raw.remove(1,1);

          //READ NEXT DIGIT AFTER "C" TO ASSIGN TO TANK # 1-9
          settingtext_tanknumber = string_settingtext_raw.charAt(1);
          
          //delete tank number from begining of string here
          string_settingtext_raw.remove(1,1); 
              
          //read digit after tank number to get tank contents code number (1 or 2)
          settingtext_tankcontents = string_settingtext_raw.charAt(1);
          
          //delete contents code from string
          string_settingtext_raw.remove(1,1); 

          //WRITE content code NUMBER TO EEPROM HERE
          EEPROM.update(29+settingtext_tanknumber, settingtext_tankcontents);     
          
          //wait for eeprom just for fun
          delay(1000);
              
          //convert the remaining digits in the string to the constant intiger
          //divide by 4 to store in eeprom
          settingtext_value = (string_settingtext_raw.toInt())/4;
          
          //clear out string for fun
          string_settingtext_raw = "";          

          //WRITE NEW psi constant TO EEPROM HERE
          EEPROM.update(19+settingtext_tanknumber, settingtext_value); 
          
          //wait for eeprom just for fun
          delay(1000);
    }
            if(sms.peek() == 'T') //T = time between reports
    {
          //extract text into string
          string_settingtext_raw = sms.readString();  
          
          //delete "T" from begining of string here
          string_settingtext_raw.remove(1,1);
          
          //convert the remaining digits in the string to the trigger intiger
          settingtext_value = string_settingtext_raw.toInt();
          
          //clear out string for fun
          string_settingtext_raw = "";          

          //WRITE NEW TRIGGER NUMBER TO EEPROM HERE
          EEPROM.update(0, settingtext_value); 
          
          //wait for eeprom just for fun
          delay(1000);
    }
                if(sms.peek() == 'H')
    {
          //extract text into string
          string_settingtext_raw = sms.readString();  
          
          //delete "H" from begining of string here
          string_settingtext_raw.remove(1,1);

          //READ NEXT DIGIT AFTER "H" TO ASSIGN TO TANK # 1-9
          settingtext_tanknumber = string_settingtext_raw.charAt(1);
          
          //delete tank number from begining of string here
          string_settingtext_raw.remove(1,1); 
          
          //convert the remaining digits in the string to the height intiger
          settingtext_value = string_settingtext_raw.toInt();
          
          //clear out string for fun
          string_settingtext_raw = "";          

          //WRITE NEW TRIGGER NUMBER TO EEPROM HERE
          EEPROM.update(39+settingtext_tanknumber, settingtext_value); 
          
          //wait for eeprom just for fun
          delay(1000);
    }
              //delete text
          sms.flush();
  } 
}
  
  
ISR(WDT_vect)
{
    time_tick_hours ++; //for each Watchdog Interupt, adds 1 to the number of 8 second ticks counted so far
    time_tick_report ++; //seperate tick total for each day
}
