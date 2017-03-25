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

volatile int time_tick_hours = 1;
volatile int time_tick_daily = 1;

const int sleep_hours = 1;
const int ticks_per_sleep = (sleep_hours*60*60)/8;
const int ticks_per_day = 10575;  //23.5 hours of 8 second ticks to account for shifts in ticks total

// char array of the telephone number to send SMS
char remoteNumber[20]= "1918XXXXXXX";

// char array of the message
char txtMsg[200]="High Tank Alarm - Testing 1 2 3"; //not using right now

String stringOne = "Power ON. Height = ";  //not sure why, but string causes output of 1 in text message

// set connection state variable
boolean notConnected = true;

// this is the threshold used for reading
const int trigger = 500;
int readvalue;
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
        readvalue = analogRead(A1);  // read a sensor from analog pin #A1
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
  sms.print(readvalue);
  sms.endSMS();
  
  //READ RECIEVED TEXTS HERE
  
  //WRITE NEW TRIGGER NUMBER TO EEPROM HERE
  EEPROM.update(address, value) 
  
    
  //SHUTDOWN GSM
  gsmAccess.shutdown();
  notConnected = true;

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

        if (time_tick_daily > ticks_per_day && time_tick_hours > ticks_per_sleep) {   //if number of ticks has reached 24 hours worth send text no matter what
  wdt_disable();
            dailyTEXT();
          
            time_tick_daily = 1;  //daily tick reset
            time_tick_hours = 1;  //hourly tick reset
  watchdogSET();          
        } //end daily text/check

else{  //if day has not elapsed then check hourly ticks
  
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
        readvalue = analogRead(A1);  // read a sensor from analog pin #A1
        delay(1000);
        digitalWrite(5, LOW); // turn off sensor
    
        if (readvalue > trigger) {{ // if the sensor is over height
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
        sms.print(readvalue);
        sms.endSMS();
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
        readvalue = analogRead(A0);  // read a sensor from analog pin 0
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
        sms.print(readvalue);
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

ISR(WDT_vect)
{
    time_tick_hours ++; //for each Watchdog Interupt, adds 1 to the number of 8 second ticks counted so far
    time_tick_daily ++; //seperate tick total for each day
}
