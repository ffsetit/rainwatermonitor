#pragma once

#include <Arduino.h>

void configureMqtt();
const char* getMqttHost();
bool checkMqttConnection();
void connectToMqtt();
void pollMqtt();
void publishSensorData(float depth, bool tapOpen, bool alarmOn, String errorMsg, unsigned long lastReading);
