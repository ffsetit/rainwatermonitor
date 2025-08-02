#include <PubSubClient.h>
#include <WiFiNINA.h>
#include "arduino_secrets.h"

char ssid[] = SECRET_SSID;    // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)

WiFiClient wifiClient;
PubSubClient  mqttClient(wifiClient);

const char* mqttServer = "mqtt.int.barnes.bz";
const int   mqttPort   = 1883;
const char* clientId   = "rainwatertank";
const char* deviceIdentifier = "rainwatertank";

const char* discoveryTopic = "homeassistant/";
// MQTT topics
const char* statusTopic       = "rainwatertank/status";
const char* tankDepthTopic    = "rainwatertank/tank/depth";
const char* alarmTopic        = "rainwatertank/tank/alarm";
const char* fillingTopic      = "rainwatertank/tank/filling";
const char* errorTopic        = "rainwatertank/tank/error";

//set interval for sending messages (milliseconds)
const long interval          = 8000;
unsigned long previousMillis = 0;

int count = 0;

bool checkConnection(){
  return WiFi.status() == WL_CONNECTED;
}

bool checkMqttConnection() {
  return mqttClient.connected();
}

void publishConfig(const char* component, const char* object_id, const char* payload) {
  String topic = String(discoveryTopic) + component + "/" + deviceIdentifier + "/" + object_id + "/config";
  Serial.println("Publishing Discovery to: "+ topic);
  boolean result = mqttClient.publish(topic.c_str(), payload, true);  // Retained

  if(!result) {
    Serial.println("Discovery Message publish failed");
  }
}

void publishDiscovery() {
  Serial.println("Publishing Discovery");
  // Tank Depth
  publishConfig("sensor", "tank_depth",
    R"({
      "name": "Tank Depth",
      "state_topic": "rainwatertank/tank/depth",
      "unit_of_measurement": "cm",
      "device_class": "distance",
      "value_template": "{{ value }}",
      "unique_id": "rainwatertank_depth",
      "availability_topic": "rainwatertank/status",
      "payload_available": "online",
      "payload_not_available": "offline",
      "device": {
        "identifiers": ["rainwatertank"],
        "name": "Water Tank Sensor",
        "manufacturer": "Pete Barnes",
        "model": "TankMonitor on Arduini Uno Wifi Rev2"
      }
    })");

  // Alarm Active
  publishConfig("binary_sensor", "alarm_active",
    R"({
      "name": "Tank Alarm",
      "state_topic": "rainwatertank/tank/alarm",
      "payload_on": "ON",
      "payload_off": "OFF",
      "device_class": "problem",
      "unique_id": "rainwatertank_alarm",
      "availability_topic": "rainwatertank/status",
      "payload_available": "online",
      "payload_not_available": "offline",
      "device": {
        "identifiers": ["rainwatertank"],
        "name": "Water Tank Sensor",
        "manufacturer": "Pete Barnes",
        "model": "TankMonitor on Arduini Uno Wifi Rev2"
      }
    })");

  // Tank Filling
  publishConfig("binary_sensor", "tank_filling",
    R"({
      "name": "Tank Filling",
      "state_topic": "rainwatertank/tank/filling",
      "payload_on": "ON",
      "payload_off": "OFF",
      "device_class": "running",
      "unique_id": "rainwatertank_filling",
      "availability_topic": "rainwatertank/status",
      "payload_available": "online",
      "payload_not_available": "offline",
      "device": {
        "identifiers": ["rainwatertank"],
        "name": "Water Tank Sensor",
        "manufacturer": "Pete Barnes",
        "model": "TankMonitor on Arduini Uno Wifi Rev2"
      }
    })");

  publishConfig("sensor", "error_message",
    R"({
      "name": "Error Message",
      "state_topic": "rainwatertank/tank/error",
      "value_template": "{{ value }}",
      "unique_id": "rainwatertank_error_message",
      "availability_topic": "rainwatertank/status",
      "payload_available": "online",
      "payload_not_available": "offline",
      "device": {
        "identifiers": ["rainwatertank"],
        "name": "Water Tank Sensor",
        "manufacturer": "Pete Barnes",
        "model": "TankMonitor on Arduini Uno Wifi Rev2"
      }
    })");
}




void connectToMqtt() {
  Serial.println("Attempting MQTT connection...");

  // Set Last Will (LWT)
  if (mqttClient.connect(clientId, nullptr, nullptr, statusTopic, 0, true, "offline")) {
    Serial.println("MQTT connected");
    mqttClient.setBufferSize(1000);

    // Publish online status
    mqttClient.publish(statusTopic, "online", true);

    // Publish discovery messages (only once or when reconnecting)
    publishDiscovery();
  } else {
    Serial.print("Failed, rc=");
    Serial.println(mqttClient.state());
  }
}

void configureWifi() {
  // attempt to connect to Wifi network:
  Serial.print("Attempting to connect to WPA SSID: ");
  Serial.println(ssid);
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    //Serial.println(WiFi.status());
    // failed, retry
    Serial.print(".");
    delay(5000);
  }

  Serial.println("You're connected to the network");
  Serial.println();

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setKeepAlive(60);

  connectToMqtt();

}

void pollMqtt() {
  mqttClient.loop();
}

void publishSensorData(int depth, bool tapOpen, bool alarmOn, String errorMsg, long lastReading) { 
  char buffer[10];
  snprintf(buffer, sizeof(buffer), "%d", depth);
  mqttClient.publish(tankDepthTopic, buffer, false);

  mqttClient.publish(alarmTopic, alarmOn ? "ON" : "OFF", false);
  mqttClient.publish(fillingTopic, tapOpen ? "ON" : "OFF", false);
  mqttClient.publish(errorTopic, errorMsg.c_str(), false);
}



