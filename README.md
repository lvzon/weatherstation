# Open source weather station

This code was used to build a remote weather station for [Ecosystem Restoration Camp Altiplano](https://www.ecosystemrestorationcamps.org/camp-altiplano/), based on an Arduino board. This repository includes the sketch for the [Arduino Uno](https://store.arduino.cc/arduino-uno-rev3), and a simple Python server for logging the data and forwarding it to the [Citizen Weather Observer Program (CWOP)](http://www.wxqa.com/) and the [Weather Underground](https://www.wunderground.com/weatherstation/overview.asp) network.


For a description of the project, see: http://gnuritas.org/weatherstation.html


### Sensors and shields used

   - To measure wind speed, wind direction and rainfall, I used a set of plastic weather station parts, including an anemometer, a wind vane and a tipping bucket rain gauge. These can be ordered from [Sparkfun as article SEN-08942](https://www.sparkfun.com/products/8942), or through [AliExpress](https://www.aliexpress.com/item/1-set-of-Spare-part-outdoor-unit-for-Professional-Wireless-Weather-Station/1214985366.html) as "Spare part outdoor unit for professional wireless weather station".
   - To measure air temperature and relative humidity, I used a [Sensirion SHT31-D temperature and humidity sensor](https://www.adafruit.com/product/2857), which has an accuracy of ±0.3°C and ±2% relative humidity, and is connected though I2C. Other sensors can also be used of course, as long as they have a reasonable accuracy. Cheap sensors such as the [DHT11](https://www.adafruit.com/product/386) tend to be very inaccurate, especially with regard to humidity, so try to avoid these.
   - To measure air pressure I used an [LPS25H air pressure sensor](https://www.pololu.com/product/2724), which has an accuracy of ±0.2 mbar. Again, other good-quality sensors can also be used. This sensor is also connected to the I2C bus.
   - To measure soil temperature I used a waterproof [DS18B20 temperature sensor](https://www.itead.cc/waterproof-ds18b20-temperature-sensor.html). Not the most accurate sensor, but cheap and good enough to give a reasonable indication, and several sensors can be used on one connection (a one-wire bus).
   - For time keeping (and possibly data storage in future), I used a [data logger shield](https://learn.adafruit.com/adafruit-data-logger-shield), which includes an SD-card slot and a battery-powered real-time clock (RTC). The shield I have uses a DS1307 RTC, but other shields may provide other RTC chips (which may actually be better).
   - For communication I used a [modem shield with a SimCom SIM808 cellular modem](https://www.elecrow.com/wiki/index.php?title=SIM808_GPRS/GSM%2BGPS_Shield_v1.1). There are various shields available, with various SimCom modem types (SIM80x, SIM90x) that all use basically the same [AT-command set](https://www.elecrow.com/wiki/images/2/20/SIM800_Series_AT_Command_Manual_V1.09.pdf). These modems are inexpensive (under $10), although they are limited to GPRS/2G, and the shields for Arduino may actually be quite costly. You can also look for a cheap SimCom modem module and connect it to the relevant Arduino pins, but note that the modem is 3.3V so you may need to use a level shifter if you use a 5V Arduino board. Other modem brands can also be used, but will probably require different AT-commands. A SIM-card will also be needed in order to send the data over a cellular data link. I used an international roaming IoT SIM from Tele2, but there are many, many companies that can provide you with cheap M2M data SIM cards, either prepaid or with a subscription plan. 


### Libraries used

   - [Adafruit_SHT31](https://github.com/adafruit/Adafruit_SHT31), to read the SHT31 temperature and humidity sensor
   - [LPS](https://github.com/pololu/lps-arduino), to read the LPS25H pressure sensor
   - [DS18B20](https://github.com/matmunk/DS18B20), to read the DS18B20 soil temperature sensor
   - [Wire](https://www.arduino.cc/en/reference/wire), for I2C-communication (required for the SHT31 and LPS25H)
   - [OneWire](https://www.arduinolibraries.info/libraries/one-wire), for 1-wire communication (required for the DS18B20)
   - [RTClib](https://github.com/adafruit/RTClib), to communicate with the Real Time Clock
   - [NeoSWSerial](https://github.com/SlashDevin/NeoSWSerial), to communicate with the modem over a software serial connection. In principle, it would be better to use the hardware serial UART, but unfortunately the Arduino Uno only has one UART, which is also used for the USB serial monitor. So if you want to be able to debug your code on an Uno while using the modem, you need to use software serial for the modem.
   - Optionally, you can use [Fat16](https://github.com/greiman/Fat16) or [SD](https://www.arduino.cc/en/Reference/SD), to write CSV-data to an SD-card. Currently this does not work on the Arduino Uno unfortunatey, as its microcontroller has insufficient on-board memory.

All libraries can be installed through the [Library Manager](https://www.arduino.cc/en/Guide/Libraries) in the Arduino IDE.

### Connections

The modem shield uses the following Arduino pins:
```
D0  RX modem serial
D1  TX modem serial
D9  modem on/off
```
The data logger shield uses the following Arduino pins:
```
D10 SD Card chip select
D11 SPI MOSI
D12 SPI MISO
D13 SPI clock
```

The I2C-bus, needed for the temperature/humidity and pressure sensors, use the following Arduino pins:
```
A4  I2C SDA (data)
A5  I2C SCL (clock)
```

The remaining sensors use the following Arduino pins:
```
A0  wind vane, with 10 kOhm resistor to Vcc (5V)
A1  current sensor
D2  anemometer (wind speed meter)
D3  rain gauge
D4  DS18B20 data line (soil temperature sensor, yellow wire), with 4.7 kOhm pull-up resistor to Vcc (5V or 3.3V, red wire)
```

### Configuration

You'll need to enter the APN (access point name) of your SIM-card in the Arduino sketch, in the string after after `#define MODEM_START_TASK`. You will also need to enter the address and the port of the server that you are using to run the Python script, in the string after `#define MODEM_START_IP`.

In the server script, you may need to configure the port it listens on (the default is 9000). You will also need to sign up for the [Citizen Weather Observer Program (CWOP)](http://www.wxqa.com/SIGN-UP.html) and [Weather Underground](https://www.wunderground.com/signup) and enter ID/login/location details in the Python script, or disable reporting to these services.
