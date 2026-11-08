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

const bool debug = false;

unsigned int pingSpeed = 2500; // How frequently are we going to send out a ping (in milliseconds). 50ms would be 20 times a second.
unsigned long pingTimer;     // Time of the last ping.

//These setting control how often the LCD Display is updated
unsigned long refreshTimer; // holds the time since the last display update
const unsigned long refreshDuration = 10000; // holds the interval to refresh screen / serial output (in milliseconds)

//These settings control the error threshold when no data is receieved from the ultrasonic sensor
unsigned long inputTimer; // time of the last valid sensor reading
unsigned long inputTimeout = 300000; // Duration in seconds to wait for sensor data before sounding the alarm  [5 minutes](in milliseconds)

bool inputError = false; // Boolean to hold the alarm state when no data is recieved from the sensor
bool levelAlarm = false; // Booolean to hold the level status


// These settings define the parameters of the tank
bool fill = false; // Used to track if the tank is currently filling
int sensorHeight = 197; // Height of the sensor above the bottom of the tank
int tankDepth = 170; // Maximum depth of the tank content
int minDepth = 10; // Level at which the low level alarm is triggered
bool initialized = false; // Value used to determine if the ultra sonic sensor is connected and has returned a valid reading

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
  
  pingTimer = millis() - pingSpeed; // Take the first reading immediately.
  inputTimer = millis(); // Start now.
  refreshTimer = millis(); // Start now.
  
  lcd.begin(16, 2); // lcd starts with resolution 16x2
  lcd.setCursor(0, 0);
  lcd.print("  Initializing");
  lcd.setCursor(0, 1);
  lcd.print("  Depth Sensor");

  configureWifi();
  startDiagnosticsServer();
  configureMqtt();
  connectToMqtt();

  updateConsole("Initializing Depth Sensor");
  
  pinMode(SOLENOID_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIGGER_PIN, LOW);
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
          inputTimer = millis();
          inputError = false;
          updateResults(depth);
        }
      }
    }
  // Do other stuff here, really. Think of it as multi-tasking.
  
  pollMqtt();
  updateWebDiagnostics(getAverageDepth(), validReadingCount > 0, inputTimer,
                       inputError, levelAlarm, fill, checkMqttConnection());
  handleDiagnosticsServer();

  now = millis();
  if (now - refreshTimer >= refreshDuration) {
    if(debug) {
      updateConsole("Refreshing....");
    }

    refreshTimer = now;
    checkError();
    
    if(inputError) {
      writeError();
      enableAlarm();
      closeTap();
    }
    if (levelAlarm && !inputError) {
      enableAlarm();
      openTap();
    } 
    if(!inputError and !levelAlarm) {
      disableAlarm();
    }

    if(!checkConnection())
    {
      configureWifi();
    }
    if(!checkMqttConnection()){
      connectToMqtt();
    }
    
    publishSensorData(getAverageDepth(), fill, levelAlarm or inputError, inputError ? getErrorMessage() : "", inputTimer);
  }
}

void checkError() {
  if(debug) {
    updateConsole("checkError Called");
  }

  unsigned long currentMillis = millis();
  unsigned long timeSinceLastPing = (currentMillis - inputTimer);

  if( timeSinceLastPing > inputTimeout ) {
    inputError = true;
  }
}

void updateResults(int depth) {
  if(debug) {
    updateConsole("updateResults Called");
  }

  readings[readingPos] = depth;
  readingPos = (readingPos + 1) % NUM_READINGS;
  if (validReadingCount < NUM_READINGS) {
    validReadingCount++;
  }
  initialized = validReadingCount == NUM_READINGS;
  if(initialized) {
    #if HIGHLEVEL == true
      levelAlarm = getAverageDepth() > maxDepth; //Set error if the level is above maximum
    #else
      levelAlarm = getAverageDepth() < minDepth; //Set error if the level is below minimum
    #endif
  }
  updateDisplay();
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

int getErrorDuration() {
  unsigned long currentMillis = millis();
  unsigned long timeSinceLastPing = (currentMillis - inputTimer);

  return timeSinceLastPing/1000;
}

String getErrorMessage() {
  if(debug) {
    updateConsole("getErrorMessage Called");
  }

  String msg = "No sensor data for " + (String)getErrorDuration() + " seconds";
  return msg;
}

void writeError() {
  String msg = getErrorMessage();
  updateConsole(msg);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("No Sensor Data");
  lcd.setCursor(0, 1);
  lcd.print("in " + (String)getErrorDuration() + "s");

}

void enableAlarm() {
  if(0 == digitalRead(BUZZER_PIN)) {
    digitalWrite(BUZZER_PIN, HIGH);
    //analogWrite(BUZZER_PIN, 127)
    updateConsole("Alarm Active");
  }
}

void disableAlarm() {
  if(1 == digitalRead(BUZZER_PIN)) {
    digitalWrite(BUZZER_PIN, LOW);
    updateConsole("Alarm Inactive");
  }
}

void openTap() {
  if(0 == digitalRead(SOLENOID_PIN)) {
    digitalWrite(SOLENOID_PIN, HIGH);
    fill = true;
  }
}

void closeTap() {
  if(fill) {
    digitalWrite(SOLENOID_PIN, LOW);
    fill = false;
  }
}

void updateDisplay() {
    String depthMsg = String("Depth: ") + String(getAverageDepth(), 1);
    String percentageMsg = getRemainingPercentage() + String("% Remaining");
    updateConsole(depthMsg + " " + percentageMsg);
  
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(depthMsg);
    lcd.setCursor(0, 1);
    lcd.print(percentageMsg);
}

void updateConsole(String message) {
  if(attachConsole) {
    consoleLog(message);
  }
}
