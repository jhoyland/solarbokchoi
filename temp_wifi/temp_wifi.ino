//#include "esp_wifi.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32Time.h>
#include "time.h"
#include "DHT.h"

#define DHTPIN 4
#define DHTTYPE DHT22

#define ERR_START_TIME_FAIL 1
#define ERR_END_TIME_FAIL 2
#define ERR_NTP_TIME_FAIL 4

//String GOOGLE_SCRIPT_ID = "AKfycbztvlntgDxZuQh7W8NpnUWmGiS_PNOF7-nANlxzGVXTY_L_d5MvZ-mtenJhXlG7xERQLg";
String GOOGLE_SCRIPT_ID = "AKfycbzZ2ltvXRhogyvpZyTQ9ES1gIfgcJ_QT5aV543W0EV71dw75oHIc0ccZF_7GkeB5xShcw";
//String GOOGLE_SCRIPT_ID = "1m51Jj15uNM3Tswu1es1RKZCMvsZ3WY0DNlnnyU98AGQEAzvWkT4xMlLK";

const char * ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -8 * 3600;
const int daylightOffset_sec = 3600;

ESP32Time rtc;

const char* ssid = "Pixel_4922";
const char* password = "jameshotspot";

//const char* ssid = "SillyPeaHead";
//const char* password = "EarlierTudors1485";

#define STATION_ID 1

#define SENSOR_READINGS 4
#define AVERAGING_WINDOW 4

#define SENSORS 2 // Temperature and humidity

#define DHTPIN 4
#define DHTTYPE DHT22

#define LONG_WAIT 6
#define SHORT_WAIT 2
#define _SECONDS 1000000
#define _MINUTES 60000000

typedef struct {
  char start_time[50];
  char end_time[50];
  float data[SENSORS];
  float data_std_dev[SENSORS];
  uint8_t err_code;
} sensorReading;

float sensor_values[SENSORS];
RTC_DATA_ATTR float sensor_value_accumulate[SENSORS];
RTC_DATA_ATTR float sensor_value_square_accumulate[SENSORS];

// Stored in RTC memory to persist during sleep mode
RTC_DATA_ATTR sensorReading current_reading;
RTC_DATA_ATTR sensorReading transmit_buffer[SENSOR_READINGS];
//RTC_DATA_ATTR struct tm schedule[SENSOR_READINGS];
RTC_DATA_ATTR uint8_t readingCounter = 0;
RTC_DATA_ATTR uint8_t averagingCounter = 0;
//RTC_DATA_ATTR struct tm last_awake;

void takeReading()
{
  // Take the current reading and store

  Serial.println("Take reading");

  DHT dht(DHTPIN,DHTTYPE);

  dht.begin();

  sensor_values[0] = dht.readTemperature();
  sensor_values[1] = dht.readHumidity();

  Serial.println("Accumulating");

  for(int i = 0; i<SENSORS; i++)
  {
    sensor_value_accumulate[i] += sensor_values[i];
    sensor_value_square_accumulate[i] += sensor_values[i] * sensor_values[i];
  }

  Serial.println("Done taking reading");
}


void setupReading()
{
  Serial.println("Seting up for new reading");

  sprintf(transmit_buffer[readingCounter].start_time,(rtc.getTime("%F.%H.%M.%S")).c_str());

  Serial.println("Clearing accumulators");

  for(int i = 0; i<SENSORS; i++)
  {
    sensor_value_accumulate[i] = 0;
    sensor_value_square_accumulate[i] =0;
  }

  transmit_buffer[readingCounter].err_code = 0;
  
}

void finalizeReading()
{
  float mean;
  float mean_of_squares;

  Serial.println("Finalizing reading");

  for(int i = 0; i<SENSORS; i++)
  {
    mean = sensor_value_accumulate[i] / SENSOR_READINGS;
    mean_of_squares = sensor_value_square_accumulate[i] / SENSOR_READINGS;
    transmit_buffer[readingCounter].data[i] = mean;
    transmit_buffer[readingCounter].data_std_dev[i] = (mean_of_squares - mean*mean) / (SENSOR_READINGS - 1.0);
  }

  sprintf(transmit_buffer[readingCounter].end_time,(rtc.getTime("%F.%H.%M.%S")).c_str());
}

void connectToWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

void transmitData()
{
  Serial.println("Preparing to transmit");
  HTTPClient http;
  String scriptURL = "https://script.google.com/macros/s/"+GOOGLE_SCRIPT_ID+"/exec?";
  // Transmit
  String urlStation = "sensorid=" + String(STATION_ID);
  String urlFinal;
  for(int i=0;i<SENSOR_READINGS;i++)
  {
    Serial.println("Preparing reading: " + String(i));
    urlFinal = scriptURL + urlStation;
    urlFinal += "&startdate=" + String(transmit_buffer[i].start_time);
    urlFinal += "&enddate=" + String(transmit_buffer[i].end_time);
    urlFinal += "&temp=" + String(transmit_buffer[i].data[0]);
    urlFinal += "&temperr=" + String(transmit_buffer[i].data_std_dev[0]);
    urlFinal += "&humid=" + String(transmit_buffer[i].data[1]);
    urlFinal += "&humiderr=" + String(transmit_buffer[i].data_std_dev[1]);
    urlFinal += "&errorcode=" + String(transmit_buffer[i].err_code);

    Serial.println(urlFinal);

    http.begin(urlFinal.c_str());
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int httpCode = http.GET(); 
    Serial.print("HTTP Status Code: ");
    Serial.println(httpCode);
      //---------------------------------------------------------------------
    //getting response from google sheet
    String payload;
    if (httpCode > 0) {
      payload = http.getString();
        Serial.println("Payload: "+payload);    
    }
      //---------------------------------------------------------------------
    http.end();
    delay(1000);
  }
}

void synchronizeTime()
{
  Serial.println("Synchronizing time");
  struct tm timeinfo;
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
  }
  else
  {
    Serial.println(&timeinfo, "Got time: %A, %B %d %Y %H:%M:%S");
    rtc.setTimeStruct(timeinfo);
  }
}

void getSchedule()
{
  // TODO Read a different google sheet to get time instructions. This will allow updating throughout the experiment.
}

void disconnectWiFi()
{
  Serial.println("Disconnecting");
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
}

void setAlarm()
{
  // TO DO: calculate time from downloaded schedule
}

void goToSleep()
{
  Serial.println("Going to sleep");
  esp_sleep_enable_timer_wakeup(LONG_WAIT * _SECONDS);
  esp_deep_sleep_start();
}

void takeNap()
{
  Serial.println("Taking a nap");
  esp_sleep_enable_timer_wakeup(SHORT_WAIT * _SECONDS);
  esp_deep_sleep_start();
}

void setup() {

  // Wake up 
  Serial.begin(115200);

  delay(1000);

  Serial.println("Waking up");
  Serial.println("avaraging counter = " + String(averagingCounter));
  Serial.println("reading counter = " + String(readingCounter));

  // If power has been off the RTC will default to an early year so we do this little check and synchronize of necessary.
  // RTC does not loose time if it has been sleep mode
  
  int year = rtc.getYear();

  Serial.println("Year:" +String(year));

  /*if(year < 2023)
  {
    Serial.println("RTC default time. Will synchronize with NTP");
    connectToWiFi();
    synchronizeTime();
    disconnectWiFi();
  }*/

  if(averagingCounter==0)
  {
    setupReading();
  }
  
 takeReading();

  averagingCounter++;
 // Serial.println("Now it's avaraging counter = " + String(averagingCounter));

  if(averagingCounter == AVERAGING_WINDOW)
  {
    finalizeReading();
    averagingCounter = 0;

    readingCounter++;


    if(readingCounter == SENSOR_READINGS)
    {
      readingCounter = 0;

      connectToWiFi();
      transmitData();
      synchronizeTime();
      getSchedule();
      disconnectWiFi();
    }

 //   setAlarm();
    goToSleep();  

  }

  takeNap();
  
}

void loop() {}














