#include <Arduino.h>
#include <WiFiNINA.h>
#include "arduino_secrets.h"
#include "wifi_config.h"
#include "diagnostics_server.h"

namespace {
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
}

bool checkConnection() {
  return WiFi.status() == WL_CONNECTED;
}

void configureWifi() {
  consoleLog(String(F("Attempting to connect to WPA SSID: ")) + ssid);
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    consoleLog(F("Wi-Fi connection failed; retrying in 10 seconds"));
    delay(10000);
  }

  consoleLog(F("You're connected to the network"));
}
