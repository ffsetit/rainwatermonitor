#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiNINA.h>
#include "arduino_secrets.h"
#include "mqtt.h"
#include "diagnostics_server.h"

namespace {
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

const char* mqttServer = SECRET_MQTT_HOST;
const int mqttPort = 1883;
const char* clientId = "rainwatertank";
const char* deviceIdentifier = "rainwatertank";
const char* discoveryTopic = "homeassistant/";
const char* statusTopic = "rainwatertank/status";
const char* tankDepthTopic = "rainwatertank/tank/depth";
const char* alarmTopic = "rainwatertank/tank/alarm";
const char* fillingTopic = "rainwatertank/tank/filling";
const char* errorTopic = "rainwatertank/tank/error";
const char* wifiIpTopic = "rainwatertank/network/ip";
const char* wifiSsidTopic = "rainwatertank/network/ssid";
const char* wifiSignalTopic = "rainwatertank/network/signal";
const char* sensorStateTopic = "rainwatertank/sensor/state";

void publishConfig(const char* component, const char* objectId, const char* payload) {
  String topic = String(discoveryTopic) + component + "/" + deviceIdentifier + "/" + objectId + "/config";
  consoleLog(String(F("Publishing Discovery to: ")) + topic);
  if (!mqttClient.publish(topic.c_str(), payload, true)) {
    consoleLog(F("Discovery Message publish failed"));
  }
}

void publishDiscovery() {
  consoleLog(F("Publishing Discovery"));
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

  publishConfig("sensor", "wifi_ip",
    R"({
      "name": "Wi-Fi IP Address",
      "state_topic": "rainwatertank/network/ip",
      "entity_category": "diagnostic",
      "icon": "mdi:ip-network",
      "unique_id": "rainwatertank_wifi_ip",
      "availability_topic": "rainwatertank/status",
      "payload_available": "online",
      "payload_not_available": "offline",
      "device": {"identifiers": ["rainwatertank"], "name": "Water Tank Sensor", "manufacturer": "Pete Barnes", "model": "TankMonitor on Arduini Uno Wifi Rev2"}
    })");

  publishConfig("sensor", "wifi_ssid",
    R"({
      "name": "Wi-Fi SSID",
      "state_topic": "rainwatertank/network/ssid",
      "entity_category": "diagnostic",
      "icon": "mdi:wifi",
      "unique_id": "rainwatertank_wifi_ssid",
      "availability_topic": "rainwatertank/status",
      "payload_available": "online",
      "payload_not_available": "offline",
      "device": {"identifiers": ["rainwatertank"], "name": "Water Tank Sensor", "manufacturer": "Pete Barnes", "model": "TankMonitor on Arduini Uno Wifi Rev2"}
    })");

  publishConfig("sensor", "wifi_signal",
    R"({
      "name": "Wi-Fi Signal",
      "state_topic": "rainwatertank/network/signal",
      "entity_category": "diagnostic",
      "device_class": "signal_strength",
      "state_class": "measurement",
      "unit_of_measurement": "dBm",
      "icon": "mdi:wifi",
      "unique_id": "rainwatertank_wifi_signal",
      "availability_topic": "rainwatertank/status",
      "payload_available": "online",
      "payload_not_available": "offline",
      "device": {"identifiers": ["rainwatertank"], "name": "Water Tank Sensor", "manufacturer": "Pete Barnes", "model": "TankMonitor on Arduini Uno Wifi Rev2"}
    })");

  publishConfig("sensor", "sensor_state",
    R"({
      "name": "Sensor State",
      "state_topic": "rainwatertank/sensor/state",
      "entity_category": "diagnostic",
      "icon": "mdi:state-machine",
      "unique_id": "rainwatertank_sensor_state",
      "availability_topic": "rainwatertank/status",
      "payload_available": "online",
      "payload_not_available": "offline",
      "device": {"identifiers": ["rainwatertank"], "name": "Water Tank Sensor", "manufacturer": "Pete Barnes", "model": "TankMonitor on Arduini Uno Wifi Rev2"}
    })");
}
}

void configureMqtt() {
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setKeepAlive(60);
  mqttClient.setBufferSize(1000);
}

const char* getMqttHost() {
  return mqttServer;
}

bool checkMqttConnection() {
  return mqttClient.connected();
}

void connectToMqtt() {
  consoleLog(F("Attempting MQTT connection..."));
  if (mqttClient.connect(clientId, nullptr, nullptr, statusTopic, 0, true, "offline")) {
    consoleLog(F("MQTT connected"));
    mqttClient.publish(statusTopic, "online", true);
    publishDiscovery();
  } else {
    consoleLog(String(F("Failed, rc=")) + mqttClient.state());
  }
}

void pollMqtt() {
  mqttClient.loop();
}

void publishSensorData(float depth, bool tapOpen, bool alarmOn, String errorMsg,
                       unsigned long lastReading, const char* sensorState) {
  (void)lastReading;
  char buffer[16];
  dtostrf(depth, 0, 1, buffer);
  mqttClient.publish(tankDepthTopic, buffer, false);
  mqttClient.publish(alarmTopic, alarmOn ? "ON" : "OFF", false);
  mqttClient.publish(fillingTopic, tapOpen ? "ON" : "OFF", false);
  mqttClient.publish(errorTopic, errorMsg.c_str(), false);

  IPAddress ip = WiFi.localIP();
  char ipAddress[16];
  snprintf(ipAddress, sizeof(ipAddress), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  char signal[8];
  snprintf(signal, sizeof(signal), "%ld", (long)WiFi.RSSI());
  mqttClient.publish(wifiIpTopic, ipAddress, false);
  mqttClient.publish(wifiSsidTopic, WiFi.SSID(), false);
  mqttClient.publish(wifiSignalTopic, signal, false);
  mqttClient.publish(sensorStateTopic, sensorState, false);
}
