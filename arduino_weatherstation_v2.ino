// Open source weather station for ERC Altiplano, based on an Arduino Uno
// (c)2018-2019, Levien van Zon (levien at gnuritas .org)
// Further information: 
// http://gnuritas.org/weatherstation.html
// https://github.com/lvzon/weatherstation

#include <Arduino.h>
#include <Wire.h>               # https://www.arduino.cc/en/reference/wire
#include <NeoSWSerial.h>
#include "Adafruit_SHT31.h"     # https://github.com/adafruit/Adafruit_SHT31
#include "RTClib.h"

// Uncomment to enable verbose output
//#define VERBOSE             1

// Echo data to serial port
//#define ECHO_TO_SERIAL   1 

// Read ressure sensor 
#define READ_PRESSURE 1

#ifdef READ_PRESSURE
#include <LPS.h>                // Pressure sensor: https://github.com/pololu/lps-arduino
#endif

#include <DS18B20.h>            // Soil temperature: https://github.com/matmunk/DS18B20
#include <OneWire.h>


#define USE_SD      0
#define USE_MODEM   1

#if USE_SD
    #define FAT16 1
    
    #if FAT16
        #include <Fat16.h>  // The FAT16 library is a little smaller than the SD-library, but still too big for use on the Uno...
    #else
        #include <SD.h>       
    #endif
#endif

RTC_DS1307 rtc;

Adafruit_SHT31 temphum = Adafruit_SHT31();

#ifdef READ_PRESSURE
LPS pressure_sensor;
#endif

#if USE_MODEM
    #define PIN_MODEMPWR    9
    NeoSWSerial modem(7, 8);
    // To use hardware serial:
    // #define modem Serial
#endif

// Here you need to set the APN for your SIM card
#define MODEM_START_TASK "AT+CSTT=\"your.apn\",\"\",\"\""

// Here you need to set your server and port (currently 9000)
#define MODEM_START_IP "AT+CIPSTART=\"tcp\",\"your.server.org\",\"9000\""

#define PIN_ANEMOMETER  2     // Cup anemometer connected to Digital 2
#define PIN_RAINGAUGE   3     // Rain gauge connected to Digital 3
#define PIN_VANE        0     // Wind vane connected to Analog 0

#define PIN_CURRENT     1      // Current sensor on A1

#define PIN_SOILMOIST   2      // Soil moisture sensor on A2

#define PIN_SOILTEMP    4      // Soil temperature sensors on D4

DS18B20 dstemp(PIN_SOILTEMP);


// for the data logging shield, we use digital pin 10 for the SD cs line
const int chipSelect = 10;

// Delay between reading out the sensors in milliseconds, should be at least a few seconds to prevent temperature sensor heating up too much
#define SENSOR_READ_DELAY_MS  12000  

// Clock offset in seconds, needed because compilation and uploading takes a few seconds, and the compile time is used to set the clock
#define TIME_ADJUSTMENT 5             


#define NUMDIRS 8   // Wind vane directions

// Scaled voltage readings per wind direction, you may have to adjust these a bit for your setup
// int adc[NUMDIRS] = {26, 45, 77, 118, 161, 196, 220, 256};
int adc[NUMDIRS] = {26, 47, 77, 118, 161, 198, 225, 256};

// North here is with the nose of the vane toward the assembly centre. 
// The vane points to where the wind blows from.
char *winddir[NUMDIRS + 1] = {"N","NE","E","NW","SE","W","SW","S","NA"};
int windangle[NUMDIRS + 1] = {0,45,90,315,135,270,225,180,-1};
byte diroffset = 0;

#define DEBOUNCETIME_MS     15      // Idle time after each pulse, to allow for "bouncing" of the reed switch contacts

volatile unsigned long pulses_anemometer = 0;   // cup rotation counter used in interrupt routine
volatile unsigned long t_cb_anmm;               // Timer to avoid contact bounce in interrupt routine
volatile unsigned long pulses_raingauge = 0;    // Incremented in the interrupt
volatile unsigned long t_cb_rg;                 // Timer to avoid contact bounce in interrupt routine

unsigned long   ts_start_anmm,
                ts_last_anmm,
                ts_start_rg,
                ts_last_rg,
                ts_last_temphum = 0,
                ts_report;

float temp_sum = 0, hum_sum = 0;
float temp_max = 0, hum_max = 0;
float temp_min = 100, hum_min = 100;
float rain_sum = 0;
unsigned long nm_temp = 0, nm_hum = 0;

unsigned long wind_sum = 0, wind_max = 0, wind_min = 200, nm_wind = 0;
unsigned long windangle_sum = 0, nm_winddir = 0;
int winddir_max = 8;

float I_sum = 0;
unsigned long nm_I = 0;

byte rainsum_reset_flag = 0;

float T_soil = 0; 
float pressure_mbar = 0;
int soilmoisture_raw = 0;


#if USE_SD    
    #if FAT16
        // Fat16 variables
        SdCard  card;
        Fat16   csv;
    #else
        File csv;   // the logging file
    #endif
#endif

// Field separator
const char sep[] = ",";

// Measurement interval (seconds)
#define MINT_SEC_TEMPHUM    10
#define MINT_SEC_WIND       30

// Reporting interval (minutes)
#define RINT_MIN            10


void error(char *str)
{
  Serial.print("ERROR: ");
  Serial.println(str);
  
  // red LED indicates error
  //digitalWrite(redLEDpin, HIGH);

  while(1)
    delay(1);
}


void setup () {

    #if VERBOSE
        while (!Serial)
            delay(10);     // will pause Zero, Leonardo, etc until serial console opens
    #endif
    
    Serial.begin(9600);

    Wire.begin();
    
    #if USE_MODEM
        modem.begin(9600);
        pinMode(PIN_MODEMPWR, OUTPUT);
        digitalWrite(PIN_MODEMPWR, LOW); 
        delay(1000);               // wait for 1 second
        digitalWrite(PIN_MODEMPWR, HIGH);
    #endif

    if (!rtc.begin()) {
        error("Couldn't find RTC");
    }
    
    if (!rtc.isrunning()) {
        
        Serial.println("RTC is NOT running!");
        
        // following line sets the RTC to the date & time this sketch was compiled
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        DateTime t = DateTime(rtc.now().unixtime() + TIME_ADJUSTMENT);
        rtc.adjust(t); 
        
        // This line sets the RTC with an explicit date & time, for example to set
        // January 21, 2014 at 3am you would call:
        // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
    }

    if (!temphum.begin(0x44)) {   // Set to 0x45 for alternate i2c addr
        error("Couldn't find temperature/humidity sensor");
    }
    
    #ifdef READ_PRESSURE
    if (!pressure_sensor.init())
    {
      error("Failed to autodetect pressure sensor!");
    } else {
      pressure_sensor.enableDefault();
    }
    #endif
    
    pinMode(PIN_ANEMOMETER, INPUT);
    digitalWrite(PIN_ANEMOMETER, HIGH);
    digitalWrite(PIN_RAINGAUGE, HIGH);
    attachInterrupt(0, count_anemometer, FALLING);
    attachInterrupt(1, count_raingauge, FALLING);
    
    DateTime now = rtc.now();
    unsigned long unixtime = now.unixtime();
    ts_start_anmm = unixtime;
    ts_start_rg = unixtime;
    ts_report = unixtime;
    
    #if USE_SD
        // initialize the SD card
        #if VERBOSE
            Serial.print("Initializing SD card...");
        #endif
        // make sure that the default chip select pin is set to
        // output, even if you don't use it:
        pinMode(chipSelect, OUTPUT);
        
        // see if the card is present and can be initialized:
        #if FAT16
          int result;
          if (!card.init() || !Fat16::init(&card)) {
        #else
          if (!SD.begin(chipSelect)) {
        #endif
            error("SD card failed, or not present");
        }
        #if VERBOSE
            Serial.println("SD card initialized.");
        #endif
        
        // create a new file
        char filename[] = "LOG000.CSV";
        for (uint8_t i = 0; i < 1000; i++) {
            filename[3] = i/100 + '0';
            filename[4] = (i/10)%10 + '0';
            filename[5] = i%10 + '0';
            #if FAT16
              result = csv.open(filename, O_APPEND | O_CREAT | O_WRITE);
              break;
            #else
              if (! SD.exists(filename)) {
                // only open a new file if it doesn't exist
                csv = SD.open(filename, FILE_WRITE); 
                break;  // leave the loop!
              }
            #endif
        }
        
        #if FAT16
        if (!result) {
        #else
        if (!csv) {
        #endif
            error("couldn't create file");
        }
        
        #if VERBOSE
            Serial.print("Logging to: ");
            Serial.println(filename);
        #endif
    #endif
    
    const char header[] = "start,duration,T_mean,T_min,T_max,RH_mean,RH_min,RH_max,rain_mm,rain_mm_sum,wind_mean,wind_min,wind_max,winddir_mean,winddir_max,I_mean,runtime_s,pressure_mbar,T_soil";
    #if USE_CSV
        csv.println(header);
    #endif
    #if ECHO_TO_SERIAL
        Serial.println(header);
    #endif
}

void loop () {
    
    DateTime now = rtc.now();
    unsigned long unixtime = now.unixtime();  // Note that this only gives actual UNIX epoch time if the RTC is set to UTC
    int hour = now.hour();
    //int minute = now.minute();
    
    if (hour == 0 && !rainsum_reset_flag) {
        rain_sum = 0;
        rainsum_reset_flag = 1;
    } else if (hour != 0) {
        rainsum_reset_flag = 0;
    }
    
    #if VERBOSE
        //Serial.println(unixtime);
    #endif
    
    ts_last_anmm = unixtime;
    ts_last_rg = unixtime;
    
    if ((unixtime - ts_start_anmm) >= MINT_SEC_WIND) {
        unsigned long ws = get_windspeed();
        int wd = get_winddir();
        wind_sum += ws;
        windangle_sum += windangle[wd];
        if (ws > wind_max) {
            wind_max = ws;
            winddir_max = wd;
        }
        if (ws < wind_min) {
            wind_min = ws;
        }
        nm_wind++;
        nm_winddir++;
    }
    
    if ((unixtime - ts_last_temphum) >= MINT_SEC_TEMPHUM) {
    
        float t = temphum.readTemperature();
        float h = temphum.readHumidity();
        ts_last_temphum = unixtime;

        if (! isnan(t)) {  // check if 'is not a number'
            temp_sum += t;
            if (t > temp_max)
                temp_max = t;
            if (t < temp_min)
                temp_min = t;
            nm_temp++;
        } else { 
            Serial.println("Failed to read temperature");
        }
        
        if (! isnan(h)) {  // check if 'is not a number'
            hum_sum += h;
            if (h > hum_max)
                hum_max = h;
            if (h < hum_min)
                hum_min = h;
            nm_hum++;
        } else { 
            Serial.println("Failed to read humidity");
        }

        // Get atmospheric pressure (in mbar)
        
        #ifdef READ_PRESSURE
        pressure_mbar = pressure_sensor.readPressureMillibars();
        #else
        pressure_mbar = 0;
        #endif
        
        // Get soil temperature

        T_soil = dstemp.getTempC();
        
        // Get soil moisture

        soilmoisture_raw = analogRead(PIN_SOILMOIST);

        
        // Get PV current measured by ACS712 current sensor (30A max) 
        int I_raw = analogRead(PIN_CURRENT);
        float I_mV = (I_raw / 1024.0) * 5000;   // Gets you mV
        float I = (I_mV - 2500) / 66;    // Measured current in A
        I_sum += I;
        nm_I++;
        
        #if VERBOSE
            Serial.print("Temperature Celsius: "); 
            Serial.print(t);
            Serial.print(" Humididy %: "); 
            Serial.println(h);
            Serial.print("Pressure (mbar): "); 
            Serial.println(pressure_mbar);
            Serial.print("Soil temperature Celsius: "); 
            Serial.println(T_soil);
            Serial.print("Soil moisture reading: "); 
            Serial.println(soilmoisture_raw);
            Serial.print("Current (A): ");             
            Serial.println(I);
        #endif
    }
    
    if ((unixtime - ts_report) >= (RINT_MIN * 60)) {
        
        // We've reached the end of the reporting interval, report our measurements
        
        float T_mean = (float)temp_sum / (float)nm_temp;
        float RH_mean = (float)hum_sum / (float)nm_hum;
        float wind_mean = ((float)wind_sum / 10.0) / (float)nm_wind;
        float I_mean = I_sum / (float)nm_I;
        float rain_mm = calc_rainfall();
        rain_sum += rain_mm;

        // Calculating the average of angles (bearings) in this way will produce incorrect results
        // in some cases. This can be fixed by summing sin and cos of the angles, and then
        // taking atan2(sum_sin, sum_cos). This is possible on Arduino, but we'll not do 
        // it for now, to keep things simple...
        int windangle_mean = windangle_sum / nm_winddir;

        unsigned long runtime_s = millis() / 1000;
        
        #if USE_CSV        
            csv.print(ts_report);
            csv.print(sep);
            csv.print(unixtime - ts_report);
            csv.print(sep);
            csv.print(T_mean);
            csv.print(sep);
            csv.print(temp_min);
            csv.print(sep);
            csv.print(temp_max);
            csv.print(sep);
            csv.print(RH_mean);
            csv.print(sep);
            csv.print(hum_min);
            csv.print(sep);
            csv.print(hum_max);
            csv.print(sep);
            csv.print(rain_mm);
            csv.print(sep);
            csv.print(rain_sum);
            csv.print(sep);
            csv.print(wind_mean);
            csv.print(sep);
            csv.print((float)wind_min / 10.0);
            csv.print(sep);
            csv.print((float)wind_max / 10.0);
            csv.print(sep);
            csv.print(windangle_mean);
            csv.print(sep);
            csv.print(winddir[winddir_max]);
            csv.print(sep);
            csv.print(I_mean);
            csv.print(sep);
            csv.print(runtime_s);
            csv.print(sep);
            csv.print(pressure_mbar);
            csv.print(sep);
            csv.print(T_soil);
            //csv.print(sep);
            //csv.print(soilmoisture_raw);
            csv.println();
        #endif
        
        #if ECHO_TO_SERIAL
            Serial.print(ts_report);
            Serial.print(sep);
            Serial.print(unixtime - ts_report);
            Serial.print(sep);
            Serial.print(T_mean);
            Serial.print(sep);
            Serial.print(temp_min);
            Serial.print(sep);
            Serial.print(temp_max);
            Serial.print(sep);
            Serial.print(RH_mean);
            Serial.print(sep);
            Serial.print(hum_min);
            Serial.print(sep);
            Serial.print(hum_max);
            Serial.print(sep);
            Serial.print(rain_mm);
            Serial.print(sep);
            Serial.print(rain_sum);
            Serial.print(sep);
            Serial.print(wind_mean);
            Serial.print(sep);
            Serial.print((float)wind_min / 10.0);
            Serial.print(sep);
            Serial.print((float)wind_max / 10.0);
            Serial.print(sep);
            Serial.print(windangle_mean);
            Serial.print(sep);
            Serial.print(winddir[winddir_max]);
            Serial.print(sep);
            Serial.print(I_mean);
            Serial.print(sep);
            Serial.print(runtime_s);
            Serial.print(sep);
            Serial.print(pressure_mbar);
            Serial.print(sep);
            Serial.print(T_soil);
            //Serial.print(sep);
            //Serial.print(soilmoisture_raw);
            Serial.println();
        #endif

        #if USE_MODEM
        
            modem_opensocket();

            modem.print(ts_report);
            modem.print(sep);
            modem.print(unixtime - ts_report);
            modem.print(sep);
            modem.print(T_mean);
            modem.print(sep);
            modem.print(temp_min);
            modem.print(sep);
            modem.print(temp_max);
            modem.print(sep);
            modem.print(RH_mean);
            modem.print(sep);
            modem.print(hum_min);
            modem.print(sep);
            modem.print(hum_max);
            modem.print(sep);
            modem.print(rain_mm);
            modem.print(sep);
            modem.print(rain_sum);
            modem.print(sep);
            modem.print(wind_mean);
            modem.print(sep);
            modem.print((float)wind_min / 10.0);
            modem.print(sep);
            modem.print((float)wind_max / 10.0);
            modem.print(sep);
            modem.print(windangle_mean);
            modem.print(sep);
            modem.print(winddir[winddir_max]);
            modem.print(sep);
            modem.print(I_mean);
            modem.print(sep);
            modem.print(runtime_s);
            modem.print(sep);
            modem.print(pressure_mbar);
            modem.print(sep);
            modem.print(T_soil);
            //modem.print(sep);
            //modem.print(soilmoisture_raw);
            modem.println();
            
            modem_closesocket();
        #endif
                
        // Reset counters
        
        temp_sum = 0;
        hum_sum = 0;
        wind_sum = 0;
        windangle_sum = 0;
        I_sum = 0;
        
        nm_temp = 0;
        nm_hum = 0;
        nm_wind = 0;
        nm_winddir = 0;
        nm_I = 0;
        
        temp_max = 0;
        hum_max = 0;
        wind_max = 0;
        winddir_max = 8;
        
        temp_min = 100;
        hum_min = 100;
        wind_min = 200;
        
        ts_report = unixtime;
    }

    delay(SENSOR_READ_DELAY_MS);
}


#if USE_MODEM

void readmodem()
{
    while(modem.available() != 0) {
      #if VERBOSE
        Serial.write(modem.read());
      #else
        modem.read();
      #endif    
    }
}


void atcmd(char *cmd, int delay_ms)
{
    #if VERBOSE
        Serial.println(cmd);
    #endif
    modem.println(cmd);
    delay(delay_ms);
    readmodem();
}


void modem_opensocket()
{
    #if VERBOSE
        //atcmd("ATE1", 500);
        atcmd("AT+COPS?", 1000);
        atcmd("AT+CSQ", 1000);
    #endif
    
    atcmd("AT+CFUN=1", 500);
    atcmd(MODEM_START_TASK, 2000);  // Start Task and Set APN, USER NAME, PASSWORD
    atcmd("AT+CIICR", 3000);        // Bring Up Wireless Connection with GPRS or CSD
    atcmd("AT+CIFSR", 1000);        // Get local IP address    
    atcmd("AT+CIPSPRT=0", 500);     // Disable ">" send prompt
    
    atcmd(MODEM_START_IP, 4000);    // Open socket

    atcmd("AT+CIPSEND", 4000);    // Initiate data input, until CTRL-Z
}

void modem_closesocket()
{
    modem.println((char)26);    // CTRL-Z to send
    delay(10000);               // wait for reply, important! the time is base on the condition of internet 
    modem.println();
    atcmd("AT+CIPCLOSE", 1000); // Close socket
}

#endif


//=======================================================
// Interrupt handler for anemometer. 
// Called each time the reed switch triggers.
//=======================================================

void count_anemometer() {
   if ((millis() - t_cb_anmm) > DEBOUNCETIME_MS) { // ignore bouncing of the switch contact.
      pulses_anemometer++;
      t_cb_anmm = millis();
   } 
}

//=======================================================
// Interrupt handler for rain gauge.
// Called each time the reed switch triggers.
//=======================================================

void count_raingauge() {
   if ((millis() - t_cb_rg) > DEBOUNCETIME_MS) { // debounce the switch contact.
      pulses_raingauge++;
      t_cb_rg = millis();
   } 
}


//=======================================================
// Find wind vane direction.
//=======================================================

int read_winddir() {
    int val = analogRead(PIN_VANE);
    return val >> 2;         // Shift to 255 range
}

int calc_winddir(byte val) {

    byte x;
    
    // Look the reading up in directions table. Find the first value
    // that's >= to what we got.
    for (x = 0 ; x < NUMDIRS ; x++) {
      if (adc[x] >= val)
        break;
    }
    
    x = (x + diroffset) % 8;   // Adjust for orientation
    
    return x;
}

int get_winddir() {
    
    byte dir = calc_winddir(read_winddir());
        
    #if VERBOSE
        Serial.print("  Dir: ");
        Serial.print(winddir[dir]);
        Serial.print("  Value: ");
        Serial.println(dir);
    #endif
    
    return dir;
}


//=======================================================
// Calculate the wind speed.
// 1 pulse/sec = 1.492 mph = 2.40114125 kph
//=======================================================

long get_windspeed() {
    
    long t_ms = (ts_last_anmm - ts_start_anmm) * 1000;
    
    cli();          // Disable interrupts while we copy and reset the counter value
    unsigned long pulses = pulses_anemometer;
    pulses_anemometer = 0;        // Reset counters
    ts_start_anmm = ts_last_anmm;
    sei();          // Re-enable interrupts
    
    // Speed will be in km/h * 10
    long speed = 24011;
    speed *= pulses;
    speed /= t_ms;
    
    #if VERBOSE
        Serial.print("Wind speed: ");
        int x = (int)speed / 10;
        Serial.print(x);
        Serial.print('.');
        x = (int)speed % 10;
        Serial.print(x);
        Serial.print(" Pulses: ");
        Serial.print(pulses);
        Serial.print(" Time (ms): ");
        Serial.print(t_ms);
    #endif
        
    return speed;
}

//=======================================================
// Calculate the rainfall volume.
// 1 bucket = 0.2794 mm
//=======================================================

float calc_rainfall() {

    long t_ms = (ts_last_rg - ts_start_rg) * 1000;
    
    cli();          // Disable interrupts while we copy and reset the counter value
    unsigned long pulses = pulses_raingauge;
    pulses_raingauge = 0;        // Reset counters
    ts_start_rg = ts_last_rg;
    sei();          // Re-enable interrupts
    
    float vol = 0.2794; // 0.2794 mm per pulse
    vol *= pulses;
    
    #if VERBOSE
        Serial.print("Rainfall: ");
        Serial.print(vol);        
        Serial.print(" Pulses: ");
        Serial.print(pulses);
        Serial.print(" Time (ms): ");
        Serial.print(t_ms);
        Serial.println();
    #endif
    
    return vol;
}


