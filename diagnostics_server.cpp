#include <Arduino.h>
#include <WiFiNINA.h>
#include <string.h>
#include <stdlib.h>
#include "diagnostics_server.h"
#include "mqtt.h"

namespace {
WiFiServer server(80);
const uint8_t LOG_COUNT = 50;
const uint8_t LOG_LENGTH = 96;
const uint16_t LOG_BYTE_CAPACITY = 3200;
struct LogRecord {
  uint16_t start;
  uint8_t length;
  uint32_t id;
};
char logBytes[LOG_BYTE_CAPACITY];
LogRecord logRecords[LOG_COUNT];
uint16_t logWritePosition = 0;
uint16_t logBytesUsed = 0;
uint8_t oldestLog = 0;
uint8_t nextLog = 0;
uint8_t storedLogs = 0;
uint32_t nextLogId = 1;
uint32_t latestLogId = 0;

float currentDepth = 0.0f;
unsigned long lastUpdated = 0;
bool hasReading = false;
bool hasEverReading = false;
char sensorStateLabel[12] = "NORMAL";
uint8_t invalidReadingCount = 0;
uint8_t recoveryReadingCount = 0;
bool tankAlarm = false;
bool alarmMuted = false;
bool muteCommandPending = false;
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

void copyLogMessage(const LogRecord& record, char* destination, size_t destinationSize) {
  size_t count = record.length;
  if (count >= destinationSize) count = destinationSize - 1;
  for (size_t i = 0; i < count; ++i) {
    destination[i] = logBytes[(record.start + i) % LOG_BYTE_CAPACITY];
  }
  destination[count] = '\0';
}

const LogRecord& logAt(uint8_t offset) {
  return logRecords[(oldestLog + offset) % LOG_COUNT];
}

void appendLogMessage(const char* message) {
  size_t length = strlen(message);
  if (length > LOG_BYTE_CAPACITY) return;

  while (storedLogs > 0 && (storedLogs == LOG_COUNT || logBytesUsed + length > LOG_BYTE_CAPACITY)) {
    logBytesUsed -= logRecords[oldestLog].length;
    oldestLog = (oldestLog + 1) % LOG_COUNT;
    --storedLogs;
  }

  LogRecord& record = logRecords[nextLog];
  record.start = logWritePosition;
  record.length = (uint8_t)length;
  record.id = nextLogId++;
  for (size_t i = 0; i < length; ++i) {
    logBytes[logWritePosition] = message[i];
    logWritePosition = (logWritePosition + 1) % LOG_BYTE_CAPACITY;
  }
  logBytesUsed += length;
  latestLogId = record.id;
  nextLog = (nextLog + 1) % LOG_COUNT;
  if (storedLogs < LOG_COUNT) ++storedLogs;
}

void printState(WiFiClient& client, bool state) {
  client.print(state ? F("Yes") : F("No"));
}

void printLogHistoryHtml(WiFiClient& client) {
  char message[LOG_LENGTH];
  for (uint8_t i = 0; i < storedLogs; ++i) {
    copyLogMessage(logAt(i), message, sizeof(message));
    printHtmlEscaped(client, message);
    client.println();
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
  client.println(F("<style>body{font:16px sans-serif;max-width:960px;margin:2em auto;padding:0 1em}.panels{display:grid;grid-template-columns:1fr 1fr;gap:1em}section{padding:1em;margin:1em 0;border:1px solid #ccc}.sensor-panel{position:relative}.sensor-refresh{position:absolute;top:.5em;right:.5em}.alarm-mute{margin-left:.5em}dt{color:#52636d}dd{margin:0 0 .8em}pre{height:22em;overflow:auto;white-space:pre-wrap}@media(max-width:640px){.panels{grid-template-columns:1fr}}</style>"));
  client.print(F("<script>window.alarmMuted="));
  client.print(alarmMuted ? F("true") : F("false"));
  client.print(F(";window.tankAlarm="));
  client.print(tankAlarm ? F("true") : F("false"));
  client.println(F(";window.setAlarmMuted=function(m,a){window.alarmMuted=m;if(typeof a==='boolean')window.tankAlarm=a;var v=document.getElementById('tank-alarm');var b=document.getElementById('alarm-mute');if(v)v.textContent=(window.tankAlarm?'Yes':'No')+(m?' (muted)':'');if(b){b.innerHTML=m?'&#128263;':'&#128266;';b.title=m?'Unmute alarm':'Mute alarm';b.setAttribute('aria-label',b.title);}};"));
  client.println(F("window.toggleAlarmMute=function(){var b=document.getElementById('alarm-mute');if(b.disabled)return;b.disabled=true;var x=new XMLHttpRequest();x.open('GET','/alarm?muted='+(window.alarmMuted?'0':'1'),true);"));
  client.println(F("x.onload=function(){if(x.status===200){try{var d=JSON.parse(x.responseText);window.setAlarmMuted(d.muted);}catch(e){b.title='Could not read mute status';}}else{b.title='Mute request failed: '+x.status;}b.disabled=false;};"));
  client.println(F("x.onerror=function(){b.title='Could not update mute status';b.disabled=false;};x.send();};</script>"));
  client.print(F("<script>window.sensorAgeBase="));
  client.print((millis() - lastUpdated) / 1000UL);
  client.print(F(";window.sensorAgeStarted=Date.now();window.sensorHasCurrent="));
  client.print(hasReading ? F("true") : F("false"));
  client.print(F(";window.sensorHasEver="));
  client.print(hasEverReading ? F("true") : F("false"));
  client.println(F(";window.setSensorAge=function(age,ever,current){window.sensorAgeBase=age;window.sensorAgeStarted=Date.now();window.sensorHasEver=ever;window.sensorHasCurrent=current;window.updateSensorAge();};window.updateSensorAge=function(){var e=document.getElementById('sensor-age');if(!e)return;var age=window.sensorAgeBase+Math.floor((Date.now()-window.sensorAgeStarted)/1000);e.textContent=!window.sensorHasEver?'No valid reading yet - monitoring for '+age+' seconds':(!window.sensorHasCurrent?'No current reading - last valid '+age+' seconds ago':age+' seconds ago');};window.setInterval(window.updateSensorAge,1000);</script>"));
  client.println(F("<script>window.refreshSensor=function(){var b=document.getElementById('refresh-sensor');if(b.disabled)return;b.disabled=true;var x=new XMLHttpRequest();"));
  client.println(F("x.open('GET','/sensor',true);x.onload=function(){if(x.status===200){try{var d=JSON.parse(x.responseText);"));
  client.println(F("document.getElementById('sensor-depth').textContent=d.hasReading?d.depth.toFixed(1)+' cm':(d.hasEverReading?'No current valid average':'No valid reading yet');"));
  client.println(F("window.setSensorAge(d.ageSeconds,d.hasEverReading,d.hasReading);"));
  client.println(F("document.getElementById('sensor-state').textContent=d.state;document.getElementById('invalid-count').textContent=d.invalidCount;"));
  client.println(F("document.getElementById('recovery-count').textContent=d.recoveryCount+'/5 valid readings';"));
  client.println(F("document.getElementById('recovery-label').style.display=d.state==='RECOVERY'?'':'none';document.getElementById('recovery-count-row').style.display=d.state==='RECOVERY'?'':'none';"));
  client.println(F("window.setAlarmMuted(d.alarmMuted,d.tankAlarm);document.getElementById('filling').textContent=d.filling?'Yes':'No';"));
  client.println(F("}catch(e){b.title='Could not read sensor status';} }else{b.title='Request failed: '+x.status;}b.disabled=false;};"));
  client.println(F("x.onerror=function(){b.title='Could not load sensor status';b.disabled=false;};x.send();};</script>"));
  client.print(F("<script>window.lastLogId="));
  client.print(latestLogId);
  client.println(F(";var logRequestActive=false;window.clearLogScreen=function(){document.getElementById('logs').textContent='';};window.refreshLogs=function(){if(logRequestActive)return;var b=document.getElementById('refresh-logs');var s=document.getElementById('log-status');var logBox=document.getElementById('logs');"));
  client.println(F("var wasAtBottom=logBox.scrollTop+logBox.clientHeight>=logBox.scrollHeight-2;logRequestActive=true;b.disabled=true;s.textContent='Loading...';var x=new XMLHttpRequest();"));
  client.println(F("x.open('GET','/logs?after='+window.lastLogId,true);x.onload=function(){if(x.status===200){var rows=x.responseText.split('\\n');for(var i=0;i<rows.length;i++){var splitAt=rows[i].indexOf('\\t');if(splitAt>0){var id=parseInt(rows[i].substring(0,splitAt),10);if(id>window.lastLogId){logBox.appendChild(document.createTextNode(rows[i].substring(splitAt+1)+'\\n'));window.lastLogId=id;}}}if(wasAtBottom)logBox.scrollTop=logBox.scrollHeight;s.textContent='Updated';}else{s.textContent='Request failed: '+x.status;}logRequestActive=false;b.disabled=false;};"));
  client.println(F("x.onerror=function(){s.textContent='Could not load logs';logRequestActive=false;b.disabled=false;};x.send();};"));
  client.println(F("window.setInterval(function(){var c=document.getElementById('auto-refresh');if(c&&c.checked)window.refreshLogs();},10000);</script></head><body>"));
  client.println(F("<h1>Water tank diagnostics</h1><div class='panels'><section class='sensor-panel'><h2>Sensor status</h2><button id='refresh-sensor' class='sensor-refresh' type='button' onclick='window.refreshSensor()' title='Refresh sensor data' aria-label='Refresh sensor data'>&#10227;</button><dl>"));
  client.print(F("<dt>Depth</dt><dd id='sensor-depth'>"));
  if (hasReading) { client.print(currentDepth, 1); client.print(F(" cm")); }
  else { client.print(hasEverReading ? F("No current valid average") : F("No valid reading yet")); }
  client.println(F("</dd>"));
  client.print(F("<dt>Last updated</dt><dd id='sensor-age'>"));
  if (hasReading) { client.print((millis() - lastUpdated) / 1000UL); client.print(F(" seconds ago")); }
  else if (hasEverReading) {
    client.print(F("No current reading - last valid "));
    client.print((millis() - lastUpdated) / 1000UL);
    client.print(F(" seconds ago"));
  } else {
    client.print(F("No valid reading yet - monitoring for "));
    client.print((millis() - lastUpdated) / 1000UL);
    client.print(F(" seconds"));
  }
  client.println(F("</dd>"));
  client.print(F("<dt>Sensor state</dt><dd id='sensor-state'>")); printHtmlEscaped(client, sensorStateLabel); client.println(F("</dd>"));
  client.print(F("<dt>Consecutive invalid readings</dt><dd id='invalid-count'>")); client.print(invalidReadingCount); client.println(F("</dd>"));
  client.print(F("<dt id='recovery-label' style='display:"));
  bool recovering = strcmp(sensorStateLabel, "RECOVERY") == 0;
  client.print(recovering ? F("block") : F("none"));
  client.print(F("'>Recovery progress</dt><dd id='recovery-count-row' style='display:"));
  client.print(recovering ? F("block") : F("none"));
  client.println(F("'><span id='recovery-count'>0/5 valid readings</span></dd>"));
  client.print(F("<dt>Tank Alarm</dt><dd><span id='tank-alarm'>"));
  printState(client, tankAlarm);
  if (alarmMuted) client.print(F(" (muted)"));
  client.print(F("</span><button id='alarm-mute' class='alarm-mute' type='button' onclick='window.toggleAlarmMute()'"));
  client.print(alarmMuted ? F(" title='Unmute alarm' aria-label='Unmute alarm'>&#128263;") : F(" title='Mute alarm' aria-label='Mute alarm'>&#128266;"));
  client.println(F("</button></dd>"));
  client.print(F("<dt>Filling</dt><dd id='filling'>")); printState(client, filling); client.println(F("</dd>"));
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
  client.println(F("<section><h2>Recent console logs</h2><button id='refresh-logs' type='button' onclick='window.refreshLogs()' title='Refresh logs' aria-label='Refresh logs'>&#10227;</button> <button id='clear-logs' type='button' onclick='window.clearLogScreen()' title='Clear log screen' aria-label='Clear log screen'>&#9114;</button> <label><input id='auto-refresh' type='checkbox'> Auto-refresh every 10 seconds</label><span id='log-status' aria-live='polite'></span><pre id='logs'>"));
  printLogHistoryHtml(client);
  client.println(F("</pre></section></body></html>"));
}

void sendLogs(WiFiClient& client, uint32_t afterId) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/plain; charset=utf-8"));
  client.println(F("Connection: close"));
  client.println(F("Cache-Control: no-store"));
  client.println();
  char message[LOG_LENGTH];
  for (uint8_t i = 0; i < storedLogs; ++i) {
    const LogRecord& record = logAt(i);
    if (record.id <= afterId) continue;
    client.print(record.id);
    client.print('\t');
    copyLogMessage(record, message, sizeof(message));
    client.print(message);
    client.print('\n');
  }
}

void sendSensorStatus(WiFiClient& client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: application/json; charset=utf-8"));
  client.println(F("Connection: close"));
  client.println(F("Cache-Control: no-store"));
  client.println();
  client.print(F("{\"hasReading\":")); client.print(hasReading ? F("true") : F("false"));
  client.print(F(",\"hasEverReading\":")); client.print(hasEverReading ? F("true") : F("false"));
  client.print(F(",\"depth\":")); client.print(currentDepth, 1);
  client.print(F(",\"ageSeconds\":")); client.print((millis() - lastUpdated) / 1000UL);
  client.print(F(",\"state\":\"")); client.print(sensorStateLabel);
  client.print(F("\",\"invalidCount\":")); client.print(invalidReadingCount);
  client.print(F(",\"recoveryCount\":")); client.print(recoveryReadingCount);
  client.print(F(",\"tankAlarm\":")); client.print(tankAlarm ? F("true") : F("false"));
  client.print(F(",\"alarmMuted\":")); client.print(alarmMuted ? F("true") : F("false"));
  client.print(F(",\"filling\":")); client.print(filling ? F("true") : F("false"));
  client.println(F("}"));
}

void sendAlarmMuteResult(WiFiClient& client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: application/json; charset=utf-8"));
  client.println(F("Connection: close"));
  client.println(F("Cache-Control: no-store"));
  client.println();
  client.print(F("{\"muted\":"));
  client.print(alarmMuted ? F("true") : F("false"));
  client.println(F("}"));
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
  char line[LOG_LENGTH];
  message.toCharArray(line, sizeof(line));
  Serial.println(line);
  appendLogMessage(line);
}

bool consumeAlarmMuteCommand(bool& muted) {
  if (!muteCommandPending) return false;
  muted = alarmMuted;
  muteCommandPending = false;
  return true;
}

void updateWebDiagnostics(float depth, bool readingAvailable, bool everRead, unsigned long readingTimestamp,
                          const char* stateLabel, uint8_t invalidCount, uint8_t recoveryCount,
                          bool tankAlarmState, bool alarmMutedState, bool isFilling, bool isMqttConnected) {
  currentDepth = depth;
  hasReading = readingAvailable;
  hasEverReading = everRead;
  lastUpdated = readingTimestamp;
  strncpy(sensorStateLabel, stateLabel, sizeof(sensorStateLabel) - 1);
  sensorStateLabel[sizeof(sensorStateLabel) - 1] = '\0';
  invalidReadingCount = invalidCount;
  recoveryReadingCount = recoveryCount;
  tankAlarm = tankAlarmState;
  alarmMuted = alarmMutedState;
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
      uint32_t afterId = 0;
      char* afterParameter = strstr(requestLine, "after=");
      if (afterParameter) afterId = (uint32_t)strtoul(afterParameter + 6, nullptr, 10);
      sendLogs(client, afterId);
    } else if (strncmp(requestLine, "GET /sensor ", 12) == 0 || strncmp(requestLine, "GET /sensor?", 12) == 0) {
      sendSensorStatus(client);
    } else if (strncmp(requestLine, "GET /alarm?muted=", 17) == 0) {
      char* value = strstr(requestLine, "muted=") + 6;
      bool requestedMuted = *value == '1';
      if (alarmMuted != requestedMuted) {
        alarmMuted = requestedMuted;
        muteCommandPending = true;
      }
      sendAlarmMuteResult(client);
    } else if (strncmp(requestLine, "GET / ", 6) == 0 || strncmp(requestLine, "GET /?", 6) == 0) {
      sendPage(client);
    } else {
      client.println(F("HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n"));
    }
    client.flush();
  }
  client.stop();
}
