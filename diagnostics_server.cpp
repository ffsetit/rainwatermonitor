#include <Arduino.h>
#include <WiFiNINA.h>
#include <string.h>
#include "diagnostics_server.h"
#include "mqtt.h"

namespace {
WiFiServer server(80);
const uint8_t LOG_COUNT = 12;
const uint8_t LOG_LENGTH = 96;
char logLines[LOG_COUNT][LOG_LENGTH] = {};
uint8_t nextLog = 0;
uint8_t storedLogs = 0;

float currentDepth = 0.0f;
unsigned long lastUpdated = 0;
bool hasReading = false;
bool sensorError = false;
bool levelAlarm = false;
bool filling = false;
bool mqttConnected = false;

void printHtmlEscaped(WiFiClient& client, const char* value) {
  for (const char* p = value; *p; ++p) {
    switch (*p) {
      case '&': client.print(F("&amp;")); break;
      case '<': client.print(F("&lt;")); break;
      case '>': client.print(F("&gt;")); break;
      case '"': client.print(F("&quot;")); break;
      case '\'': client.print(F("&#39;")); break;
      default: client.write(*p); break;
    }
  }
}

void printState(WiFiClient& client, bool state) {
  client.print(state ? F("Yes") : F("No"));
}

void printLogs(WiFiClient& client) {
  for (uint8_t i = 0; i < storedLogs; ++i) {
    uint8_t index = (nextLog + LOG_COUNT - storedLogs + i) % LOG_COUNT;
    client.print(logLines[index]);
    client.print('\n');
  }
}

void sendPage(WiFiClient& client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html; charset=utf-8"));
  client.println(F("Connection: close"));
  client.println(F("Cache-Control: no-store"));
  client.println();
  client.println(F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"));
  client.println(F("<title>Water tank diagnostics</title>"));
  client.println(F("<style>body{font:16px system-ui;max-width:960px;margin:2rem auto;padding:0 1rem;background:#f4f7f8;color:#15232c}"));
  client.println(F(".panels{display:grid;grid-template-columns:1fr 1fr;gap:1rem}section{background:white;padding:1rem 1.25rem;margin:1rem 0;border-radius:10px}"));
  client.println(F("h1{font-size:1.5rem}dt{color:#52636d}dd{margin:0 0 .8rem;font-weight:600}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#101a20;color:#d4f4de;padding:1rem;border-radius:8px}"));
  client.println(F("#logs{height:22rem;overflow-y:auto}"));
  client.println(F("button{padding:.6rem 1rem;border:0;border-radius:6px;background:#176b87;color:white;font:inherit;cursor:pointer}button:disabled{opacity:.6}#log-status{margin-left:.6rem;color:#52636d}"));
  client.println(F("@media(max-width:640px){.panels{grid-template-columns:1fr}}</style>"));
  client.println(F("<script>var logRequestActive=false;window.refreshLogs=function(){if(logRequestActive)return;var b=document.getElementById('refresh-logs');var s=document.getElementById('log-status');var logBox=document.getElementById('logs');"));
  client.println(F("var wasAtBottom=logBox.scrollTop+logBox.clientHeight>=logBox.scrollHeight-2;logRequestActive=true;b.disabled=true;s.textContent='Loading...';var x=new XMLHttpRequest();"));
  client.println(F("x.open('GET','/logs',true);x.onload=function(){if(x.status===200){logBox.textContent=x.responseText;if(wasAtBottom)logBox.scrollTop=logBox.scrollHeight;s.textContent='Updated';}else{s.textContent='Request failed: '+x.status;}logRequestActive=false;b.disabled=false;};"));
  client.println(F("x.onerror=function(){s.textContent='Could not load logs';logRequestActive=false;b.disabled=false;};x.send();};"));
  client.println(F("window.setInterval(function(){var c=document.getElementById('auto-refresh');if(c&&c.checked)window.refreshLogs();},10000);</script></head><body>"));
  client.println(F("<h1>Water tank diagnostics</h1><div class='panels'><section><h2>Sensor status</h2><dl>"));
  client.print(F("<dt>Depth</dt><dd>"));
  if (hasReading) { client.print(currentDepth, 1); client.print(F(" cm")); }
  else { client.print(F("No valid reading yet")); }
  client.println(F("</dd>"));
  client.print(F("<dt>Last updated</dt><dd>"));
  if (hasReading) { client.print((millis() - lastUpdated) / 1000UL); client.print(F(" seconds ago")); }
  else { client.print(F("Never")); }
  client.println(F("</dd>"));
  client.print(F("<dt>Sensor error</dt><dd>")); printState(client, sensorError); client.println(F("</dd>"));
  client.print(F("<dt>Level alarm</dt><dd>")); printState(client, levelAlarm); client.println(F("</dd>"));
  client.print(F("<dt>Filling</dt><dd>")); printState(client, filling); client.println(F("</dd>"));
  client.println(F("</dl></section><section><h2>Network status</h2><dl>"));
  client.print(F("<dt>Wi-Fi SSID</dt><dd>"));
  printHtmlEscaped(client, WiFi.SSID());
  client.println(F("</dd>"));
  client.print(F("<dt>IP address</dt><dd>")); client.print(WiFi.localIP()); client.println(F("</dd>"));
  client.print(F("<dt>Wi-Fi signal</dt><dd>")); client.print(WiFi.RSSI()); client.println(F(" dBm</dd>"));
  client.print(F("<dt>MQTT host</dt><dd>")); client.print(getMqttHost()); client.println(F("</dd>"));
  client.print(F("<dt>MQTT status</dt><dd>"));
  client.print(mqttConnected ? F("Connected") : F("Disconnected"));
  client.println(F("</dd></dl></section></div>"));
  client.println(F("<section><h2>Recent console logs</h2><button id='refresh-logs' type='button' onclick='window.refreshLogs()'>Refresh logs</button> <label><input id='auto-refresh' type='checkbox'> Auto-refresh every 10 seconds</label><span id='log-status' aria-live='polite'></span><pre id='logs'>"));
  for (uint8_t i = 0; i < storedLogs; ++i) {
    uint8_t index = (nextLog + LOG_COUNT - storedLogs + i) % LOG_COUNT;
    printHtmlEscaped(client, logLines[index]);
    client.println();
  }
  client.println(F("</pre></section></body></html>"));
}

void sendLogs(WiFiClient& client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/plain; charset=utf-8"));
  client.println(F("Connection: close"));
  client.println(F("Cache-Control: no-store"));
  client.println();
  printLogs(client);
}
}

void startDiagnosticsServer() {
  server.begin();
  IPAddress ip = WiFi.localIP();
  char message[64];
  snprintf(message, sizeof(message), "Diagnostics server at http://%u.%u.%u.%u",
           ip[0], ip[1], ip[2], ip[3]);
  consoleLog(String(message));
}

void consoleLog(const String& message) {
  Serial.println(message);
  message.toCharArray(logLines[nextLog], LOG_LENGTH);
  nextLog = (nextLog + 1) % LOG_COUNT;
  if (storedLogs < LOG_COUNT) storedLogs++;
}

void updateWebDiagnostics(float depth, bool readingAvailable, unsigned long readingTimestamp,
                          bool sensorErrorState, bool levelAlarmState, bool isFilling, bool isMqttConnected) {
  currentDepth = depth;
  hasReading = readingAvailable;
  lastUpdated = readingTimestamp;
  sensorError = sensorErrorState;
  levelAlarm = levelAlarmState;
  filling = isFilling;
  mqttConnected = isMqttConnected;
}

void handleDiagnosticsServer() {
  WiFiClient client = server.available();
  if (!client) return;

  char requestLine[80];
  uint8_t length = 0;
  uint8_t headerLineLength = 0;
  bool requestLineRead = false;
  bool headersRead = false;
  unsigned long started = millis();
  while (client.connected() && millis() - started < 1000UL) {
    while (client.available()) {
      char c = client.read();
      if (!requestLineRead) {
        if (c == '\n') {
          requestLine[length] = '\0';
          requestLineRead = true;
        } else if (c != '\r' && length < sizeof(requestLine) - 1) {
          requestLine[length++] = c;
        }
      } else if (c == '\n') {
        if (headerLineLength == 0) {
          headersRead = true;
          break;
        }
        headerLineLength = 0;
      } else if (c != '\r' && headerLineLength < 255) {
        headerLineLength++;
      }
    }
    if (headersRead) break;
    delay(1);
  }

  if (headersRead) {
    if (strncmp(requestLine, "GET /logs ", 10) == 0 || strncmp(requestLine, "GET /logs?", 10) == 0) {
      sendLogs(client);
    } else if (strncmp(requestLine, "GET / ", 6) == 0 || strncmp(requestLine, "GET /?", 6) == 0) {
      sendPage(client);
    } else {
      client.println(F("HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n"));
    }
    client.flush();
  }
  client.stop();
}
