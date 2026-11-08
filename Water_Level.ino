#include <LiquidCrystal.h>
#include "wifi_config.h"
#include "mqtt.h"
#include "diagnostics_server.h"

//These Settings define the physical pins used for the various inputs/outputs
#define TRIGGER_PIN  13  // Arduino pin tied to trigger pin on ping sensor.
#define ECHO_PIN     12  // Arduino pin tied to echo pin on ping sensor.
#define BUZZER_PIN   4   // Arduino pin tied to buzzer
#define SOLENOID_PIN 3   // Arduino pin tied to solenoid valve

#define MAX_DISTANCE 250 // Maximum distance we want to ping for (in centimeters). Maximum sensor distance is rated at 400-500cm.
#define NUM_READINGS 10
#define MAX_INVALID_READINGS 3
#define RECOVERY_READINGS_REQUIRED 5

const bool debug = false;

unsigned int pingSpeed = 2500; // How frequently are we going to send out a ping (in milliseconds). 50ms would be 20 times a second.
unsigned long pingTimer;     // Time of the last ping.

//These setting control how often the LCD Display is updated
unsigned long refreshTimer; // holds the time since the last display update
const unsigned long refreshDuration = 10000; // holds the interval to refresh screen / serial output (in milliseconds)

//These settings control the error threshold when no data is receieved from the ultrasonic sensor
unsigned long inputTimer; // time of the last valid sensor reading
const unsigned long inputTimeout = 300000UL;

enum SensorState { SENSOR_NORMAL, SENSOR_WARNING, SENSOR_FAULT, SENSOR_RECOVERY };
const char* sensorStateName(SensorState state);
SensorState sensorState = SENSOR_NORMAL;
int consecutiveInvalidReadings = 0;
int consecutiveValidReadings = 0;
bool sensorInitialized = false;
bool levelMonitoringReady = false;
float recoveryReadings[RECOVERY_READINGS_REQUIRED];
char sensorFaultReason[48] = "";
bool levelAlarm = false; // Booolean to hold the level status


// These settings define the parameters of the tank
bool fill = false; // Used to track if the tank is currently filling
int sensorHeight = 197; // Height of the sensor above the bottom of the tank
int tankDepth = 170; // Maximum depth of the tank content
int minDepth = 10; // Level at which the low level alarm is triggered

int readingPos = 0; // Next array index to write
int validReadingCount = 0; // Number of initialized entries in readings[]
int readings[NUM_READINGS]; //  Array of depth readings

bool attachConsole = true; // Set this to false to disable the Serial console output

const int rs = 6, en = 7, d4 = 8, d5 = 9, d6 = 10, d7 = 11;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

void setup() {

  if(attachConsole) {
    Serial.begin(115200); // Open serial monitor at 115200 baud to see ping results.

    updateConsole("==================");
    updateConsole("Controller Mode: Water" );
    updateConsole("Sensor Echo Pin: " + (String) ECHO_PIN);
    updateConsole("Sensor Trigger Pin: " + (String) TRIGGER_PIN);
    updateConsole("Sensor Interval: " + (String) (pingSpeed/1000) + "s");
    updateConsole("Solenoid Pin: " + (String) SOLENOID_PIN);
    updateConsole("Alarm Pin: " + (String) BUZZER_PIN);
    updateConsole("Tank Max Depth: " + (String) tankDepth + "cm");
    updateConsole("Sensor Height: " + (String) sensorHeight + "cm");
    updateConsole("Low Level: " + (String) minDepth + "cm");
    updateConsole("No Data Alarm: " + (String) (inputTimeout/1000) + "s");
    updateConsole("==================");
  }
  
  refreshTimer = millis();
  
  lcd.begin(16, 2); // lcd starts with resolution 16x2
  lcd.setCursor(0, 0);
  lcd.print("  Initializing");
  lcd.setCursor(0, 1);
  lcd.print("  Depth Sensor");

  configureWifi();
  startDiagnosticsServer();
  configureMqtt();
  connectToMqtt();

  pingTimer = millis() - pingSpeed; // Take the first reading immediately.
  inputTimer = millis(); // Start the initial sensor timeout after network setup.

  updateConsole("Initializing Depth Sensor");
  
  pinMode(SOLENOID_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIGGER_PIN, LOW);
  digitalWrite(SOLENOID_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
}

void loop() {
    unsigned long now = millis();
    if (now - pingTimer >= pingSpeed) {
      pingTimer = now;
      // pulseIn is synchronous, so no timer callback or shared ISR state is needed.
      // 58 us is approximately the round-trip time per centimeter.
      digitalWrite(TRIGGER_PIN, LOW);
      delayMicroseconds(2);
      digitalWrite(TRIGGER_PIN, HIGH);
      delayMicroseconds(10);
      digitalWrite(TRIGGER_PIN, LOW);
      const unsigned long echoTimeoutUs = (MAX_DISTANCE * 58UL) + 1000UL;
      unsigned long echoTime = pulseIn(ECHO_PIN, HIGH, echoTimeoutUs);
      unsigned int distanceCm = echoTime == 0 ? 0 : (unsigned int)((echoTime + 29UL) / 58UL);
      if (distanceCm > 0) {
        int depth = sensorHeight - (int)distanceCm;
        if (depth >= 0 && depth <= tankDepth) {
          handleSensorReading((float)depth);
        } else {
          handleInvalidReading(distanceCm);
        }
      } else {
        handleInvalidReading(0);
      }
    }

  checkSensorTimeout();
  updateOutputs();
  // Do other stuff here, really. Think of it as multi-tasking.
  
  pollMqtt();
  updateWebDiagnostics(getAverageDepth(), validReadingCount > 0, inputTimer,
                       sensorStateName(sensorState), consecutiveInvalidReadings,
                       consecutiveValidReadings, levelAlarm, fill, checkMqttConnection());
  handleDiagnosticsServer();

  now = millis();
  if (now - refreshTimer >= refreshDuration) {
    if(debug) {
      updateConsole("Refreshing....");
    }

    refreshTimer = now;
    updateDisplay();

    if(!checkConnection())
    {
      configureWifi();
    }
    if(!checkMqttConnection()){
      connectToMqtt();
    }
    
    publishSensorData(getAverageDepth(), fill, alarmShouldBeActive(), getErrorMessage(), inputTimer,
                      sensorStateName(sensorState));
  }
}

const char* sensorStateName(SensorState state) {
  switch (state) {
    case SENSOR_WARNING: return "WARNING";
    case SENSOR_FAULT: return "FAULT";
    case SENSOR_RECOVERY: return "RECOVERY";
    default: return "NORMAL";
  }
}

void resetReadings() {
  for (int i = 0; i < NUM_READINGS; ++i) readings[i] = 0;
  readingPos = 0;
  validReadingCount = 0;
}

void recalculateLevelAlarm() {
  #if HIGHLEVEL == true
    levelAlarm = getAverageDepth() > maxDepth;
  #else
    levelAlarm = getAverageDepth() < minDepth;
  #endif
}

bool alarmShouldBeActive() {
  return levelAlarm || sensorState == SENSOR_FAULT || sensorState == SENSOR_RECOVERY;
}

void updateOutputs() {
  bool shouldFill = sensorState == SENSOR_NORMAL && levelAlarm;
  bool shouldAlarm = alarmShouldBeActive();

  if (fill != shouldFill) {
    digitalWrite(SOLENOID_PIN, shouldFill ? HIGH : LOW);
    fill = shouldFill;
    updateConsole(shouldFill ? "Solenoid Open" : "Solenoid Closed");
  }
  if (digitalRead(BUZZER_PIN) != (shouldAlarm ? HIGH : LOW)) {
    digitalWrite(BUZZER_PIN, shouldAlarm ? HIGH : LOW);
    updateConsole(shouldAlarm ? "Alarm Active" : "Alarm Inactive");
  }
}

void enterSensorFault(const String& reason) {
  if (sensorState == SENSOR_FAULT) return;

  sensorState = SENSOR_FAULT;
  consecutiveValidReadings = 0;
  reason.toCharArray(sensorFaultReason, sizeof(sensorFaultReason));
  levelAlarm = false;
  levelMonitoringReady = false;
  resetReadings();
  updateConsole(String("SENSOR FAULT: ") + reason);
  updateDisplay();
  updateOutputs();
}

void checkSensorTimeout() {
  if (sensorState != SENSOR_FAULT && millis() - inputTimer >= inputTimeout) {
    enterSensorFault("No valid sensor reading for 5 minutes");
  }
}

void handleInvalidReading(unsigned int distanceCm) {
  updateConsole(String("Invalid sensor reading: ") + distanceCm + " cm");
  consecutiveValidReadings = 0;

  if (sensorState == SENSOR_RECOVERY) {
    enterSensorFault("Invalid reading during recovery");
    return;
  }
  if (!sensorInitialized || sensorState == SENSOR_FAULT) return;

  if (consecutiveInvalidReadings < MAX_INVALID_READINGS) {
    ++consecutiveInvalidReadings;
  }
  if (consecutiveInvalidReadings >= MAX_INVALID_READINGS) {
    enterSensorFault("Three consecutive invalid readings");
    return;
  }
  if (sensorState == SENSOR_NORMAL) {
    sensorState = SENSOR_WARNING;
    updateConsole("Sensor Warning");
    updateDisplay();
  }
}

void handleSensorReading(float depth) {
  inputTimer = millis();
  sensorInitialized = true;
  consecutiveInvalidReadings = 0;

  if (sensorState == SENSOR_FAULT || sensorState == SENSOR_RECOVERY) {
    if (sensorState == SENSOR_FAULT) {
      sensorState = SENSOR_RECOVERY;
      consecutiveValidReadings = 0;
      updateConsole("Sensor Recovery Started");
    }

    recoveryReadings[consecutiveValidReadings++] = depth;
    updateConsole(String("Recovery Reading: ") + consecutiveValidReadings + "/" + RECOVERY_READINGS_REQUIRED);
    if (consecutiveValidReadings < RECOVERY_READINGS_REQUIRED) {
      updateDisplay();
      return;
    }

    resetReadings();
    for (int i = 0; i < RECOVERY_READINGS_REQUIRED; ++i) readings[i] = recoveryReadings[i];
    validReadingCount = RECOVERY_READINGS_REQUIRED;
    readingPos = validReadingCount % NUM_READINGS;
    levelMonitoringReady = true;
    sensorState = SENSOR_NORMAL;
    consecutiveValidReadings = 0;
    recalculateLevelAlarm();
    updateConsole(String("Sensor Recovery Successful. Depth: ") + String(getAverageDepth(), 1) + "cm");
    updateDisplay();
    updateOutputs();
    return;
  }

  if (sensorState == SENSOR_WARNING) {
    sensorState = SENSOR_NORMAL;
    updateConsole("Sensor Warning Cleared");
  }
  consecutiveValidReadings = 0;
  readings[readingPos] = depth;
  readingPos = (readingPos + 1) % NUM_READINGS;
  if (validReadingCount < NUM_READINGS) ++validReadingCount;
  if (validReadingCount == NUM_READINGS) levelMonitoringReady = true;
  if (levelMonitoringReady) recalculateLevelAlarm();
  updateDisplay();
  updateOutputs();
}

float getAverageDepth() {
  int total = 0;
  for (int i = 0; i < validReadingCount; i++) {
      total += readings[i];
  }
  if (validReadingCount == 0) return 0.0f;
  return (float)total / (float)validReadingCount;
}

float getRemainingPercentage() {

  float avg = getAverageDepth();
  float percentRemaining = (avg / (float)tankDepth) * 100;
  return percentRemaining;
}

String getErrorMessage() {
  switch (sensorState) {
    case SENSOR_WARNING:
      return String("Sensor warning: ") + consecutiveInvalidReadings + "/" + MAX_INVALID_READINGS + " invalid readings";
    case SENSOR_FAULT:
      return String("Sensor fault: ") + sensorFaultReason + " (last valid " +
             ((millis() - inputTimer) / 1000UL) + "s ago)";
    case SENSOR_RECOVERY:
      return String("Sensor recovery: ") + consecutiveValidReadings + "/" + RECOVERY_READINGS_REQUIRED + " valid readings";
    default:
      return "";
  }
}

void updateDisplay() {
  lcd.clear();
  if (sensorState == SENSOR_FAULT) {
    lcd.setCursor(0, 0);
    lcd.print("Sensor Fault");
    lcd.setCursor(0, 1);
    lcd.print("Check Sensor!");
    return;
  }
  if (sensorState == SENSOR_RECOVERY) {
    lcd.setCursor(0, 0);
    lcd.print("Sensor Recovery");
    lcd.setCursor(0, 1);
    lcd.print(String(consecutiveValidReadings) + "/" + RECOVERY_READINGS_REQUIRED + " Valid");
    return;
  }
  if (sensorState == SENSOR_WARNING) {
    lcd.setCursor(0, 0);
    lcd.print("Sensor Warning");
    lcd.setCursor(0, 1);
    lcd.print("Checking Sensor");
    return;
  }
  if (validReadingCount == 0) {
    lcd.setCursor(0, 0);
    lcd.print("Waiting for");
    lcd.setCursor(0, 1);
    lcd.print("Sensor Data");
    return;
  }

  String depthMsg = String("Depth: ") + String(getAverageDepth(), 1) + "cm";
  String percentageMsg = String(getRemainingPercentage(), 1) + "% Remaining";
  lcd.setCursor(0, 0);
  lcd.print(depthMsg);
  lcd.setCursor(0, 1);
  lcd.print(percentageMsg);
  updateConsole(depthMsg + " " + percentageMsg);
}

void updateConsole(String message) {
  if(attachConsole) {
    consoleLog(message);
  }
}
