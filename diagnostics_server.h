#pragma once

#include <Arduino.h>

void startDiagnosticsServer();
void handleDiagnosticsServer();
void consoleLog(const String& message);
void updateWebDiagnostics(float depth, bool hasReading, unsigned long lastUpdated,
                          bool sensorError, bool levelAlarm, bool filling, bool mqttConnected);
