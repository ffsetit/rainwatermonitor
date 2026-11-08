#pragma once

#include <Arduino.h>

void startDiagnosticsServer();
void handleDiagnosticsServer();
void consoleLog(const String& message);
bool consumeAlarmMuteCommand(bool& muted);
void updateWebDiagnostics(float depth, bool hasReading, bool hasEverReading, unsigned long lastUpdated,
                          const char* sensorState, uint8_t invalidCount, uint8_t recoveryCount,
                          bool tankAlarm, bool alarmMuted, bool filling, bool mqttConnected);
