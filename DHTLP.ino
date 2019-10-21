// DHT Temperature & Humidity Sensor
// Unified Sensor Library Example
// Written by Tony DiCola for Adafruit Industries
// Released under an MIT license.

// REQUIRES the following Arduino libraries:
// - DHT Sensor Library: https://github.com/adafruit/DHT-sensor-library
// - Adafruit Unified Sensor Lib: https://github.com/adafruit/Adafruit_Sensor

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

// FOTA

const char* fwUrlBase = "http://172.24.0.253/fota/";
const int   FW_VERSION = 12;

// IP STACK
IPAddress staticIP(172, 24, 0, 232); //ESP static ip
IPAddress gateway(172, 24, 0, 1);   //IP Address of your WiFi Router (Gateway)
IPAddress subnet(255, 255, 255, 0);  //Subnet mask
IPAddress dns(8, 8, 8, 8);  //DNS

#define DHTPIN 4     // Digital pin connected to the DHT sensor (pin D2 is 4)
// Feather HUZZAH ESP8266 note: use pins 3, 4, 5, 12, 13 or 14 --
// Pin 15 can work but DHT must be disconnected during program upload.

// Uncomment the type of sensor in use:
//#define DHTTYPE    DHT11     // DHT 11
#define DHTTYPE    DHT22     // DHT 22 (AM2302)
//#define DHTTYPE    DHT21     // DHT 21 (AM2301)

#define MQTT_CLIENT_ID        "bigroom"
#define MQTT_TOPIC_TEMP_HUM   "home/" MQTT_CLIENT_ID
#define MQTT_TOPIC_STATE      "home/" MQTT_CLIENT_ID "/status"
#define MQTT_PUBLISH_DELAY 60000

//#define _TRACE  1

const char *WIFI_SSID = "TADI2";
const char *WIFI_PASSWORD = "Pippo731@";

const char *MQTT_SERVER = "172.24.0.253";
const char *MQTT_USER = ""; // NULL for no authentication
const char *MQTT_PASSWORD = ""; // NULL for no authentication


// See guide for details on sensor wiring and usage:
//   https://learn.adafruit.com/dht/overview

WiFiClient espClient;
PubSubClient mqttClient(espClient);

DHT_Unified dht(DHTPIN, DHTTYPE);

float temperature;
float humidity;

void setup() {
  Serial.begin(115200);
  
  // Initialize device.
  dht.begin();
  delay(2500);
  // Get temperature event and print its value.
  sensors_event_t event;
  dht.temperature().getEvent(&event);
  temperature = event.temperature;
  uint32_t currTempTenths = temperature*10;
  
  if (isnan(temperature)) {
    Serial.println(F("Error reading temperature!"));
    temperature = -50;
  }
  dht.humidity().getEvent(&event);
  humidity = event.relative_humidity;
  if (isnan(humidity)) {
    Serial.println(F("Error reading humidity!"));
    humidity = -50;
  }

#ifdef _TRACE
  Serial.println("");
  Serial.print("Temperature ");
  Serial.println(temperature);
#endif
  uint32_t prevTempTenths;
  ESP.rtcUserMemoryRead(0, &prevTempTenths, sizeof(prevTempTenths));
  uint32_t counter;
  ESP.rtcUserMemoryRead(4, &counter, sizeof(counter));
  counter++;
  ESP.rtcUserMemoryWrite(4, &counter, sizeof(counter));

  if ((abs(prevTempTenths - currTempTenths) > 5) || ((counter % 2) == 0))
  {
    ESP.rtcUserMemoryWrite(0, &currTempTenths, sizeof(currTempTenths));
  
    setupWifi();

    checkForUpdates();

    mqttClient.setServer(MQTT_SERVER, 1883);

    connectAndSend();
  }

  ESP.deepSleep(30e6, WAKE_RF_DEFAULT);
}


void connectAndSend() {
  if (!mqttClient.connected()) {
    mqttReconnect();
  }
    mqttClient.loop();

    mqttPublish(MQTT_TOPIC_TEMP_HUM, temperature, humidity);
    WiFi.disconnect();


}

void loop() {

    
}

void setupWifi() {

#ifdef _TRACE
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);
#endif

  WiFi.config(staticIP, subnet, gateway);
  WiFi.mode(WIFI_STA);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tries = 30;
  while ((WiFi.status() != WL_CONNECTED) && (tries-- > 0)) {
    delay(500);
#ifdef _TRACE
    Serial.print(".");
#endif
  }

#ifdef _TRACE
  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
#endif
}

void mqttReconnect() {
  int tries = 5;
  while (!mqttClient.connected() && (tries-- > 0)) {
#ifdef _TRACE
    Serial.print("Attempting MQTT connection...");
#endif

    // Attempt to connect
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
#ifdef _TRACE
      Serial.println("connected");
      // Once connected, publish an announcement...
      // mqttClient.publish(MQTT_TOPIC_STATE, "connected", true);
#endif
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      delay(3000);
    }
  }
}

void mqttPublish(char *topic, float temperature, float humidity) {
  
  String msg = String("{ \"temperature\": ")  + temperature + String(" , \"humidity\": ") + humidity + String(" , \"fw\": ") + FW_VERSION + String(" }");
#ifdef _TRACE
  Serial.println(msg);
#endif
  mqttClient.publish(topic, msg.c_str() , true);
  mqttClient.disconnect();
}

void checkForUpdates() {
  String fwURL = String( fwUrlBase );
  String fwVersionURL = fwURL;
  fwVersionURL.concat( ".version" );

  Serial.println( "Checking for firmware updates." );
  Serial.print( "Firmware version URL: " );
  Serial.println( fwVersionURL );

  HTTPClient httpClient;
  httpClient.begin( fwVersionURL );
  int httpCode = httpClient.GET();
  if( httpCode == 200 ) {
    String newFWVersion = httpClient.getString();

#ifdef _TRACE
    Serial.print( "Current firmware version: " );
    Serial.println( FW_VERSION );
    Serial.print( "Available firmware version: " );
    Serial.println( newFWVersion );
#endif

    int newVersion = newFWVersion.toInt();

    if( newVersion > FW_VERSION ) {

      String fwImageURL = fwURL;
      fwImageURL.concat(WiFi.macAddress());
      fwImageURL.concat("_fw.bin");
      //ESPhttpUpdate.setLedPin(LED_BUILTIN, LOW);
#ifdef _TRACE
      Serial.print( "Preparing to update " );
      Serial.println(fwImageURL);
#endif
      t_httpUpdate_return ret = ESPhttpUpdate.update( fwImageURL );

      switch(ret) {
        case HTTP_UPDATE_FAILED:
          Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
          break;

        case HTTP_UPDATE_NO_UPDATES:
#ifdef _TRACE
          Serial.println("HTTP_UPDATE_NO_UPDATES");
#endif
          break;
      }
    }
    else {
#ifdef _TRACE
      Serial.println( "Already on latest version" );
#endif
    }
  }
  else {
    Serial.print( "Firmware version check failed, got HTTP response code " );
    Serial.println( httpCode );
  }
  httpClient.end();
}
