#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <FS.h>

#include <arduino-timer.h>

#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME280 bme;
bool sensorReady = false;

bool fsReady = false;

const char* ap_ssid = "AtmoLogger";      //AP SSID
IPAddress ip(192, 168, 1, 100);
IPAddress subnet(255, 255, 255, 0);
const byte DNS_PORT = 53;
DNSServer dnsServer;
ESP8266WebServer server(80);

auto timer = timer_create_default();

struct SensorValues
{
    float mTemperature = 0.f;
    float mHumidity = 0.f;
    float mPressure = 0.f;
};

SensorValues getSensorValues()
{
    SensorValues values;
    values.mTemperature = bme.readTemperature();
    values.mHumidity = bme.readHumidity();
    values.mPressure = bme.readPressure() / 100.0f;
    return values;
}

template<unsigned MaxLogs, unsigned LogEvery>
class Logger
{
public:
    Logger() = default;

public:
    void logIfNeeded()
    {
        mWait++;
        if (mWait >= LogEvery)
        {
            mWait = 0;
            mLogs.at(mIndex) = getSensorValues();
            mIndex++;
            mIndex %= MaxLogs;
            if (mIndex == 0)
            {
                mHasCompletedOneCycle = true;
            }
        }
    }

    String getLogs()
    {
        String logs = "[";
        String timeLogs = "[";
        String tempLogs = "[";
        String humLogs = "[";
        String presLogs = "[";
        const unsigned upperBound = mHasCompletedOneCycle ? MaxLogs : mIndex;
        for (int i = 0; i < upperBound; i++)
        {
            timeLogs += i;
            
            int currentIndex = mIndex - upperBound + i;
            if (currentIndex < 0)
            {
                currentIndex += MaxLogs;
            }
            SensorValues& current = mLogs.at(currentIndex);
            
            tempLogs += current.mTemperature;
            humLogs += current.mHumidity;
            presLogs += current.mPressure;

            if (i < upperBound - 1)
            {
                timeLogs += ",";
                tempLogs += ",";
                humLogs += ",";
                presLogs += ",";
            }
        }
        timeLogs += "],";
        tempLogs += "]";
        humLogs += "],";
        presLogs += "],";
        logs += timeLogs;
        logs += humLogs;
        logs += presLogs;
        logs += tempLogs;
        logs += "]";
        return logs;
    }

private:
    std::array<SensorValues, MaxLogs> mLogs;
    unsigned mIndex = 0;
    unsigned mWait = LogEvery;
    bool mHasCompletedOneCycle = false;
};

Logger<12, 1>   mHourLogger;
Logger<24, 6>   mDayLogger;
Logger<28, 36>  mWeekLogger;
Logger<30, 144> mMonthLogger;

bool logIfNeeded(void* = 0)
{
    mHourLogger.logIfNeeded();
    mDayLogger.logIfNeeded();
    mWeekLogger.logIfNeeded();
    mMonthLogger.logIfNeeded();
    return true;
}

String toStringIp(IPAddress ip)
{
    String res = "";
    for (int i = 0; i < 3; i++) {
        res += String((ip >> (8 * i)) & 0xFF) + ".";
    }
    res += String(((ip >> 8 * 3)) & 0xFF);
    return res;
}

void captivePortal()
{
    server.sendHeader("Location", String("http://") + toStringIp(server.client().localIP()), true);
    server.send(302, "text/plain", "");
    server.client().stop();
}
 
bool handleUrl(String path)
{
    if (path.endsWith("/debug"))
    {
        String page = "";

        if (!fsReady)
        {
            page += "Filesystem error";
        }
        else if (!sensorReady)
        {
            page += "Sensor error";
        }
        else
        {
            page += "Temperature = ";
            page += bme.readTemperature();
            page += " °C<br/>";
            
            page += "Pressure = ";
            page += bme.readPressure() / 100.0F;
            page += " hPa<br/>";
            
            page += "Humidity = ";
            page += bme.readHumidity();
            page += " %<br/>";
        }
         
        server.send(200,"text/html",page.c_str());
        return true;
    }
    else if (path.endsWith("/data"))
    {
        String page = "";

        if (fsReady && sensorReady)
        {
            SensorValues values = getSensorValues();
            page += "{";
            page += "\"hour\":";
            page += mHourLogger.getLogs();
            page += ",\"day\":";
            page += mDayLogger.getLogs();
            page += ",\"week\":";
            page += mWeekLogger.getLogs();
            page += ",\"month\":";
            page += mMonthLogger.getLogs();
            page += ",\"temperature\":";
            page += values.mTemperature;
            page += ",\"humidity\":";
            page += values.mHumidity;
            page += ",\"pressure\":";
            page += values.mPressure;
            page += "}";
        }
         
        server.send(200,"application/json",page.c_str());
        return true;
    }
    else if (path.endsWith("/uPlot.iife.min.js"))
    {
        String page = "";

        if (fsReady && sensorReady)
        {
            if (File html = SPIFFS.open("/uPlot.iife.min.js", "r"))
            {
                page = html.readString();
                html.close();
            }
        }
         
        server.send(200,"application/javascript",page.c_str());
        return true;
    }
    else if (path.endsWith("/uPlot.min.css"))
    {
        String page = "";

        if (fsReady && sensorReady)
        {
            if (File html = SPIFFS.open("/uPlot.min.css", "r"))
            {
                page = html.readString();
                html.close();
            }
        }
         
        server.send(200,"text/css",page.c_str());
        return true;
    }
    else if (path.endsWith("/"))
    {
        String page = "";

        if (fsReady && sensorReady)
        {
            if (File html = SPIFFS.open("/index.html", "r"))
            {
                page = html.readString();
                html.close();
            }
        }
        else
        {
            page += "Error, please reboot.";
        }
         
        server.send(200,"text/html",page.c_str());
        return true;
    }
    return false;
}


void setup()
{
    Wire.pins(0, 2);
    Wire.begin();

    if(SPIFFS.begin())
    {
        fsReady = true;
    }
    
    unsigned status = bme.begin(0x76);
    if (status)
    {
        sensorReady = true;
    }
    
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(ip, ip, subnet);
    WiFi.softAP(ap_ssid);
    dnsServer.start(DNS_PORT, "*", ip);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
 
    server.onNotFound([]()
    {
        if (!handleUrl(server.uri()))
        {
            captivePortal();
        }
    });
    server.begin();

    if (sensorReady)
    {
        logIfNeeded();
        timer.every(600000, logIfNeeded);
    }
}


void loop()
{ 
    dnsServer.processNextRequest();
    server.handleClient();
    timer.tick();
}
