/**
 * main.cpp - Basic example of WiFi provisioning over BLE
 *
 * This example demonstrates how to use the PicoWiFiProvisioning library to receive
 * WiFi credentials securely over BLE and connect to a WiFi network.
 * It uses the onboard LED for visual feedback of the wifi provisioning state:
 * - Fast blink: connecting to WiFi
 * - Solid on: connected to WiFi
 * It uses optional GPIO BLE LED to indicate BLE connection state:
 * - On: BLE connected
 * - Off: BLE disconnected
 * It also implements BOOTSEL button functionality to clear WiFi networks.
 * RESET (short pin RUN to GND) will reinitialize the WiFi provisioning.
 *
 * For Raspberry Pi Pico W with arduino-pico core.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <BTstackLib.h>
#include <BLESecure.h>
#include <BLENotify.h>
#include "PicoWiFiProvisioning.h"

// Optional GPIO pin used to indicate BLE connection state with an LED
const int BLE_LED_PIN = 16; 

// Onboard LED used for visual feedback of the wifi provisioning state
const int LED_PIN = LED_BUILTIN;
const int SLOW_BLINK_MS = 1000;
const int FAST_BLINK_MS = 500;
enum LedState
{
  LED_OFF,
  LED_SLOW_BLINK,
  LED_FAST_BLINK,
  LED_SOLID_ON
};
LedState currentLedState = LED_OFF;
unsigned long lastBlinkTime = 0;
bool ledIsCurrentlyOn = false;

// Flag to track if we're connected to WiFi
bool wifiConnected = false;

// Flag to track if we're paired with a BLE device
bool blePaired = false;

// Flag to trigger a reset on next loop iteration
bool needReset = false;

// Function to update LED based on currentLedState
void updateLed()
{
  unsigned long currentTime = millis();
  switch (currentLedState)
  {
  case LED_OFF:
    if (ledIsCurrentlyOn)
    {
      digitalWrite(LED_PIN, LOW);
      ledIsCurrentlyOn = false;
    }
    break;
  case LED_SLOW_BLINK:
    if (currentTime - lastBlinkTime >= (unsigned long)(SLOW_BLINK_MS / 2))
    {
      lastBlinkTime = currentTime;
      ledIsCurrentlyOn = !ledIsCurrentlyOn;
      digitalWrite(LED_PIN, ledIsCurrentlyOn ? HIGH : LOW);
    }
    break;
  case LED_FAST_BLINK:
    if (currentTime - lastBlinkTime >= (unsigned long)(FAST_BLINK_MS / 2))
    {
      lastBlinkTime = currentTime;
      ledIsCurrentlyOn = !ledIsCurrentlyOn;
      digitalWrite(LED_PIN, ledIsCurrentlyOn ? HIGH : LOW);
    }
    break;
  case LED_SOLID_ON:
    if (!ledIsCurrentlyOn)
    {
      digitalWrite(LED_PIN, HIGH);
      ledIsCurrentlyOn = true;
    }
    break;
  }
}

void handleBleConnectionChange(bool isConnected)
{
  if (isConnected)
  {
    if (PicoWiFiProvisioning.getStatus() == PROVISION_IDLE && !wifiConnected) 
    {
      currentLedState = LED_OFF;
      // turn on BLE LED
      digitalWrite(BLE_LED_PIN, HIGH);
    }
  }
  else // BLE Disconnected
  {
      currentLedState = LED_OFF;
      // turn off BLE LED
      digitalWrite(BLE_LED_PIN, LOW);
  }
}


// Callback for WiFi status changes
void onWiFiStatus(wl_status_t status)
{
  switch (status)
  {
    case WL_CONNECTED:
      Serial.println("WiFi connected!");
      Serial.print("IP address: "); Serial.println(WiFi.localIP());
      currentLedState = LED_SOLID_ON;
      wifiConnected = true;
      break;

    case WL_DISCONNECTED:
      Serial.println("WiFi disconnected");
      if (wifiConnected) {
          currentLedState = LED_OFF;
      }
      wifiConnected = false;
      break;

    case WL_CONNECTION_LOST:
      if (wifiConnected) {
        Serial.println("WiFi disconnected or connection lost");
        currentLedState = LED_OFF;
      }
      wifiConnected = false;
      break;

    case WL_CONNECT_FAILED:
      Serial.println("WiFi connection failed");
      wifiConnected = false;
      break;
    default:
      break;
  }
}

// Callback for WiFi provisioning status changes
void onProvisionStatus(PicoWiFiProvisioningStatus status)
{
  switch (status)
  {
    case PROVISION_IDLE:
      Serial.println("Provisioning: idle");
       break;

    case PROVISION_STARTED:
      Serial.println("Provisioning: started");
      break;

    case PROVISION_CONNECTING:
      Serial.println("Provisioning: connecting to WiFi (disconnecting BLE, trying WiFi)");
      currentLedState = LED_FAST_BLINK;
      break;

    case PROVISION_FAILED:
      Serial.println("Provisioning: failed");
      currentLedState = LED_OFF; // Turn LED OFF on any provisioning failure
      break;

    case PROVISION_CONNECTED:
      Serial.println("Provisioning: connected to WiFi");
      break;

    case PROVISION_COMPLETE:
      Serial.println("Provisioning: complete");
      break;
  }
}

// Callback for pairing status changes - implements visual feedback
void onPairingStatus(BLEPairingStatus status, BLEDevice *device)
{
  bool currentPairingStatusForCharacteristic = false; // To inform the library

  if (status == PAIRING_COMPLETE)
  {
    Serial.println("BLE pairing complete, ready for WiFi provisioning");
    blePaired = true;
    currentPairingStatusForCharacteristic = true;
  }
  else if (status == PAIRING_FAILED)
  {
    Serial.println("BLE pairing failed");
    blePaired = false;
    currentPairingStatusForCharacteristic = false;
  }
  else if (status == PAIRING_STARTED)
  {
    Serial.println("BLE pairing started");
  }
  else if (status == PAIRING_IDLE)
  {
    blePaired = false;
    currentPairingStatusForCharacteristic = false;
  }

  // IMPORTANT: Notify the PicoWiFiProvisioning library to update its characteristic
  PicoWiFiProvisioning.updatePairingStatusCharacteristic(currentPairingStatusForCharacteristic); 

}

// Function to initialize the WiFi provisioning
bool startProvisioning()
{
  if (PicoWiFiProvisioning.begin("PicoWiFi", SECURITY_MEDIUM, IO_CAPABILITY_NO_INPUT_NO_OUTPUT))
  {
    // Serial.println("WiFi provisioning service started");
    return true;
  }
  else
  {
    Serial.println("Failed to start WiFi provisioning service");
    return false;
  }
}

void setup()
{
  // Initialize serial for debugging
  Serial.begin(115200);
  while (!Serial)
    delay(10); // Wait for serial to initialize
  delay(1000); // Give time for serial to initialize
  Serial.println("WiFi Provisioning Example");

  // Initialize LEDs
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  currentLedState = LED_OFF;
  pinMode(BLE_LED_PIN, OUTPUT);
  digitalWrite(BLE_LED_PIN, LOW);

  // Set callbacks before initialization
  PicoWiFiProvisioning.setBLEConnectionStateCallback(handleBleConnectionChange);
  PicoWiFiProvisioning.setWiFiStatusCallback(onWiFiStatus);
  PicoWiFiProvisioning.setStatusCallback(onProvisionStatus);

  // Set pairing status callback directly to BLESecure
  BLESecure.setPairingStatusCallback(onPairingStatus);

  // Initialize WiFi provisioning
  if (startProvisioning())
  {
    // Try to connect to any stored WiFi networks
    Serial.println("Attempting to connect to stored networks...");
    if (PicoWiFiProvisioning.connectToStoredNetworks())
    {
      Serial.println("Connected to a stored network!");
    }
    else
    {
      Serial.println("No stored networks or failed to connect");
      Serial.println("Waiting for BLE connection to provision WiFi...");
    }
  }

  Serial.println("Press BOOTSEL button to clear all WiFi networks");
  Serial.println("RESET (short pin RUN to GND) to reinitialize WiFi provisioning");
}

void loop()
{
  // Process WiFi and BLE events
  PicoWiFiProvisioning.loop();

  // Check if we need to perform a reset after BOOTSEL was pressed
  if (needReset)
  {
    needReset = false;

    Serial.println("Performing full reset and reinitialization...");

    // Disconnect WiFi first - this is important because it will call WiFi.end()
    // which resets the CYW43 chip
    Serial.println("Disconnecting WiFi...");
    WiFi.disconnect();
    delay(500); // Give time for WiFi to fully disconnect
    wifiConnected = false;
    currentLedState = LED_OFF;

    // Clear networks
    bool cleared = PicoWiFiProvisioning.clearNetworks();
    Serial.println(cleared ? "Networks cleared successfully" : "Failed to clear networks");

    Serial.println("RESET pico (short RUN pin to GND) to reinitialize WiFi provisioning");
  
    // Blink LED to signal reset
    for (int i = 0; i < 5; i++)
    {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
  }

  // Check BOOTSEL button state with debouncing
  static unsigned long lastButtonPressTime = 0;
  static bool buttonPressed = false;
  const unsigned long debounceDelay = 300;

  if (BOOTSEL)
  {
    unsigned long currentTime = millis();

    if (!buttonPressed && (currentTime - lastButtonPressTime > debounceDelay))
    {
      buttonPressed = true;
      lastButtonPressTime = currentTime;
      Serial.println("BOOTSEL pressed: disconnecting WiFi and clearing networks");

      // Set the flag for reset on next loop iteration
      needReset = true;
    }

    // Wait for button release to avoid multiple triggers
    while (BOOTSEL)
    {
      delay(10);
    }
    buttonPressed = false;
  }

  // Print signal strength every 10 seconds if connected
  static unsigned long lastRssiPrint = 0;
  if (wifiConnected && millis() - lastRssiPrint > 10000)
  {
    Serial.print("WiFi signal strength (RSSI): ");
    Serial.print(PicoWiFiProvisioning.getRSSI());
    Serial.println(" dBm");
    lastRssiPrint = millis();
  }

  updateLed();

  delay(10);
}