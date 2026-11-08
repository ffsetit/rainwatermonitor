# Water Level

Water Level is an Arduino-based rainwater tank monitor. It measures the water depth with an ultrasonic sensor, shows readings and alarms on a 16×2 LCD, controls a solenoid valve, and publishes status over MQTT for Home Assistant. A small web page on the device provides live diagnostics and recent console logs.

## Features

- Ultrasonic depth readings with a rolling average
- Low-level (or configurable high-level) alarm and valve control
- LCD and buzzer status feedback
- Wi-Fi connectivity and MQTT sensor discovery for Home Assistant
- HTTP diagnostics page with sensor, network, and log status

## Hardware

The source targets an **Arduino Uno WiFi Rev2** and currently assigns these pins:

| Component | Arduino pin |
| --- | ---: |
| Ultrasonic sensor trigger | 13 |
| Ultrasonic sensor echo | 12 |
| Buzzer | 4 |
| Solenoid valve | 3 |
| 16×2 LCD (RS, E, D4–D7) | 6, 7, 8, 9, 10, 11 |

The sketch assumes the sensor is mounted above the tank and that the configured sensor height and tank depth match the installation. Check the pin assignments and tank settings near the top of `Water_Level.ino` before powering connected hardware. Use suitable driver circuitry and power supplies for the valve and other loads; do not drive them directly from Arduino pins unless their electrical requirements permit it.

## Build and upload

1. Install the Arduino IDE and select **Arduino Uno WiFi Rev2** as the board.
2. Install the **PubSubClient** library by Nick O'Leary through the Arduino Library Manager. `LiquidCrystal` and `WiFiNINA` are provided by the board/library environment.
3. Create `arduino_secrets.h` in the sketch folder with your network credentials:

   ```cpp
   #define SECRET_SSID "your-wifi-name"
   #define SECRET_PASS "your-wifi-password"
   #define SECRET_MQTT_HOST "your-mqtt-broker-hostname"
   ```

   This file is excluded from Git. Never commit real credentials.
4. Open `Water_Level.ino`, review the installation settings, then compile and upload.

Set `SECRET_MQTT_HOST` to the hostname of an MQTT broker reachable from the device's Wi-Fi network. The MQTT port (1883), topics, and Home Assistant discovery identifiers are defined in `mqtt.cpp`. The diagnostics server listens on port 80; its address is logged to the serial console during startup.

## Configuration

Adjust the pin definitions, `sensorHeight`, `tankDepth`, `minDepth`, sensor timing, and alarm behavior in `Water_Level.ino` for your installation. Configure the MQTT host in the ignored `arduino_secrets.h` file; configure the port and topic identifiers in `mqtt.cpp`. The diagnostics page is served by the device's built-in HTTP server after it joins Wi-Fi.

## Contributing

Bug reports, documentation improvements, and code contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for the contribution process and coding guidance.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
