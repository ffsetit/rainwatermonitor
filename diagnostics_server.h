#pragma once

#include <Arduino.h>

void startDiagnosticsServer();
void handleDiagnosticsServer();
void consoleLog(const String& message);
void updateWebDiagnostics(float depth, bool hasReading, unsigned long lastUpdated,
                          const char* sensorState, uint8_t invalidCount, uint8_t recoveryCount,
                          bool levelAlarm, bool filling, bool mqttConnected);
