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
#if defined(WIN32) || defined(__WIN32__) || defined(_WIN32)
    IMMDeviceEnumerator* m_deviceEnumerator = nullptr;
    IMMDevice* m_defaultDevice = nullptr;
    bool m_comInitialized = false;
#endif
    std::string m_lastDefaultDeviceName;

public:
    AudioDeviceListener() {
#if defined(WIN32) || defined(__WIN32__) || defined(_WIN32)
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) {
            m_comInitialized = true;
            CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER,
                             __uuidof(IMMDeviceEnumerator), (void**)&m_deviceEnumerator);
        }
#endif
    }

    ~AudioDeviceListener() {
#if defined(WIN32) || defined(__WIN32__) || defined(_WIN32)
        if (m_defaultDevice) {
            m_defaultDevice->Release();
            m_defaultDevice = nullptr;
        }
        if (m_deviceEnumerator) {
            m_deviceEnumerator->Release();
            m_deviceEnumerator = nullptr;
        }
        if (m_comInitialized) {
            CoUninitialize();
        }
#endif
    }

    /**
     * @brief Get the name of the default playback device
     * Re-initializes WASAPI enumerator to detect device changes (inspired by PyAudio approach)
     * @return Device name as string, empty if failed
     */
    std::string getDefaultPlaybackDeviceName() {
        std::string result;
#if defined(WIN32) || defined(__WIN32__) || defined(_WIN32)
        // Re-initialize COM and enumerator to detect device changes
        // This is the key difference from the original implementation
        IMMDeviceEnumerator* tempEnumerator = nullptr;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            NULL,
            CLSCTX_INPROC_SERVER,
            __uuidof(IMMDeviceEnumerator),
            (void**)&tempEnumerator
        );

        if (FAILED(hr) || !tempEnumerator) {
            return result;
        }

        IMMDevice* defaultDevice = nullptr;
        hr = tempEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);

        if (SUCCEEDED(hr) && defaultDevice) {
            IPropertyStore* propertyStore = nullptr;
            hr = defaultDevice->OpenPropertyStore(STGM_READ, &propertyStore);

            if (SUCCEEDED(hr) && propertyStore) {
                PROPVARIANT varName;
                PropVariantInit(&varName);

                hr = propertyStore->GetValue(PKEY_Device_FriendlyName, &varName);
                if (SUCCEEDED(hr) && varName.pwszVal) {
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

        // Clean up temporary enumerator
        tempEnumerator->Release();
#endif
        return result;
    }

    /**
     * @brief Get detailed information about the default device
     * @return Formatted device information string
     */
    std::string getDefaultDeviceInfo() {
        std::string result;
#if defined(WIN32) || defined(__WIN32__) || defined(_WIN32)
        IMMDeviceEnumerator* tempEnumerator = nullptr;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            NULL,
            CLSCTX_INPROC_SERVER,
            __uuidof(IMMDeviceEnumerator),
            (void**)&tempEnumerator
        );

        if (FAILED(hr) || !tempEnumerator) {
            return "Failed to create device enumerator";
        }

        IMMDevice* defaultDevice = nullptr;
        hr = tempEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);

        if (SUCCEEDED(hr) && defaultDevice) {
            // Get device name (friendly name)
            IPropertyStore* propertyStore = nullptr;
            hr = defaultDevice->OpenPropertyStore(STGM_READ, &propertyStore);

            if (SUCCEEDED(hr) && propertyStore) {
                PROPVARIANT varName;
                PropVariantInit(&varName);
                hr = propertyStore->GetValue(PKEY_Device_FriendlyName, &varName);

                std::string deviceName = "Unknown";
                if (SUCCEEDED(hr) && varName.pwszVal) {
                    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, NULL, 0, NULL, NULL);
                    if (sizeNeeded > 0) {
                        deviceName.resize(sizeNeeded - 1);
                        WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, &deviceName[0], sizeNeeded, NULL, NULL);
                    }
                }
                PropVariantClear(&varName);

                // Get device ID (instance GUID)
                PROPVARIANT varDeviceId;
                PropVariantInit(&varDeviceId);
                hr = propertyStore->GetValue(PKEY_Device_InstanceId, &varDeviceId);

                std::string deviceId = "N/A";
                if (SUCCEEDED(hr) && varDeviceId.pwszVal) {
                    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, varDeviceId.pwszVal, -1, NULL, 0, NULL, NULL);
                    if (sizeNeeded > 0) {
                        deviceId.resize(sizeNeeded - 1);
                        WideCharToMultiByte(CP_UTF8, 0, varDeviceId.pwszVal, -1, &deviceId[0], sizeNeeded, NULL, NULL);
                    }
                }
                PropVariantClear(&varDeviceId);

                result = "Device: " + deviceName + "\nID: " + deviceId + "\nType: Render (Playback)";

                propertyStore->Release();
            }
            defaultDevice->Release();
        } else {
            result = "No default playback device found";
        }

        tempEnumerator->Release();
#else
        result = "Device detection only supported on Windows";
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

    // Device change detection (every 2 seconds)
    std::chrono::steady_clock::time_point m_lastCheckTime;
    std::string m_lastDeviceName;
    static constexpr int CHECK_INTERVAL_MS = 2000;

    std::unique_ptr<AudioDeviceListener> m_deviceListener;
    bool m_enabled = false;

public:
    AudioDeviceManager() : m_deviceListener(std::make_unique<AudioDeviceListener>()) {
        m_lastSwitchTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(DEBOUNCE_MS + 1);
        m_lastCheckTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(CHECK_INTERVAL_MS + 1);
        m_lastDeviceName = "";
    }

    ~AudioDeviceManager() = default;

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
     * @brief Check if device has changed and auto-switch if enabled
     * Should be called periodically (e.g., from audio processing callback)
     * Returns true if device was switched, false otherwise
     */
    bool checkAndSwitchDeviceIfChanged() {
        if (!m_enabled) return false;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastCheckTime);

        // Only check every CHECK_INTERVAL_MS (2 seconds)
        if (elapsed.count() < CHECK_INTERVAL_MS) {
            return false;
        }

        m_lastCheckTime = now;

        // Get current system device
        std::string currentDevice = getSystemDefaultDevice();
        if (currentDevice.empty()) {
            return false;
        }

        // Check if device has changed
        if (currentDevice == m_lastDeviceName) {
            return false; // No change
        }

        // Device changed - store new device name
        m_lastDeviceName = currentDevice;

        // Return true to signal that device changed
        // The actual switching logic will be in plugin.cpp
        return true;
    }

    /**
     * @brief Get the last detected device name
     */
    std::string getLastDetectedDevice() const {
        return m_lastDeviceName;
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


// Note: findMatchingDevice and switchPlaybackDeviceForAllConnections are implemented in plugin.cpp
// They require TS3 API definitions and cannot be inline in this header
