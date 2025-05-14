/**
 * PicoWiFiProvisioning.cpp - Library for WiFi provisioning over BLE for Raspberry Pi Pico W
 *
 * Implementation file for the PicoWiFiProvisioning library.
 */

#include "PicoWiFiProvisioning.h"
#include <ArduinoJson.h>

// Define the UUIDs for service and characteristics
static const char *SERVICE_UUID = "5a67d678-6361-4f32-8396-54c6926c8fa1";
static const char *SSID_CHAR_UUID = "5a67d678-6361-4f32-8396-54c6926c8fa2";
static const char *PASSWORD_CHAR_UUID = "5a67d678-6361-4f32-8396-54c6926c8fa3";
static const char *COMMAND_CHAR_UUID = "5a67d678-6361-4f32-8396-54c6926c8fa4";
static const char *PAIRING_STATUS_CHAR_UUID = "5a67d678-6361-4f32-8396-54c6926c8fa5";

// Global instance
PicoWiFiProvisioningClass PicoWiFiProvisioning;

// Forward declare the global callbacks
void bleDeviceConnected(BLEStatus status, BLEDevice *device);
void bleDeviceDisconnected(BLEDevice *device);
int gattWriteCallback(uint16_t characteristic_id, uint8_t *buffer, uint16_t buffer_size);
uint16_t gattReadCallback(uint16_t characteristic_id, uint8_t *buffer, uint16_t buffer_size);

void onPairingStatusChange(BLEPairingStatus status, BLEDevice *device); // For BLESecure callback

// Constructor
PicoWiFiProvisioningClass::PicoWiFiProvisioningClass() : _status(PROVISION_IDLE),
                                                         _networkCount(0),
                                                         _statusCallback(nullptr),
                                                         _wifiStatusCallback(nullptr),
                                                         _bleConnectionStateCallback(nullptr),
                                                         _serviceUUID(SERVICE_UUID),
                                                         _ssidCharUUID(SSID_CHAR_UUID),
                                                         _passwordCharUUID(PASSWORD_CHAR_UUID),
                                                         _commandCharUUID(COMMAND_CHAR_UUID),
                                                         _pairingStatusCharUUID(PAIRING_STATUS_CHAR_UUID),
                                                         _ssidCharHandle(0),
                                                         _passwordCharHandle(0),
                                                         _commandCharHandle(0),
                                                         _pairingStatusCharHandle(0),
                                                         _allowProvisioningWhenConnected(false),
                                                         _connectedDevice(nullptr),
                                                         _connectionStartTime(0) // Initialized from pico_repo_3.txt
{
    // Initialize string buffers
    memset(_receivedSSID, 0, sizeof(_receivedSSID));
    memset(_receivedPassword, 0, sizeof(_receivedPassword));

    // Initialize networks array
    for (int i = 0; i < MAX_WIFI_NETWORKS; i++)
    {
        memset(_networks[i].ssid, 0, sizeof(_networks[i].ssid));
        memset(_networks[i].password, 0, sizeof(_networks[i].password));
        _networks[i].enabled = false;
    }
}

// Global callback for pairing status updates from BLESecure
void onPairingStatusChange(BLEPairingStatus status, BLEDevice *device)
{
    // This callback is registered with BLESecure.
    // PicoWiFiProvisioning's own updatePairingStatusCharacteristic can be called
    // by BLESecure's callback handler in main.cpp or here if preferred.
    if (status == PAIRING_COMPLETE)
    {
        Serial.println("BLE pairing complete, ready for WiFi provisioning (via onPairingStatusChange)");
        PicoWiFiProvisioning.updatePairingStatusCharacteristic(true);
    }
    else if (status == PAIRING_FAILED)
    {
        Serial.println("BLE pairing failed (via onPairingStatusChange)");
        PicoWiFiProvisioning.updatePairingStatusCharacteristic(false);
    }
}

// Initialize the WiFi provisioning service
bool PicoWiFiProvisioningClass::begin(const char *deviceName, BLESecurityLevel securityLevel, io_capability_t ioCapability)
{
    if (!LittleFS.begin())
    {
        Serial.println("Failed to initialize LittleFS");
        return false;
    }
    loadNetworksFromFlash();
    BLENotify.begin();
    BTstack.setup(deviceName);
    BLESecure.begin(ioCapability);
    BLESecure.setSecurityLevel(securityLevel, true);
    BLESecure.allowReconnectionWithoutDatabaseEntry(true);
    BLESecure.requestPairingOnConnect(true); // Auto-request pairing
    BLESecure.setBLEDeviceConnectedCallback(bleDeviceConnected);
    BLESecure.setBLEDeviceDisconnectedCallback(bleDeviceDisconnected);
    BTstack.setGATTCharacteristicWrite(gattWriteCallback);
    BTstack.setGATTCharacteristicRead(gattReadCallback);
    setupBLEService();
    BLESecure.setPairingStatusCallback(onPairingStatusChange); // Use the global onPairingStatusChange
    BTstack.startAdvertising();
    Serial.println("WiFi Provisioning service started");
    return true;
}

// Process BLE and WiFi events
void PicoWiFiProvisioningClass::loop()
{
    BTstack.loop();
    BLENotify.update();

    wl_status_t currentWiFiStatus = (wl_status_t)WiFi.status();
    static wl_status_t lastReportedWiFiStatusToApp = WL_NO_SHIELD;

    if (_status == PROVISION_CONNECTING)
    {
        unsigned long currentTime = millis();
        if (currentWiFiStatus == WL_CONNECTED)
        {
            Serial.println("WiFi connected! (Detected in library loop's PROVISION_CONNECTING block)");
            setStatus(PROVISION_CONNECTED);
        }
        else if (currentWiFiStatus == WL_CONNECT_FAILED ||
                 currentWiFiStatus == WL_NO_SSID_AVAIL)
        {
            Serial.print("WiFi connection failed (Reported by WiFi stack in PROVISION_CONNECTING block): ");
            Serial.println(currentWiFiStatus);
            setStatus(PROVISION_FAILED);
        }
        else if (currentTime - _connectionStartTime > WIFI_CONNECT_TIMEOUT_MS)
        {
            Serial.println("WiFi connection timed out. (Inside PROVISION_CONNECTING block timeout condition)");
            setStatus(PROVISION_FAILED);
            WiFi.disconnect(); // Explicitly stop the WiFi connection attempt on timeout
        }
    }

    if (currentWiFiStatus != lastReportedWiFiStatusToApp)
    {
        if (_wifiStatusCallback)
        {
            _wifiStatusCallback(currentWiFiStatus);
        }
        lastReportedWiFiStatusToApp = currentWiFiStatus;
    }

    static wl_status_t _internalLastWiFiStateForGeneralChanges = WL_NO_SHIELD;
    if (currentWiFiStatus != _internalLastWiFiStateForGeneralChanges)
    {
        switch (currentWiFiStatus)
        {
        case WL_DISCONNECTED:
        case WL_CONNECTION_LOST:
            if (_status == PROVISION_CONNECTED)
            {
                Serial.println("WiFi connection lost (Detected post-connection in library loop).");
                setStatus(PROVISION_IDLE);
            }
            break;
        }
        _internalLastWiFiStateForGeneralChanges = currentWiFiStatus;
    }
}

void PicoWiFiProvisioningClass::updatePairingStatusCharacteristic(bool isPaired)
{
    uint8_t pairingStatus = isPaired ? PAIRING_STATUS_PAIRED : PAIRING_STATUS_NOT_PAIRED;
    if (BLENotify.isSubscribed(_pairingStatusCharHandle))
    {
        BLENotify.notify(_pairingStatusCharHandle, &pairingStatus, 1);
        Serial.print("Sent pairing status update (from lib): ");
        Serial.println(pairingStatus);
    }
}

bool PicoWiFiProvisioningClass::saveNetwork(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0)
    {
        return false;
    }
    int existingIndex = -1;
    for (int i = 0; i < _networkCount; i++)
    {
        if (strcmp(_networks[i].ssid, ssid) == 0)
        {
            existingIndex = i;
            break;
        }
    }
    if (existingIndex >= 0)
    {
        strncpy(_networks[existingIndex].password, password, MAX_PASSWORD_LENGTH);
        _networks[existingIndex].enabled = true;
    }
    else if (_networkCount < MAX_WIFI_NETWORKS)
    {
        strncpy(_networks[_networkCount].ssid, ssid, MAX_SSID_LENGTH);
        strncpy(_networks[_networkCount].password, password, MAX_PASSWORD_LENGTH);
        _networks[_networkCount].enabled = true;
        _networkCount++;
    }
    else
    {
        return false; // No room
    }
    return saveNetworksToFlash();
}

bool PicoWiFiProvisioningClass::connectToStoredNetworks()
{
    if (_status == PROVISION_CONNECTING || _status == PROVISION_CONNECTED)
    {
        return false;
    }
    if (_networkCount == 0)
    {
        return false;
    }
    for (int i = 0; i < _networkCount; i++)
    {
        if (_networks[i].enabled)
        {
            Serial.print("Attempting to connect to stored network (async): ");
            Serial.println(_networks[i].ssid);
            connectToNetwork(_networks[i].ssid, _networks[i].password);
            if (_status == PROVISION_CONNECTING)
            { // Check if connectToNetwork initiated an attempt
                return true;
            }
            else
            {
                return false; // connectToNetwork did not start for some reason (e.g. empty SSID internally)
            }
        }
    }
    return false;
}

void PicoWiFiProvisioningClass::connectToNetwork(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0)
    {
        Serial.println("connectToNetwork: SSID is empty, connection attempt aborted.");
        return;
    }

    setStatus(PROVISION_CONNECTING);
    Serial.print("Attempting to connect to WiFi network (async): ");
    Serial.println(ssid);

    Serial.println("Stopping BLE advertising during WiFi connection");
    BTstack.stopAdvertising();

    if (_connectedDevice != nullptr)
    {
        Serial.println("Disconnecting BLE device before WiFi connection");
        BTstack.bleDisconnect(_connectedDevice);
        // Note: _connectedDevice cleared in handleDeviceDisconnected
    }

    Serial.println("Proceeding to WiFi operations after BLE shutdown pause.");

    if (WiFi.status() != WL_DISCONNECTED && WiFi.status() != WL_IDLE_STATUS)
    {
        Serial.println("Disconnecting from current WiFi network first...");
        WiFi.disconnect();
        // No delay here, WiFi.begin() will override/manage.
    }

    WiFi.begin(ssid, password);
    _connectionStartTime = millis();
}

bool PicoWiFiProvisioningClass::clearNetworks()
{
    for (int i = 0; i < MAX_WIFI_NETWORKS; i++)
    {
        memset(_networks[i].ssid, 0, sizeof(_networks[i].ssid));
        memset(_networks[i].password, 0, sizeof(_networks[i].password));
        _networks[i].enabled = false;
    }
    _networkCount = 0;
    if (LittleFS.exists(WIFI_CONFIG_FILE))
    {
        LittleFS.remove(WIFI_CONFIG_FILE);
    }
    return true;
}

uint8_t PicoWiFiProvisioningClass::getNetworkCount()
{
    return _networkCount;
}

PicoWiFiProvisioningStatus PicoWiFiProvisioningClass::getStatus()
{
    return _status;
}

// Set the current status and call the callback if registered
void PicoWiFiProvisioningClass::setStatus(PicoWiFiProvisioningStatus newStatus)
{
    if (_status != newStatus)
    {
        // Serial.print(millis());                                                // DEBUG
        // Serial.print("ms: PicoWiFiProvisioningClass::_status changing from "); // DEBUG
        // Serial.print(_status);                                                 // DEBUG
        // Serial.print(" to ");                                                  // DEBUG
        // Serial.println(newStatus);                                             // DEBUG
        _status = newStatus;
        if (_statusCallback)
        {
            _statusCallback(_status);
        }
    }
    else
    {
        // Optional: Log if setStatus is called but status doesn't change
        // Serial.print(millis()); // DEBUG
        // Serial.print("ms: PicoWiFiProvisioningClass::setStatus called with same status: "); // DEBUG
        // Serial.println(newStatus); // DEBUG
    }
}

// --------------- ADDED MISSING DEFINITION ---------------
// Set callback for status changes
void PicoWiFiProvisioningClass::setStatusCallback(void (*callback)(PicoWiFiProvisioningStatus status))
{
    _statusCallback = callback;
}
// -------------------------------------------------------

void PicoWiFiProvisioningClass::setWiFiStatusCallback(void (*callback)(wl_status_t status))
{
    _wifiStatusCallback = callback;
}

void PicoWiFiProvisioningClass::setBLEConnectionStateCallback(void (*callback)(bool isConnected))
{
    _bleConnectionStateCallback = callback;
}

void PicoWiFiProvisioningClass::setPasskeyDisplayCallback(void (*callback)(uint32_t passkey))
{
    BLESecure.setPasskeyDisplayCallback(callback);
}

void PicoWiFiProvisioningClass::setNumericComparisonCallback(void (*callback)(uint32_t passkey, BLEDevice *device))
{
    BLESecure.setNumericComparisonCallback(callback);
}

void PicoWiFiProvisioningClass::acceptNumericComparison(bool accept)
{
    BLESecure.acceptNumericComparison(accept);
}

void PicoWiFiProvisioningClass::allowProvisioningWhenConnected(bool allow)
{
    _allowProvisioningWhenConnected = allow;
}

int32_t PicoWiFiProvisioningClass::getRSSI()
{
    return WiFi.RSSI();
}

// BTstack global callback Trampolines
void bleDeviceConnected(BLEStatus status, BLEDevice *device)
{
    PicoWiFiProvisioning.handleDeviceConnected(status, device);
}

void bleDeviceDisconnected(BLEDevice *device)
{
    PicoWiFiProvisioning.handleDeviceDisconnected(device);
}

int gattWriteCallback(uint16_t characteristic_id, uint8_t *buffer, uint16_t buffer_size)
{
    return PicoWiFiProvisioning.handleGattWrite(characteristic_id, buffer, buffer_size);
}

uint16_t gattReadCallback(uint16_t characteristic_id, uint8_t *buffer, uint16_t buffer_size)
{
    return PicoWiFiProvisioning.handleGattRead(characteristic_id, buffer, buffer_size);
}

// Class member implementations for BLE events
void PicoWiFiProvisioningClass::handleDeviceConnected(BLEStatus status, BLEDevice *device)
{
    if (status == BLE_STATUS_OK)
    {
        Serial.println("BLE Device connected");
        _connectedDevice = device;
        if (_bleConnectionStateCallback)
        {
            _bleConnectionStateCallback(true);
        }
        if (_status == PROVISION_CONNECTED && !_allowProvisioningWhenConnected)
        {
            Serial.println("Already connected to WiFi. Further BLE provisioning may be restricted.");
        }
    }
    else
    {
        Serial.print("BLE Connection attempt failed or ended with status: ");
        Serial.println(status);
        _connectedDevice = nullptr;
        if (_bleConnectionStateCallback)
        {
            _bleConnectionStateCallback(false);
        }
    }
}

void PicoWiFiProvisioningClass::handleDeviceDisconnected(BLEDevice *device)
{
    Serial.println("BLE Device disconnected");
    updatePairingStatusCharacteristic(false); // Pairing is lost/invalid on disconnect
    _connectedDevice = nullptr;
    BLENotify.handleDisconnection();
    if (_bleConnectionStateCallback)
    {
        _bleConnectionStateCallback(false);
    }
}

int PicoWiFiProvisioningClass::handleGattWrite(uint16_t characteristic_id, uint8_t *buffer, uint16_t buffer_size)
{
    if (characteristic_id == _ssidCharHandle)
    {
        memset(_receivedSSID, 0, sizeof(_receivedSSID));
        size_t copyLen = min((size_t)buffer_size, (size_t)MAX_SSID_LENGTH);
        memcpy(_receivedSSID, buffer, copyLen);
        Serial.print("Received SSID: ");
        Serial.println(_receivedSSID);
    }
    else if (characteristic_id == _passwordCharHandle)
    {
        memset(_receivedPassword, 0, sizeof(_receivedPassword));
        size_t copyLen = min((size_t)buffer_size, (size_t)MAX_PASSWORD_LENGTH);
        memcpy(_receivedPassword, buffer, copyLen);
        Serial.println("Received password");
    }
    else if (characteristic_id == _commandCharHandle && buffer_size >= 1)
    {
        uint8_t command = buffer[0];
        processCommand(command);
    }

    // Handle CCCD writes for notifications
    if (buffer_size == 2)
    { // CCCD is 2 bytes
        // The characteristic_id in gattWriteCallback for a CCCD write is the handle of the CCCD itself.
        // The characteristic's value handle is usually CCCD_handle - 1.
        uint16_t char_value_handle = characteristic_id - 1;
        uint16_t cccd_value = (buffer[1] << 8) | buffer[0];

        if (char_value_handle == _pairingStatusCharHandle)
        { // Check if this CCCD belongs to pairingStatusChar
            if (cccd_value == 0x0001)
            { // Notifications enabled
                BLENotify.handleSubscriptionChange(_pairingStatusCharHandle, true);
                Serial.println("Pairing status notifications enabled by client");
                bool isPaired = (_connectedDevice && BLESecure.getPairingStatus() == PAIRING_COMPLETE && BLESecure.isEncrypted(_connectedDevice));
                updatePairingStatusCharacteristic(isPaired); // Send current status
            }
            else if (cccd_value == 0x0000)
            { // Notifications disabled
                BLENotify.handleSubscriptionChange(_pairingStatusCharHandle, false);
                Serial.println("Pairing status notifications disabled by client");
            }
        }
        // Add similar blocks for other characteristics if they have CCCDs and need handling
    }
    return 0;
}

uint16_t PicoWiFiProvisioningClass::handleGattRead(uint16_t characteristic_id, uint8_t *buffer, uint16_t buffer_size)
{
    if (characteristic_id == _ssidCharHandle)
    {
        // Provide the value of the _receivedSSID.
        // This reflects the SSID last written by the client during provisioning.
        size_t len = strlen(_receivedSSID);

        if (buffer == NULL)
        { // Client is asking for the length only
            return len;
        }

        if (buffer_size < len)
        {
            // Provided buffer is too small.
            // You might want to log an error or handle it differently.
            // For now, returning 0, which is an error for ATT reads.
            Serial.println("Error: Buffer too small for SSID read.");
            return 0;
        }

        memcpy(buffer, _receivedSSID, len);
        // Serial.print("Read SSID: "); Serial.println(_receivedSSID); // Optional: for debugging
        return len;
    }
    else if (characteristic_id == _pairingStatusCharHandle)
    {
        // Provide the current pairing status. Uses enum PairingStatusCodes from .h
        uint8_t pairingStatusValue = PAIRING_STATUS_NOT_PAIRED; // Default to not paired

        // Check current pairing status with the connected device
        if (_connectedDevice && BLESecure.getPairingStatus() == PAIRING_COMPLETE && BLESecure.isEncrypted(_connectedDevice))
        {
            pairingStatusValue = PAIRING_STATUS_PAIRED; //
        }

        if (buffer == NULL)
        { // Client is asking for the length only
            return sizeof(pairingStatusValue);
        }

        if (buffer_size < sizeof(pairingStatusValue))
        {
            Serial.println("Error: Buffer too small for Pairing Status read.");
            return 0;
        }

        buffer[0] = pairingStatusValue;
        // Serial.print("Read Pairing Status: "); Serial.println(pairingStatusValue); // Optional: for debugging
        return sizeof(pairingStatusValue);
    }

    // If the characteristic_id is not handled by this function, return 0.
    // This indicates to the stack that this callback did not provide data for this handle.
    return 0;
}

bool PicoWiFiProvisioningClass::loadNetworksFromFlash()
{
    if (!LittleFS.exists(WIFI_CONFIG_FILE))
    {
        Serial.println("No WiFi configuration file found");
        return false;
    }
    File configFile = LittleFS.open(WIFI_CONFIG_FILE, "r");
    if (!configFile)
    {
        Serial.println("Failed to open WiFi configuration file");
        return false;
    }
    size_t fileSize = configFile.size();
    if (fileSize > 2048)
    { // Basic sanity check for file size
        Serial.println("WiFi configuration file is too large");
        configFile.close();
        return false;
    }
    JsonDocument doc; // Using ArduinoJson V7 syntax, if applicable, else adjust for V6
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();
    if (error)
    {
        Serial.print("Failed to parse WiFi configuration: ");
        Serial.println(error.c_str());
        return false;
    }
    JsonArray networksArray = doc["networks"].as<JsonArray>();
    _networkCount = 0;
    for (JsonObject network : networksArray)
    {
        if (_networkCount >= MAX_WIFI_NETWORKS)
            break;
        const char *ssid = network["ssid"];
        const char *password = network["password"];
        bool enabled = network["enabled"] | true; // Default to true if missing
        if (ssid && strlen(ssid) > 0)
        {
            strncpy(_networks[_networkCount].ssid, ssid, MAX_SSID_LENGTH);
            if (password)
            {
                strncpy(_networks[_networkCount].password, password, MAX_PASSWORD_LENGTH);
            }
            else
            {
                _networks[_networkCount].password[0] = '\0';
            }
            _networks[_networkCount].enabled = enabled;
            _networkCount++;
        }
    }
    Serial.print("Loaded ");
    Serial.print(_networkCount);
    Serial.println(" WiFi networks from flash");
    return true;
}

bool PicoWiFiProvisioningClass::saveNetworksToFlash()
{
    JsonDocument doc;
    JsonArray networksArray = doc["networks"].to<JsonArray>();
    for (int i = 0; i < _networkCount; i++)
    {
        JsonObject network = networksArray.add<JsonObject>();
        network["ssid"] = _networks[i].ssid;
        network["password"] = _networks[i].password;
        network["enabled"] = _networks[i].enabled;
    }
    File configFile = LittleFS.open(WIFI_CONFIG_FILE, "w");
    if (!configFile)
    {
        Serial.println("Failed to open WiFi configuration file for writing");
        return false;
    }
    if (serializeJson(doc, configFile) == 0)
    {
        Serial.println("Failed to write WiFi configuration to file");
        configFile.close();
        return false;
    }
    configFile.close();
    Serial.println("WiFi networks saved to flash");
    return true;
}

void PicoWiFiProvisioningClass::setupBLEService()
{
    BTstack.addGATTService(&_serviceUUID);
    _ssidCharHandle = BLENotify.addNotifyCharacteristic(
        &_ssidCharUUID, ATT_PROPERTY_READ | ATT_PROPERTY_WRITE);
    _passwordCharHandle = BLENotify.addNotifyCharacteristic(
        &_passwordCharUUID, ATT_PROPERTY_WRITE);
    _commandCharHandle = BLENotify.addNotifyCharacteristic(
        &_commandCharUUID, ATT_PROPERTY_WRITE);
    _pairingStatusCharHandle = BLENotify.addNotifyCharacteristic(
        &_pairingStatusCharUUID, ATT_PROPERTY_READ | ATT_PROPERTY_NOTIFY);
    updatePairingStatusCharacteristic(false); // Initial status
    Serial.println("BLE service and characteristics set up");
}

void PicoWiFiProvisioningClass::processCommand(uint8_t command)
{
    Serial.print("Received command: 0x");
    Serial.println(command, HEX);
    switch (command)
    {
    case CMD_SAVE_NETWORK:
        if (strlen(_receivedSSID) > 0)
        {
            if (saveNetwork(_receivedSSID, _receivedPassword))
            {
                Serial.println("Network saved successfully");
            }
            else
            {
                Serial.println("Failed to save network");
            }
            memset(_receivedSSID, 0, sizeof(_receivedSSID));
            memset(_receivedPassword, 0, sizeof(_receivedPassword));
        }
        break;
    case CMD_CONNECT:
        if (strlen(_receivedSSID) > 0)
        {
            connectToNetwork(_receivedSSID, _receivedPassword);
        }
        else
        {
            connectToStoredNetworks();
        }
        break;
    case CMD_CLEAR_NETWORKS:
        clearNetworks();
        Serial.println("All WiFi networks cleared.");
        break;
    case CMD_DISCONNECT:
        WiFi.disconnect();
        setStatus(PROVISION_IDLE); // Revert to idle after explicit disconnect command
        Serial.println("WiFi disconnect command processed.");
        break;
    // CMD_GET_STATUS, CMD_START_SCAN, CMD_GET_SCAN_RESULTS are not fully implemented here
    default:
        Serial.println("Unknown command received.");
        break;
    }
}