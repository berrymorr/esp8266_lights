#include <Arduino.h>
#include <Arduino_JSON.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266WiFi.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"


//=====================settings======================
#define FIRMWARE_VERSION "0.1"
#undef SERIAL                  //use Serial.print for debug or not (defined in Arduino.h)


//WiFi settings
const char *ssid = "SSID Name";
const char *password = "SSID_password";

//MQTT settings
#define SSL                     //use https or http
#define MQTT_USER               "mqtt_user"
#define MQTT_PASSWORD           "mqtt_pass"
#define STATIC_MESSAGE_BUF_LEN  256
#define MQTT_SERVER             "mqtt_domain_or_ip"

#ifdef SSL
#define MQTT_SERVERPORT         8883
//openssl x509 -in mqttserver.crt -sha1 -noout -fingerprint
#define SHA1_Fingerprint        "DE:A8:B3:D1:B5:F1:F7:34:F4:84:C7:E4:6C:4E:C5:AC:E9:F9:8D:0A"
#else
#define MQTT_SERVERPORT         1883
#endif
#define TEMP_TOPIC              "room01/bed/temp"
#define LIGHT_TOPIC               "room01/bed/light"
#define DEVICE_IDENTIFICATION_STRING "NODEMCU_with_BED_LIGHTS" //use alphanumeric only! this const indentifies firmware filename @OTA too

//pin definitions
#define WIFI_LED LED_BUILTIN
#define MQTT_LED LED_BUILTIN_AUX
#define ONE_WIRE_BUS D5
#define R_pin D6
#define G_pin D7
#define B_pin D8
//===============end=of=settings====================


#ifdef SSL
WiFiClientSecure client;  //with SSL/TLS support
#else
WiFiClient client;        //without SSL/TLS support
#endif

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, MQTT_SERVER, MQTT_SERVERPORT, MQTT_USER, MQTT_PASSWORD);
// Setup feeds to publish and subscribe
Adafruit_MQTT_Publish mqtt_pub = Adafruit_MQTT_Publish(&mqtt, TEMP_TOPIC);
Adafruit_MQTT_Subscribe mqtt_sub = Adafruit_MQTT_Subscribe(&mqtt, LIGHT_TOPIC, 1);


//1-Wire global objects setup
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// JSON message creation part
JSONVar message_object;         // to store message parameters
String json_msg_string;         // to stringify JSON message object
char message_string_buf[STATIC_MESSAGE_BUF_LEN];   // to use in mqtt publish function

DeviceAddress ds1820addrs[20];



void MQTT_connect(Adafruit_MQTT_Client mqtt) {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected())
    return;

  #ifdef SERIAL
  Serial.print("Connecting to MQTT... ");
  #endif

  uint8_t retries = 100;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
    #ifdef SERIAL
    Serial.println(mqtt.connectErrorString(ret));
    #endif
    mqtt.disconnect();
    #ifdef SERIAL
    Serial.print("Retrying... ");
    #endif
    delay(500);  // wait .5 seconds
    retries--;
    if (retries == 0) ESP.restart();
  }

  #ifdef SERIAL
  Serial.println("MQTT Connected!");
  #endif
}


//  1-WIRE ROUTINES
#ifdef SERIAL
void printAddress(DeviceAddress deviceAddress)
{ 
  for (uint8_t i = 0; i < 8; i++)
  {
    Serial.print("0x");
    if (deviceAddress[i] < 0x10) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
    if (i < 7) Serial.print(", ");
  }
  Serial.println("");
}
#endif

void enum1wire() {
  int deviceCount = 0;

  sensors.begin();

  #ifdef SERIAL
  Serial.println("Searching 1-wire devices...");
  #endif
  deviceCount = sensors.getDeviceCount();
  if (deviceCount>20) deviceCount = 20;
  #ifdef SERIAL
  Serial.print("Found "+String(deviceCount)+" devices.\n");
  Serial.println("Addresses:");
  #endif

  sensors.requestTemperatures();
  //sensors.setWaitForConversion(true);

  for (int i = 0;  i < deviceCount;  i++)
  {
    sensors.getAddress(ds1820addrs[i], i);
    #ifdef SERIAL
    Serial.print("Sensor ");
    Serial.print(i+1);
    Serial.print(" : ");
    printAddress(ds1820addrs[i]);
    float tempC = sensors.getTempC(ds1820addrs[i]);
    Serial.println("\ttemperature is "+String(tempC)+"°C");
    #endif
  }

}



bool connectToWifi() {
  byte timeout = 100;

  #ifdef SERIAL
  Serial.println("\n\n");
  Serial.print("Connecting to "+String(ssid)+" ");
  #endif

  WiFi.hostname("ESP8266");
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  for (int i = 0; i < timeout; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      #ifdef SERIAL
      Serial.println("\nConnected to WiFi '"+WiFi.SSID()+"' @channel #"+String(WiFi.channel())+" RSSI="+String(WiFi.RSSI())+" dBm");
      Serial.println("IP:"+WiFi.localIP().toString()+" MASK:"+WiFi.subnetMask().toString()+
          " DNS:"+WiFi.dnsIP().toString()+" GW:"+WiFi.gatewayIP().toString());
      #endif
      analogWrite(LED_BUILTIN, 0);
      return true;
    }
    delay(100);
    #ifdef SERIAL
    Serial.print(".");
    #endif
  }

  #ifdef SERIAL
  Serial.println("\nFailed to connect to WiFi");
  Serial.println("Check network status and access data");
  Serial.println("Push RST to try again");
  #endif
  return false;
}


void setup() {
  //built-in leds
  pinMode(WIFI_LED, OUTPUT);
  analogWrite(WIFI_LED, 1024); //switch the led off
  pinMode(MQTT_LED, OUTPUT);
  analogWrite(MQTT_LED, 1024); //switch the led off


  //uart
  #ifdef SERIAL
  Serial.begin(115200);
  delay(100);
  Serial.println("tty initialized!");
  #endif

  //1-wire
  enum1wire();

  //wi-fi

  WiFi.mode(WIFI_STA);

  if (!connectToWifi()) {
    #ifdef SERIAL
    Serial.println("Can't connect to WiFi, waiting 3 s before restart");
    #endif
    delay(3000);
    ESP.restart();
  }

  //check 4 updates
  analogWrite(R_pin,1024);
  String firmware_filename = String("/") + String(DEVICE_IDENTIFICATION_STRING) + String(".bin");
  #ifdef SERIAL
  Serial.print(String("checking for new firmware at http://") + String(MQTT_SERVER) + String(":") +
      String(MQTT_SERVERPORT+1) + firmware_filename);
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  t_httpUpdate_return ret = ESPhttpUpdate.update(MQTT_SERVER, MQTT_SERVERPORT+1, firmware_filename, String(FIRMWARE_VERSION));
  #pragma GCC diagnostic pop
  switch(ret) {
    case HTTP_UPDATE_FAILED:
        Serial.println(" [FAILED]");
        break;
    case HTTP_UPDATE_NO_UPDATES:
        Serial.println(" [NO UPDATES]");
        break;
    default:
        break;
  }
  #else
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  ESPhttpUpdate.update(MQTT_SERVER, MQTT_SERVERPORT+1, firmware_filename, String(FIRMWARE_VERSION));
  #pragma GCC diagnostic pop
  #endif
  analogWrite(R_pin,0);

  //time
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  #ifdef SERIAL
  Serial.print("Syncing time");
  #endif
  int i = 0;
  //while (time(NULL) < 1000000000ul && i<50) {
  //while (time(NULL) < 1000000ULL && i<50) {
  while (time(NULL) < 1000000 && i<50) {
    if (i % 2 == 0) {
      analogWrite(WIFI_LED, 0);
    }
    else {
      analogWrite(WIFI_LED, 1024);
    }
    #ifdef SERIAL
    Serial.print(".");
    #endif
    delay(1000);
    i++;
  }
  if (i>=50) ESP.restart();



  #ifdef SSL
  client.setFingerprint(SHA1_Fingerprint);
  #endif
  message_object["ident"] = DEVICE_IDENTIFICATION_STRING;

  //mqtt
  mqtt.subscribe(&mqtt_sub);

  //connected 2 wifi, updated (if was able) and ntped
  analogWrite(WIFI_LED, 0);
}

void loop() {


  MQTT_connect(mqtt);
  time_t now = time(NULL);
  struct tm * tms = localtime(&now);
  int h = tms->tm_hour;
  String dt = ctime(&now);
  dt.remove(dt.length()-1);

  #ifdef SSL
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  #endif
  if(! (mqtt.ping()
    #ifdef SSL
    & client.verify(SHA1_Fingerprint, MQTT_SERVER)
    #endif

  ) ) {
    #ifdef SERIAL
    Serial.println(dt+" Can't ping mqtt, disconnecting...");
    #endif
    mqtt.disconnect();
    analogWrite(MQTT_LED, 1024);
    return;
  }
  #ifdef SSL
  #pragma GCC diagnostic pop
  #endif

  //setting LEDs
  if (h>9 && h<21) { //day
    analogWrite(WIFI_LED, 0);
    analogWrite(MQTT_LED, 0);
    }
  else { //night
    analogWrite(WIFI_LED, 1010);
    analogWrite(MQTT_LED, 1010);

  }

  //getting and publishing temperatures
  sensors.requestTemperatures();
  float dstemp = sensors.getTempC(ds1820addrs[0]);
  if (dstemp>-127) {  //don't publish '-127' value which means that conversion isn't ready
    message_object["temperature"] = dstemp;
    json_msg_string = JSON.stringify(message_object);
    json_msg_string.toCharArray(message_string_buf, json_msg_string.length() + 1);
    #ifdef SERIAL
    Serial.print(dt+" Publishing message to broker: ");
    Serial.println(message_string_buf);
    #endif
    mqtt_pub.publish(message_string_buf);
    // cleanup memory used
    memset(message_string_buf, 0, STATIC_MESSAGE_BUF_LEN);
  }

  //getting subscribe messages
  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(2000))) {
    // Check if got subscription
    if (subscription == &mqtt_sub) {
      #ifdef SERIAL
      Serial.print(F("light: "));
      Serial.println((char *)mqtt_sub.lastread);
      #endif
      uint32_t ledval = atoi((char *)mqtt_sub.lastread);  // convert to a number
      uint8_t r_val = (uint8_t)ledval;
      uint8_t g_val = (uint8_t)(ledval >> 8);
      uint8_t b_val = (uint8_t)(ledval >> 16);
      #ifdef SERIAL
      Serial.printf("red: %d, green: %d, blue: %d \r\n",r_val,g_val,b_val);
      #endif
      analogWrite(R_pin,(uint16_t)(r_val << 2));
      analogWrite(G_pin,(uint16_t)(g_val << 2));
      analogWrite(B_pin,(uint16_t)(b_val << 2));
    }
  }

}
