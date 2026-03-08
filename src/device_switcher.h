#pragma once

#if defined(WIN32) || defined(__WIN32__) || defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#endif

#include <set>
#include <shared_mutex>
#include <chrono>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cctype>
#include <functional>


/* Forward declaration for TS3 API */
extern "C" {
    extern struct TS3Functions ts3Functions;
}

/* TS3 Sound Device Constants */
#ifndef PLAYBACK
#define PLAYBACK 0
#endif
#ifndef CAPTURE
#define CAPTURE 1
#endif

/**
 * @brief Helper class to manage WASAPI device enumeration and notification
 */
class AudioDeviceListener {
private:
    bool m_comInitialized = false;

public:
    AudioDeviceListener() {
        // Initialize COM for this thread
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) {
            m_comInitialized = true;
        }
    }

    ~AudioDeviceListener() {
        if (m_comInitialized) {
            CoUninitialize();
        }
    }

    /**
     * @brief Get the name of the default playback device
     * Reinitializes enumerator each time to detect device changes
     * @return Device name as string, empty if failed
     */
    std::string getDefaultPlaybackDeviceName() {
        std::string result;
#if defined(WIN32) || defined(__WIN32__) || defined(_WIN32)
        if (!m_comInitialized) {
            return result;
        }

        // Recreate device enumerator each time to detect changes
        IMMDeviceEnumerator* deviceEnumerator = nullptr;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER,
                                      __uuidof(IMMDeviceEnumerator), (void**)&deviceEnumerator);

        if (FAILED(hr) || !deviceEnumerator) {
            return result;
        }

        IMMDevice* defaultDevice = nullptr;
        hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);

        if (SUCCEEDED(hr) && defaultDevice) {
            IPropertyStore* propertyStore = nullptr;
            hr = defaultDevice->OpenPropertyStore(STGM_READ, &propertyStore);

            if (SUCCEEDED(hr) && propertyStore) {
                PROPVARIANT varName;
                PropVariantInit(&varName);

                hr = propertyStore->GetValue(PKEY_Device_FriendlyName, &varName);
                if (SUCCEEDED(hr)) {
                    // Convert wide string to UTF-8
                    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, NULL, 0, NULL, NULL);
                    if (sizeNeeded > 0) {
                        result.resize(sizeNeeded - 1);
                        WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, &result[0], sizeNeeded, NULL, NULL);
                    }
                }
                PropVariantClear(&varName);
                propertyStore->Release();
            }
            defaultDevice->Release();
        }

        deviceEnumerator->Release();
#endif
        return result;
    }
};

/**
 * @brief Main audio device manager
 * Manages active TS3 connections and coordinates device switching
 */
class AudioDeviceManager {
private:
    std::set<uint64_t> m_activeConnections;
    mutable std::shared_mutex m_connectionMutex;

    std::chrono::steady_clock::time_point m_lastSwitchTime;
    static constexpr int DEBOUNCE_MS = 500;

    std::unique_ptr<AudioDeviceListener> m_deviceListener;
    bool m_enabled = false;
    std::string m_lastSwitchedDevice;  // Track last switched device for logging
    std::string m_lastDetectedSystemDevice;  // Track last detected system device for change detection
    std::string m_lastUsedModeID;  // Track the last used modeID for device switching

    std::chrono::steady_clock::time_point m_lastCheckTime;
    static constexpr int CHECK_INTERVAL_MS = 500;  // Poll interval for device change detection

public:
    AudioDeviceManager() : m_deviceListener(std::make_unique<AudioDeviceListener>()) {
        m_lastSwitchTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(DEBOUNCE_MS + 1);
        m_lastCheckTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(CHECK_INTERVAL_MS + 1);
    }

    ~AudioDeviceManager() {
    }

    /**
     * @brief Enable/disable automatic device switching
     */
    void setEnabled(bool enabled) {
        m_enabled = enabled;
    }

    bool isEnabled() const {
        return m_enabled;
    }

    /**
     * @brief Register a new active server connection
     */
    void registerConnection(uint64_t serverConnectionHandlerID) {
        if (serverConnectionHandlerID == 0) return;

        std::unique_lock<std::shared_mutex> lock(m_connectionMutex);
        m_activeConnections.insert(serverConnectionHandlerID);
    }

    /**
     * @brief Unregister a server connection
     */
    void unregisterConnection(uint64_t serverConnectionHandlerID) {
        std::unique_lock<std::shared_mutex> lock(m_connectionMutex);
        m_activeConnections.erase(serverConnectionHandlerID);
    }

    /**
     * @brief Get all active server connections
     */
    std::vector<uint64_t> getActiveConnections() const {
        std::shared_lock<std::shared_mutex> lock(m_connectionMutex);
        return std::vector<uint64_t>(m_activeConnections.begin(), m_activeConnections.end());
    }

    /**
     * @brief Check if enough time has passed since last switch (debouncing)
     */
    bool canSwitch() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastSwitchTime);
        return elapsed.count() >= DEBOUNCE_MS;
    }

    /**
     * @brief Get the current default playback device name from system
     */
    std::string getSystemDefaultDevice() const {
        if (m_deviceListener) {
            return m_deviceListener->getDefaultPlaybackDeviceName();
        }
        return {};
    }

    /**
     * @brief Update last switch timestamp
     */
    void updateSwitchTime() {
        m_lastSwitchTime = std::chrono::steady_clock::now();
    }

    /**
     * @brief Get the last switched device name
     */
    std::string getLastSwitchedDevice() const {
        return m_lastSwitchedDevice;
    }

    /**
     * @brief Update last switched device name
     */
    void setLastSwitchedDevice(const std::string& deviceName) {
        m_lastSwitchedDevice = deviceName;
    }

    /**
     * @brief Check if system default device has changed and needs switching
     * Polls by getting current system device and comparing with last detected device
     * @return true if a device change was detected
     */
    bool checkAndSwitchIfDeviceChanged() {
        if (!m_enabled) {
            return false;
        }

        // Check if enough time has passed since last check (polling interval)
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastCheckTime);
        if (elapsed.count() < CHECK_INTERVAL_MS) {
            return false;
        }

        m_lastCheckTime = now;

        // Get current system device (this reinitializes WASAPI objects)
        std::string currentDevice = getSystemDefaultDevice();

        // If we can't get device info, can't detect changes
        if (currentDevice.empty()) {
            return false;
        }

        // Check if device has changed
        if (m_lastDetectedSystemDevice != currentDevice) {
            m_lastDetectedSystemDevice = currentDevice;

            // Check if enough time has passed since last switch (debounce)
            auto switchElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastSwitchTime);
            if (switchElapsed.count() >= DEBOUNCE_MS) {
                return true;
            }
        }

        return false;
    }

    /**
     * @brief Get the last used modeID
     */
    std::string getLastUsedModeID() const {
        return m_lastUsedModeID;
    }

    /**
     * @brief Update last used modeID (call this after a device switch)
     */
    void updateLastUsedModeID(const char* modeID) {
        if (modeID) {
            m_lastUsedModeID = modeID;
        }
    }

    /**
     * @brief Get current system device name
     */
    std::string getCurrentSystemDevice() const {
        if (m_deviceListener) {
            return m_deviceListener->getDefaultPlaybackDeviceName();
        }
        return {};
    }

    /**
     * @brief Initialize and log the default device at startup
     */
    void initializeAndLogDefaultDevice(std::function<void(const std::string&)> logCallback = nullptr) {
        std::string initialDevice = getCurrentSystemDevice();
        if (!initialDevice.empty()) {
            m_lastDetectedSystemDevice = initialDevice;
            if (logCallback) {
                logCallback("Default playback device detected: [" + initialDevice + "]");
            }
        } else {
            if (logCallback) {
                logCallback("Warning: Could not detect default playback device on startup");
            }
        }
    }
};

/**
 * @brief Convert string to lowercase for case-insensitive comparison
 */
inline std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

/**
 * @brief Check if two strings partially match (case-insensitive)
 */
inline bool isPartialMatch(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;

    std::string lower_a = toLower(a);
    std::string lower_b = toLower(b);

    return lower_a.find(lower_b) != std::string::npos ||
           lower_b.find(lower_a) != std::string::npos;
}

/**
 * @brief Find matching TS3 playback device from system device name
 * Strategy: exact match -> partial match -> first device
 */
inline std::string findMatchingDevice(const std::string& systemDeviceName, const char* modeID) {
    if (systemDeviceName.empty() || !modeID) {
        return {};
    }

    char*** deviceListPtr = nullptr;
    unsigned int error = ts3Functions.getPlaybackDeviceList(modeID, &deviceListPtr);

    if (error != ERROR_ok || !deviceListPtr || !*deviceListPtr) {
        return {};
    }

    char** deviceList = *deviceListPtr;
    std::string result;

    // Strategy 1: Exact match
    for (int i = 0; deviceList[i]; i++) {
        if (systemDeviceName == deviceList[i]) {
            result = deviceList[i];
            ts3Functions.freeMemory(deviceList);
            return result;
        }
    }

    // Strategy 2: Partial match (case-insensitive)
    for (int i = 0; deviceList[i]; i++) {
        if (isPartialMatch(systemDeviceName, deviceList[i])) {
            result = deviceList[i];
            ts3Functions.freeMemory(deviceList);
            return result;
        }
    }

    // Strategy 3: Default to first device
    if (deviceList[0]) {
        result = deviceList[0];
    }

    ts3Functions.freeMemory(deviceList);
    return result;
}

/**
 * @brief Switch playback device for all active connections
 */
inline void switchPlaybackDeviceForAllConnections(
    AudioDeviceManager* manager,
    const std::string& systemDevice,
    const char* modeID,
    std::function<void(const std::string&)> logCallback = nullptr)
{
    if (!manager || !manager->isEnabled() || !manager->canSwitch()) {
        return;
    }

    std::string tsDevice = findMatchingDevice(systemDevice, modeID);
    if (tsDevice.empty()) {
        if (logCallback) {
            logCallback("Failed to find matching TS3 playback device for: " + systemDevice);
        }
        return;
    }

    // Get the last switched device for comparison
    std::string lastDevice = manager->getLastSwitchedDevice();

    // Only log if device actually changed
    if (lastDevice == tsDevice) {
        return;
    }

    auto connections = manager->getActiveConnections();
    for (uint64_t schid : connections) {
        // Open new device with mode ID and device name
        unsigned int error = ts3Functions.openPlaybackDevice(schid, modeID, tsDevice.c_str());
        if (error != ERROR_ok) {
            char* errorMsg = nullptr;
            ts3Functions.getErrorMessage(error, &errorMsg);

            // Log error message
            if (errorMsg) {
                std::string errMsg = "Failed to switch playback device to [" + tsDevice + "]: " + std::string(errorMsg);
                ts3Functions.printMessage(schid, errMsg.c_str(), PLUGIN_MESSAGE_TARGET_SERVER);
                if (logCallback) {
                    logCallback(errMsg);
                }
            }
        } else {
            // Log successful device switch
            std::string logMsg;
            if (lastDevice.empty()) {
                logMsg = "Playback device switched to: [" + tsDevice + "]";
            } else {
                logMsg = "Playback device switched from [" + lastDevice + "] to [" + tsDevice + "]";
            }
            ts3Functions.printMessage(schid, logMsg.c_str(), PLUGIN_MESSAGE_TARGET_SERVER);
            if (logCallback) {
                logCallback(logMsg);
            }
        }
    }

    // Update the last switched device
    manager->setLastSwitchedDevice(tsDevice);
    manager->updateSwitchTime();
}
