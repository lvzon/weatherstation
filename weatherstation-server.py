#!/usr/bin/python3 

# Open source weather station for ERC Altiplano, based on an Arduino Uno
# Server-side code
# (c)2018-2019, Levien van Zon (levien at gnuritas .org)
# Further information: 
# http://gnuritas.org/weatherstation.html
# https://github.com/lvzon/weatherstation

from datetime import datetime
import select

# Bind to localhost, port 9000
HOST, PORT = "", 9000

# CWOP station ID, password and location
cwop_station = 'BLA'
cwop_pass = '-1'
# Attention, format of the location is bit special. Although there is a dot, the values are in degrees, minutes and seconds!
cwop_position = '9999.99N/88888.88W_'

# Weather Underground user and password
wu_station = 'BLA'
wu_password = 'blabla'

# Variables sent by the Arduino weather station

vars=['start','duration','T_mean','T_min','T_max','RH_mean','RH_min','RH_max','rain_mm','rain_mm_sum','wind_mean','wind_min','wind_max','winddir_mean','winddir_max','I_mean','runtime_s','pressure_mbar','T_soil']

# Open your data output file

csv = open('weatherdata.csv', 'a')
#print('servertime,start,duration,T_mean,T_min,T_max,RH_mean,RH_min,RH_max,rain_mm,rain_mm_sum,wind_mean,wind_min,wind_max,winddir_mean,winddir_max,I_mean', file=csv)

# Open additional files for dumping Weather Underground URLs and invalid data 

urls = open('wu_urls.txt', 'a')
invalid = open('invalid_data.txt', 'a')

def C_to_F (celsius):
    return round(9.0 / 5.0 * celsius + 32, 2)

def kmh_to_mph (kmh):
    return round(kmh / 1.60934, 2)

def mm_to_inch (mm):
    return round(mm / 25.4, 2)

dir_to_degrees = {'N': 0,'NE': 45,'E': 90,'SE': 135,'S': 180,'SW': 225,'W': 270,'NW': 315, 'NA': None}

import math

def get_dew_point_c(t_air_c, rel_humidity):
    """Compute the dew point in degrees Celsius
    :param t_air_c: current ambient temperature in degrees Celsius
    :type t_air_c: float
    :param rel_humidity: relative humidity in %
    :type rel_humidity: float
    :return: the dew point in degrees Celsius
    :rtype: float
    """
    # Source: https://gist.github.com/sourceperl/45587ea99ff123745428
    A = 17.27
    B = 237.7
    alpha = ((A * t_air_c) / (B + t_air_c)) + math.log(rel_humidity/100.0)
    return (B * alpha) / (A - alpha)


import urllib.request

def generate_wureq (valdict):
    
    ts = valdict['servertime']
    dateutc = datetime.utcfromtimestamp(ts).strftime("%Y-%m-%d+%H%%3A%M%%3A%S") # [YYYY-MM-DD HH:MM:SS (mysql format)] In Universal Coordinated Time (UTC) Not local time
    tempf = str(C_to_F(valdict['T_mean'])) # [F outdoor temperature] 
    humidity = str(valdict['RH_mean']) # [% outdoor humidity 0-100%]
    dailyrainin = str(mm_to_inch(valdict['rain_mm_sum'])) # [rain inches so far today in local time]
    windspdmph_avg2m = str(kmh_to_mph(valdict['wind_mean'])) # [mph 2 minute average wind speed mph]
    winddir_avg2m = str(valdict['winddir_mean']) # [0-360 2 minute average wind direction]
    windgustmph_10m = str(kmh_to_mph(valdict['wind_max'])) # [mph past 10 minutes wind gust mph ]
    
    wind_max_deg = dir_to_degrees[valdict['winddir_max']]
    windgustdir_10m = str(wind_max_deg) # [0-360 past 10 minutes wind gust direction]

    dewptf = str(C_to_F(get_dew_point_c(valdict['T_mean'], valdict['RH_mean'])))

    paramstr = 'ID=' + wu_station + '&PASSWORD=' + wu_password + '&dateutc=' + dateutc + '&tempf=' + tempf + '&humidity=' + humidity + '&dewptf=' + dewptf +     '&dailyrainin=' + dailyrainin + '&windspeedmph=' + windspdmph_avg2m + '&winddir=' + winddir_avg2m +  '&windgustmph=' + windgustmph_10m
    
    if wind_max_deg is not None:
        paramstr = paramstr + '&windgustdir=' + windgustdir_10m
    
    return 'https://weatherstation.wunderground.com/weatherstation/updateweatherstation.php?' + paramstr


import sys, os, time
from datetime import datetime, timedelta
from socket import *

cwop_host = 'cwop.aprs.net'
cwop_port = 14580
cwop_address = cwop_station + '>APRS,TCPIP*:'

def make_aprs_wx(wind_dir=None, wind_speed=None, wind_gust=None, temperature=None, rain_last_hr=None, rain_last_24_hrs=None, rain_since_midnight=None, humidity=None, pressure=None):
    # Assemble the weather data of the APRS packet
    def str_or_dots(number, length):
        # If parameter is None, fill with dots, otherwise pad with zero
        if number is None:
            return '.'*length
        else:
            format_type = {
                'int': 'd',
                'float': '.0f',
            }[type(number).__name__]
            return ''.join(('%0',str(length),format_type)) % number
    return '%s/%sg%st%sr%sp%sP%sh%sb%s' % (
        str_or_dots(wind_dir, 3),
        str_or_dots(wind_speed, 3),
        str_or_dots(wind_gust, 3),
        str_or_dots(temperature, 3),
        str_or_dots(rain_last_hr, 3),
        str_or_dots(rain_last_24_hrs, 3),
        str_or_dots(rain_since_midnight, 3),
        str_or_dots(humidity, 2),
        str_or_dots(pressure, 5),
    )


def cwop_send_packet(valdict):

    rh = valdict['RH_mean']
    if rh > 99.9:
        rh = 99.9

    ts = valdict['servertime']
    # Attention, temperature in Fahrenheit!
    fahrenheit = C_to_F(valdict['T_mean'])
    humidity = rh
    # Attention, barometric pressure in tenths of millibars/tenths of hPascal!
    #pressure = press[2][0][1] * 10
    pressure = valdict['pressure_mbar'] * 10
 
    # If you have wind and rain data, get it here. Be aware that values are required in mph and in hundredths of an inch!
    wind_degrees = dir_to_degrees[valdict['winddir_max']]
    wind_mph = kmh_to_mph(valdict['wind_mean'])
    wind_gust_mph = kmh_to_mph(valdict['wind_max'])
    precip_today_in = mm_to_inch(valdict['rain_mm_sum'])
 
    # Prepare the data, which will be sent
    wx_data = make_aprs_wx(wind_degrees, wind_mph, wind_gust_mph, fahrenheit, None, None, precip_today_in * 100.0, humidity, pressure)
    # Use UTC
    utc_datetime = datetime.utcfromtimestamp(ts)
    # Create socket and connect to server
    sSock = socket(AF_INET, SOCK_STREAM)
    sSock.connect((cwop_host, cwop_port))
    # Log on
    sSock.send(b'user ' + cwop_station + ' pass ' + cwop_pass + ' vers Python\n')
    # Send packet
    packetstr = cwop_address + '@' + utc_datetime.strftime("%d%H%M") + 'z' + cwop_position + wx_data + 'Arduino\n'
    sSock.send(packetstr.encode('ascii'))
    # Close socket, must be closed to avoid buffer overflow
    sSock.shutdown(0)
    sSock.close()
 

def decode_line(line):
    
    vals = line.rstrip().split(',')
    
    if (len(vars) == len(vals)):
        
        valdict = dict(zip(vars, vals))
        for key, value in valdict.items():
            try:
                num = float(value)
                valdict[key] = num
            except ValueError:
                pass
        
        ts = valdict['start']
        
        try:
        
            valdict['servertime'] = int(datetime.now().timestamp())
            #valdict['servertime'] = int(valdict['start'] + valdict['duration'])
        
            print(datetime.utcfromtimestamp(ts))
            print(valdict)
        
            url = generate_wureq(valdict)
            print(url, file=urls)
            contents = urllib.request.urlopen(url).read()
            
            cwop_send_packet(valdict)
            
            return str(valdict['servertime']) + ',' + line
        
        except:
            pass
        
    print("invalid line")
    print(line, file=invalid)

    return None


def read_line(fd, timeout):
    
    inputs = [fd]
    
    chars = []
    line = ''
    failed = 0
        
    readable, writable, exceptional = select.select(inputs, [], inputs, timeout)
    while readable and failed < timeout and len(chars) and chars[-1] != '\n':
            
        if not (readable or writable or exceptional):
            print('1-second time-out')
            failed += 1
            continue 
            
        for s in readable:
            char = s.recv(1)
            chars.append(char)
            line = ''.join(chars)
            print('Received char', char, 'and appended to line:', line)
                
        readable, writable, exceptional = select.select(inputs, [], inputs, 1)

    if len(chars) and chars[-1] == '\n':
        print('Line received:', line)
        return line
    else:
        print('No line received:', line)
        print(line, file=invalid)
        return None
    
    
    
import socketserver

class MyTCPHandler(socketserver.StreamRequestHandler):

    def handle(self):
        # self.rfile is a file-like object created by the handler;
        # we can now use e.g. readline() instead of raw recv() calls
        #try:
        #line = self.rfile.readline()
        
        inputs = [self.rfile]
        timeout = 60
        readable, writable, exceptional = select.select(inputs, [], inputs, timeout)
        
        if not (readable or writable or exceptional):
            print('Timed out while waiting for data on socket')
            return 
        
        #line = read_line(self.rfile, 30)
        line = self.rfile.readline()
                    
        while line:
        
            print("Data received from {}:".format(self.client_address[0]))
            try:
                line = line.decode('utf_8')
                csvline = decode_line(line)
                if csvline:
                    csv.write(csvline)
            except:
                print('invalid data received')
                print(line, file=invalid)

            #line = read_line(self.rfile, 30)
            line = self.rfile.readline()

if __name__ == "__main__":

    # Create the server, binding to localhost
    server = socketserver.TCPServer((HOST, PORT), MyTCPHandler)

    # Activate the server; this will keep running until you
    # interrupt the program with Ctrl-C
    server.serve_forever()


csv.close()

