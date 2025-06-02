# Pico WiFi Provisioning

A PlatformIO library for securely provisioning WiFi credentials to Raspberry Pi Pico W boards with over BLE (Bluetooth Low Energy).

## Overview

This library provides a simple and secure way to provision WiFi credentials to a Raspberry Pi Pico W device using Bluetooth Low Energy (BLE) with secure pairing. It's designed specifically for use with the [arduino-pico](https://github.com/earlephilhower/arduino-pico) core and requires the Pico W variant with onboard WiFi and Bluetooth capabilities.

## Features:

- **Secure BLE Provisioning:** Receive WiFi SSID and password securely over a paired BLE connection.
- **Secure Pairing:** Utilizes BLESecure for secure bonding and encryption during provisioning.
- **Credential Storage:** Saves WiFi network configurations to LittleFS flash memory.
- **Automatic Connection:** Attempts to connect to stored networks on startup.
- **Status Callbacks:** Provides callbacks to monitor the provisioning process, WiFi status, and BLE connection state.

## Compatibility

- **Hardware:** Raspberry Pi Pico W
- **Framework:** Arduino
- **Core:** earlephilhower/arduino-pico
- **PlatformIO Platform:** maxgerhardt/platform-raspberrypi

## Dependencies

- [arduino-pico](https://github.com/earlephilhower/arduino-pico) core with BLE and WiFi support
- [pico-ble-secure](https://github.com/IoT-gamer/pico-ble-secure) library for secure BLE connections
- [pico-ble-notify](https://github.com/IoT-gamer/pico-ble-notify) library for BLE notifications
- [ArduinoJson](https://arduinojson.org/) library for configuration storage

## Installation

### Using PlatformIO Registry

1. Add the library to your `platformio.ini`:

```ini
lib_deps =
    pico-wifi-provisioning
```

## Example Project

Check out the [BasicProvisioning](/examples/BasicProvisioning) example for a complete implementation including:

- Onbard LED status indication
  - Fast blink: connecting to WiFi
  - Solid on: connected to WiFi
- GPIO LED for BLE status indication
- BOOTSEL button handling for network reset
- BLE pairing with encryption and feedback
- WiFi connection management

### Notes
- Need to connect to Serial Monitor
- Pairing on android may show duplicate requests (click both)
- Pairing request will timout quickly
- BLE will dosconnect after receiving WiFi credentials
- May take a few seconds to connect to WiFi after provisioning

## Usage

Here's a basic example of how to use the library:

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <BTstackLib.h>
#include <BLESecure.h>
#include <BLENotify.h>
#include "PicoWiFiProvisioning.h"

// LED for status indication
const int LED_PIN = LED_BUILTIN;

// Callback for WiFi status changes
void onWiFiStatus(wl_status_t status) {
  switch (status) {
    case WL_CONNECTED:
      Serial.println("WiFi connected!");
      Serial.print("IP address: "); Serial.println(WiFi.localIP());
      digitalWrite(LED_PIN, HIGH); // LED on when connected
      break;
    case WL_DISCONNECTED:
    case WL_CONNECTION_LOST:
      Serial.println("WiFi disconnected");
      digitalWrite(LED_PIN, LOW); // LED off when disconnected
      break;
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Set callback for WiFi status changes
  PicoWiFiProvisioning.setWiFiStatusCallback(onWiFiStatus);
  
  // Initialize WiFi provisioning (device name, security level, IO capability)
  if (PicoWiFiProvisioning.begin("PicoW", SECURITY_MEDIUM, IO_CAPABILITY_NO_INPUT_NO_OUTPUT)) {
    // Try to connect to any stored WiFi networks
    Serial.println("Attempting to connect to stored networks...");
    if (PicoWiFiProvisioning.connectToStoredNetworks()) {
      Serial.println("Connected to a stored network!");
    } else {
      Serial.println("No stored networks or failed to connect");
      Serial.println("Waiting for BLE connection to provision WiFi...");
    }
  }
}

void loop() {
  // Process WiFi and BLE events
  PicoWiFiProvisioning.loop();
  
  // Your application code here
  
  delay(10);
}
```

## Key Steps:

- Include the `PicoWiFiProvisioning.h` header.
- Define and implement callback functions for `onWiFiStatus`, `onProvisionStatus`, and `handleBleConnectionChange`.
- Set the callback functions using `PicoWiFiProvisioning.set...Callback()`.
- Initialize the provisioning service using `PicoWiFiProvisioning.begin()`, providing a device name, security level, and IO capability.
- Optionally, call `PicoWiFiProvisioning.connectToStoredNetworks()` in `setup()` to automatically connect if credentials exist.
- Call `PicoWiFiProvisioning.loop()` in your main `loop()` function to process BLE and WiFi events.

## BLE Service Definition

The library creates a custom BLE service with the following characteristics:

| Characteristic | UUID | Properties | Description |
|---------------|------|------------|-------------|
| SSID | 5a67d678-6361-4f32-8396-54c6926c8fa2 | Read, Write | WiFi SSID |
| Password | 5a67d678-6361-4f32-8396-54c6926c8fa3 | Write | WiFi Password |
| Command | 5a67d678-6361-4f32-8396-54c6926c8fa4 | Write | [Control commands](#commands) |
| Pairing Status | 5a67d678-6361-4f32-8396-54c6926c8fa5 | Read, Notify | BLE pairing status |

## Configuration

You can customize the following parameters in `PicoWiFiProvisioning.h`:

- `MAX_WIFI_NETWORKS`: Maximum number of WiFi networks that can be stored (default: 5)
- `MAX_SSID_LENGTH`: Maximum length for SSID (default: 32)
- `MAX_PASSWORD_LENGTH`: Maximum length for password (default: 64)
- `WIFI_CONFIG_FILE`: File path for storing WiFi credentials (default: "/wifi_config.json")

When calling `PicoWiFiProvisioning.begin()`, you can configure:
- `deviceName:` The name broadcasted via BLE.
- `securityLevel:` The [BLE security level](#security-levels)
- `ioCapability:` The input/output capabilities of your device for pairing 
  (see [IO Capabilities](#io-capabilities))

## Storing WiFi networks

- The library uses LittleFS to store up to `MAX_WIFI_NETWORKS` (default 5) WiFi network configurations in a file named `/wifi_config.json`. Each entry includes the SSID, password, and an enabled flag.
- `saveNetwork(ssid, password)`: Saves or updates a network in flash.
- `loadNetworksFromFlash()`: Loads networks from the configuration file on startup.
- `clearNetworks()`: Erases all stored networks and the configuration file.

## Callbacks

You can set various callbacks to react to different events:

- `setStatusCallback(void (*callback)(PicoWiFiProvisioningStatus status))`: Called when the internal provisioning status changes.
- `setWiFiStatusCallback(void (*callback)(wl_status_t status))`: Called when the underlying WiFi connection status changes.
- `setBLEConnectionStateCallback(void (*callback)(bool isConnected))`: Called when a BLE device connects or disconnects.
- `setPasskeyDisplayCallback(void (*callback)(uint32_t passkey))`: (Optional) Called when a passkey needs to be displayed during pairing.
- `setNumericComparisonCallback(void (*callback)(uint32_t passkey, BLEDevice *device))`: (Optional) Called during numeric comparison pairing. You'll need to call acceptNumericComparison(bool accept) in response.

## Commands

The following commands can be sent to the Command characteristic:

| Command | Value | Description |
|---------|-------|-------------|
| CMD_SAVE_NETWORK | 0x01 | Save the current SSID and password as a network |
| CMD_CONNECT | 0x02 | Connect to the specified network or stored networks |
| CMD_CLEAR_NETWORKS | 0x03 | Clear all stored networks |
| CMD_GET_STATUS | 0x04 | Request the current status (Partially implemented) |
| CMD_DISCONNECT | 0x05 | Disconnect from the current WiFi network |
| CMD_START_SCAN | 0x06 | Start a WiFi network scan (not fully implemented) |
| CMD_GET_SCAN_RESULTS | 0x07 | Get WiFi scan results (not fully implemented) |

## Security Levels

The library supports different security levels through the BLESecure library:

- `SECURITY_MEDIUM`: Authentication, encryption
- `SECURITY_HIGH`: Authentication, MITM protection, encryption
- `SECURITY_HIGH_SC`: Authentication, MITM protection, encryption, secure connections

## IO Capabilities

The library supports different IO capabilities through the BLESecure library:

- `IO_CAPABILITY_DISPLAY_ONLY`: Device can display a 6-digit passkey
   - e.g., OLED screen, no buttons
- `IO_CAPABILITY_DISPLAY_YES_NO`: Device can display and confirm a passkey
  - e.g., OLED screen + confirmation button
  - offers the best security through numeric comparison (MITM protection)
- `IO_CAPABILITY_KEYBOARD_ONLY`: Device has a keyboard for passkey entry
- `IO_CAPABILITY_NO_INPUT_NO_OUTPUT`: Device has no input or output capabilities
  - This falls back to "Just Works" pairing
- `IO_CAPABILITY_KEYBOARD_DISPLAY`: Device has both keyboard and display

## Mobile Applications

### Example Flutter App
A simple Flutter app is available to demonstrate the provisioning process. It allows you to connect to the Pico W device and send WiFi credentials securely.
- [pico_wifi_provisioning_flutter_app](https://github.com/IoT-gamer/pico_wifi_provisioning_flutter_app)

## Troubleshooting

- **BLE not advertising**: Ensure the arduino-pico core is configured with BLE support in your platformio.ini.
- **WiFi connection failures**: Check the SSID and password being sent. Ensure they're valid and within length limits.
- **Flash storage issues**: Make sure LittleFS is properly initialized and has enough space.
- **BLE pairing issues**: Any time bonding information is lost on either the peripheral or central device, re-encryption will fail, and the bond must be reestablished.
  - For example, if the peripheral device (Pico W) flash is erased, the bond information will also be lost. And the central device (e.g., a mobile phone) will need to unpair (in settings/Bluetooth) and re-pair with the Pico W.
  - example error message:
  ```
  Re-encryption failed, status: 61
  BLE pairing failed
  Pairing failed, status: 61, reason: 0
  ```

## License

This library is released under the MIT License. See the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.
