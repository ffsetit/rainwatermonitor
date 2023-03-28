#include <NewPing.h>
#include <LiquidCrystal.h>

#define WATERMODE false  // CHANGE THIS TO "false" for the Oil sensor configuration

//These Settings define the physical pins used for the various inputs/outputs
#define TRIGGER_PIN  13  // Arduino pin tied to trigger pin on ping sensor.
#define ECHO_PIN     12  // Arduino pin tied to echo pin on ping sensor.
#define BUZZER_PIN   4   // Arduino pin tied to buzzer

#if WATERMODE == false
#define SOLENOID_PIN 3   // Arduino pin tied to solenoid valve
#endif

#define MAX_DISTANCE 250 // Maximum distance we want to ping for (in centimeters). Maximum sensor distance is rated at 400-500cm.
#define NUM_READINGS 10

NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE); // NewPing setup of pins and maximum distance.

unsigned int pingSpeed = 2500; // How frequently are we going to send out a ping (in milliseconds). 50ms would be 20 times a second.
unsigned long pingTimer;     // Holds the next ping time.

//These setting control how often the LCD Display is updated
unsigned long refreshTimer; // holds the time since the last display update
unsigned long refreshDuration = 10000; // holds the interval to refresh screen / serial output (in milliseconds)

//These settings control the error threshold when no data is receieved from the ultrasonic sensor
volatile unsigned long inputTimer; // holds the time since the last sensor was recieved
unsigned long inputTimeout = 300000; // Duration in seconds to wait for sensor data before sounding the alarm  [5 minutes](in milliseconds)

volatile bool inputError = false; // Boolean to hold the alarm state when no data is recieved from the sensor
bool lowLevel = false; // Booolean to hold the level status


// These settings define the parameters of the tank
#if WATERMODE == true
int sensorHeight = 200; // Height of the sensor above the bottom of the tank
int tankDepth = 170; // Maximum depth of the tank content
int minDepth = 10; // Level at which the low level alarm is triggered
#else
int sensorHeight = 110; // Height of Sensor from Bottom of the tank
int tankDepth = 95; // Maximum depth of oil in the tank
int minDepth = 30; // Minimum depth to trigger alarm
#endif

bool initialized = false; // Value used to determine if the ultra sonic sensor is connected and has returned a valid reading

#if WATERMODE == true
bool fill = false; // Used to track if the tank is currently filling
#endif

int readingPos = 0; // Array index used to determine where to store the latest depth reading
int readings[NUM_READINGS]; //  Array of depth readings

bool attachConsole = true; // Set this to false to disable the Serial console output

const int rs = 6, en = 7, d4 = 8, d5 = 9, d6 = 10, d7 = 11;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

void setup() {

  if(attachConsole) {
    Serial.begin(115200); // Open serial monitor at 115200 baud to see ping results.

    updateConsole("==================");
    updateConsole("Controller Mode: " + (String)(WATERMODE ? "Water" : "Oil"));
    updateConsole("Sensor Echo Pin: " + (String) ECHO_PIN);
    updateConsole("Sensor Trigger Pin: " + (String) TRIGGER_PIN);
    updateConsole("Sensor Interval: " + (String) (pingSpeed/1000) + "s");
    #if WATERMODE == true
    updateConsole("Solenoid Pin: " + (String) SOLENOID_PIN);
    #endif
    updateConsole("Alarm Pin: " + (String) BUZZER_PIN);
    updateConsole("Tank Max Depth: " + (String) tankDepth + "cm");
    updateConsole("Sensor Height: " + (String) sensorHeight + "cm");
    updateConsole("Low Level: " + (String) minDepth + "cm");
    updateConsole("No Data Alarm: " + (String) (inputTimeout/1000) + "s");
    updateConsole("==================");
  }
  
  pingTimer = millis(); // Start now.
  inputTimer = millis(); // Start now.
  refreshTimer = millis()-refreshDuration; // Start now.
  
  lcd.begin(16, 2); // lcd starts with resolution 16x2
  lcd.setCursor(0, 0);
  lcd.print("  Initializing");
  lcd.setCursor(0, 1);
  lcd.print("  Depth Sensor");

  updateConsole("Initializing Depth Sensor");
  
#if WATERMODE == true
  pinMode(SOLENOID_PIN, OUTPUT);
#endif
  pinMode(BUZZER_PIN, OUTPUT);
}

void loop() {
    if (millis() >= pingTimer) {   // pingSpeed milliseconds since last ping, do another ping.
      pingTimer += pingSpeed;      // Set the next ping time.
      sonar.ping_timer(echoCheck); // Send out the ping, calls "echoCheck" function every 24uS where you can check the ping status.
    }
  // Do other stuff here, really. Think of it as multi-tasking.

 
  if( millis() - refreshDuration > refreshTimer ) {
    checkError();
    refreshTimer = millis();
    if(inputError) {
      writeError();
      enableAlarm();
      #if WATERMODE == true
      closeTap();
      #endif
    }
    if (lowLevel) {
      enableAlarm();
      #if WATERMODE == true
      openTap();
      #endif
    } 
    if(!inputError and !lowLevel) {
      disableAlarm();
    }
  }
}

void echoCheck() { // Timer2 interrupt calls this function every 24uS where you can check the ping status.
  // Don't do anything here!
  if (sonar.check_timer()) { // This is how you check to see if the ping was received.
    inputTimer = millis(); // Update last ping recieved time
    int depth = sensorHeight - (sonar.ping_result / US_ROUNDTRIP_CM);
    inputError = false;
    updateResults(depth);
  }
  // Don't do anything here!
}

void checkError() {
 unsigned long currentMillis = millis();
 unsigned long timeSinceLastPing = (currentMillis - inputTimer);
  
  if( timeSinceLastPing > inputTimeout ) {
    inputError = true;
  }
}

void updateResults(int depth) {
  inputTimer = millis(); // Update last ping recieved time
  if(readingPos >= NUM_READINGS) {
    readingPos = 0;
    initialized = true; 
  } else {
    readingPos++;
  }
  readings[readingPos] = depth;
  if(initialized) {
    lowLevel = getAverageDepth() < minDepth; //Set error if the level is below minimum
  }
  updateDisplay();
}

float getAverageDepth() {
  int total = 0;
  for (int i = 0; i < NUM_READINGS; i++) {
      total += readings[i];
  }

  return (float)total / (float)NUM_READINGS;
}

float getRemainingPercentage() {

  float avg = getAverageDepth();
  float percentRemaining = (avg / (float)tankDepth) * 100;

  return percentRemaining;
}

void writeError() {
  unsigned long currentMillis = millis();
  unsigned long timeSinceLastPing = (currentMillis - inputTimer);
 
  int errorDuration = timeSinceLastPing/1000;
  String msg = "Error: No sensor data for " + (String)errorDuration + " seconds";
  updateConsole(msg);
  updateConsole("Current Millis: " + (String)currentMillis + ", Last Ping Millis: " + (String)timeSinceLastPing);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("No Sensor Data");
  lcd.setCursor(0, 1);
  lcd.print("in " + (String)errorDuration + "s");
}

void enableAlarm() {
  if(0 == digitalRead(BUZZER_PIN)) {
    digitalWrite(BUZZER_PIN, HIGH);
    updateConsole("Alarm Active");
  }
}

void disableAlarm() {
  if(1 == digitalRead(BUZZER_PIN)) {
    digitalWrite(BUZZER_PIN, LOW);
    updateConsole("Alarm Inactive");
  }
}

#if WATERMODE == true
void openTap() {
  if(0 == digitalRead(SOLENOID_PIN)) {
    digitalWrite(SOLENOID_PIN, HIGH);
    fill = true;
  }
}

void closeTap() {
  if(fill) {
    digitalWrite(SOLENOID_PIN, LOW);
  }
}
#endif

void updateDisplay() {
    String depthMsg = String("Depth: ") + getAverageDepth();
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
    Serial.println(message);
  }
}
