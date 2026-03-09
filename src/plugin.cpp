#if defined(WIN32) || defined(__WIN32__) || defined(_WIN32)
#pragma warning(disable : 4100) /* Disable Unreferenced parameter warning */
//#include <Windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

#include "teamspeak/public_errors.h"
#include "teamspeak/public_errors_rare.h"
#include "teamspeak/public_definitions.h"
#include "teamspeak/public_rare_definitions.h"
#include "ts3_functions.h"

#include "plugin.h"
#include "definitions.hpp"
#include "helper.h"
#include "config.h"
#include "fixed_map.h"
#include "lufs.h"
#include "buffer.h"
#ifdef HAVE_QT_CHARTS
#include "visualize.h"
#endif
#include "compressor.h"

#include "rnnoise.h"
#include "device_switcher.h"

// Qt includes should be last
#include <QtWidgets/QPushButton>
#include <QtCore/QObject>

static struct TS3Functions ts3Functions;
static char *pluginID = NULL;
Config *configObject;
#ifdef HAVE_QT_CHARTS
Visualize *visualizeWindow;
#endif
AudioDeviceManager *g_audioDeviceManager = nullptr;

class UUIDWrapper {
private:
	char uuid[28];
public:
	UUIDWrapper(char* _uuid) {
		std::memcpy(uuid, _uuid, 28 * sizeof(char));
	}
	bool operator==(const UUIDWrapper& rhs) const
	{
		return !strncmp(uuid, rhs.uuid, 28);
	}
};

typedef FixedSizeMap<int, DenoiseState*> ChannelMap;
typedef FixedSizeMap<UUIDWrapper, ChannelMap*> SchidClientIdMap;

ChannelMap* txStatePerChannel;
int vadRolloff( 0 );
RingBuffer<float> txLUFSAdjustment( ADJUSTMENT_BUF );
Compressor compressor( 0.010f, 0.200f );
RingBuffer<float> vadProbOverTime( 50 );

ChannelMap* rxStatePerChannel;
SchidClientIdMap* rxStatePerChannelPerUser;


/*
 * Device Switching Helper Functions
 * These functions require TS3 API and are implemented here instead of in the header
 */

/**
 * @brief Find a TS3 device that matches the system device name
 * Uses 3-tier matching: exact match, partial match, or first available
 * Matches against displayName (deviceList[i][0]) and returns deviceID (deviceList[i][1])
 * Note: TS3 API format is opposite to documentation: [0]=displayName, [1]=deviceID(GUID)
 * @return Device ID (GUID) from TS3, or empty string if not found
 */
static std::string findMatchingDevice(AudioDeviceManager* manager, const std::string& systemDevice, const char* modeID) {
	if (!modeID || systemDevice.empty()) {
		return {};
	}

	// Get available playback devices for this mode
	char*** deviceList = nullptr;
	unsigned int error = ts3Functions.getPlaybackDeviceList(modeID, &deviceList);
	if (error != ERROR_ok || !deviceList) {
		return {};
	}

	std::string result;
	std::string systemDeviceLower = toLower(systemDevice);

	// Iterate through devices and find matching one
	// Actual format (different from docs): deviceList[i][0] = displayName, deviceList[i][1] = deviceID(GUID)
	// Tier 1: Exact match on display name
	for (int i = 0; deviceList[i] != nullptr; ++i) {
		char* displayName = deviceList[i][0];
		char* deviceId = deviceList[i][1];
		if (!displayName || !deviceId) continue;

		std::string displayNameLower = toLower(displayName);
		if (displayNameLower == systemDeviceLower) {
			result = deviceId;  // Return the GUID device ID
			break;
		}
	}

	// Tier 2: Partial match if no exact match found
	if (result.empty()) {
		for (int i = 0; deviceList[i] != nullptr; ++i) {
			char* displayName = deviceList[i][0];
			char* deviceId = deviceList[i][1];
			if (!displayName || !deviceId) continue;

			std::string displayNameLower = toLower(displayName);
			if (displayNameLower.find(systemDeviceLower) != std::string::npos ||
				systemDeviceLower.find(displayNameLower) != std::string::npos) {
				result = deviceId;  // Return the GUID device ID
				break;
			}
		}
	}

	// Tier 3: Use first available device if no match found
	if (result.empty() && deviceList[0] != nullptr && deviceList[0][1] != nullptr) {
		result = deviceList[0][1];  // Return the GUID device ID
	}

	// Clean up
	ts3Functions.freeMemory(deviceList);

	return result;
}

/**
 * @brief Switch all active connections to the matching system device
 */
static void switchPlaybackDeviceForAllConnections(AudioDeviceManager* manager, const std::string& systemDevice, const char* modeID) {
	if (!manager || systemDevice.empty() || !modeID) {
		return;
	}

	// Check debounce
	if (!manager->canSwitch()) {
		return;
	}

	// Find matching device
	std::string matchingDevice = findMatchingDevice(manager, systemDevice, modeID);
	if (matchingDevice.empty()) {
		return;
	}

	// Switch all active connections
	auto connections = manager->getActiveConnections();
	for (uint64_t schid : connections) {
		// Save the current volume settings before closing the device
		float savedVolumeModifier = 0.0f;
		unsigned int volumeError = ts3Functions.getPlaybackConfigValueAsFloat(schid, "volume_modifier", &savedVolumeModifier);

		// Save the sound pack volume (wave volume)
		float savedWaveVolume = 0.0f;
		unsigned int waveVolumeError = ts3Functions.getPlaybackConfigValueAsFloat(schid, "volume_factor_wave", &savedWaveVolume);

		// First close the existing device to avoid ERROR_sound_handler_has_device
		ts3Functions.closePlaybackDevice(schid);

		// Now open the new device
		unsigned int error = ts3Functions.openPlaybackDevice(schid, modeID, matchingDevice.c_str());
		if (error != ERROR_ok && configObject) {
			char* errorMsg = nullptr;
			ts3Functions.getErrorMessage(error, &errorMsg);
			std::ostringstream logMessage;
			logMessage << "Failed to switch device to " << matchingDevice << " for connection " << schid << ": " << (errorMsg ? errorMsg : "Unknown error");
			configObject->appendLog(QString::fromStdString(logMessage.str()));
			if (errorMsg) ts3Functions.freeMemory(errorMsg);
		} else {
			// Device switched successfully - restore the volume settings and enable echo cancellation

			// Restore playback device volume
			if (volumeError == ERROR_ok) {
				std::ostringstream volumeStream;
				volumeStream << std::fixed << std::setprecision(1) << savedVolumeModifier;
				std::string volumeStr = volumeStream.str();
				unsigned int setError = ts3Functions.setPlaybackConfigValue(schid, "volume_modifier", volumeStr.c_str());
				if (setError != ERROR_ok && configObject) {
					configObject->appendLog(QString::fromUtf8("⚠ 恢复播放设备音量失败"));
				}
			}

			// Restore sound pack volume (wave volume)
			if (waveVolumeError == ERROR_ok) {
				std::ostringstream waveVolumeStream;
				waveVolumeStream << std::fixed << std::setprecision(1) << savedWaveVolume;
				std::string waveVolumeStr = waveVolumeStream.str();
				unsigned int setWaveError = ts3Functions.setPlaybackConfigValue(schid, "volume_factor_wave", waveVolumeStr.c_str());
				if (setWaveError != ERROR_ok && configObject) {
					configObject->appendLog(QString::fromUtf8("⚠ 恢复音效包音量失败"));
				}
			}

			// Automatically enable echo cancellation
			unsigned int echoError = ts3Functions.setPreProcessorConfigValue(schid, "echo_canceling", "true");
			if (echoError != ERROR_ok && configObject) {
				configObject->appendLog(QString::fromUtf8("⚠ 启用回声消除失败"));
			}
		}
	}

	// Update switch timestamp
	manager->updateSwitchTime();
}

/*-------------------------- Configure Here --------------------------*/
/*
 * The following functions should be configured to your needs.
 */
int ts3plugin_init() {
	char configPath[PATH_BUFSIZE];
	ts3Functions.getConfigPath(configPath, PATH_BUFSIZE);
	configObject = new Config(QString::fromUtf8(configPath) + CONFIG_FILE);
#ifdef HAVE_QT_CHARTS
	visualizeWindow = new Visualize();
#endif
	txStatePerChannel = new ChannelMap(MAX_RX_CHANNEL);
	rxStatePerChannel = new ChannelMap(MAX_RX_CHANNEL);
	rxStatePerChannelPerUser = new SchidClientIdMap(MAX_STREAM_FILTER);

	// Initialize audio device manager
	g_audioDeviceManager = new AudioDeviceManager();
	if (configObject) {
		g_audioDeviceManager->setEnabled(configObject->getConfigOption("autoDeviceSwitch").toBool());

		// Initialize device info in logs
		AudioDeviceListener deviceListener;
		std::string deviceInfo = deviceListener.getDefaultDeviceInfo();
		configObject->appendLog(QString::fromUtf8("插件初始化"));
		configObject->appendLog(QString::fromUtf8(deviceInfo.c_str()));

		// Set up device query handler
		configObject->setShowDeviceHandler([]() -> std::string {
			AudioDeviceListener listener;
			std::string result = listener.getDefaultDeviceInfo();
			result += "\n---\n";

			// Get TS3 current playback device for the first active connection
			if (g_audioDeviceManager) {
				auto connections = g_audioDeviceManager->getActiveConnections();
				if (!connections.empty()) {
					char* currentDevice = nullptr;
					int isDefault = 0;
					unsigned int error = ts3Functions.getCurrentPlaybackDeviceName(connections[0], &currentDevice, &isDefault);
					if (error == ERROR_ok && currentDevice) {
						result += "TS3当前播放设备: " + std::string(currentDevice) + "\n";
						result += "是否为默认: " + std::string(isDefault ? "是" : "否");
						ts3Functions.freeMemory(currentDevice);
					}
				}
			}

			return result;
		});

		// Set up device switch handler
		configObject->setSwitchDeviceHandler([](const std::string& unused) -> std::string {
			AudioDeviceListener listener;
			std::string deviceName = listener.getDefaultPlaybackDeviceName();

			if (deviceName.empty()) {
				return "❌ 无法获取默认设备";
			}

			if (!g_audioDeviceManager) {
				return "❌ 设备管理器未初始化";
			}

			auto connections = g_audioDeviceManager->getActiveConnections();
			if (connections.empty()) {
				return "无活跃连接";
			}

			// Get the playback mode (try to get default mode, or use "default")
			char* modeID = nullptr;
			unsigned int modeError = ts3Functions.getDefaultPlayBackMode(&modeID);
			if (modeError != ERROR_ok || !modeID) {
				modeID = const_cast<char*>("default");
			}

			// Find the matching device in TS3's device list
			std::string ts3Device = findMatchingDevice(g_audioDeviceManager, deviceName, modeID);
			if (ts3Device.empty()) {
				std::string errorMsg = "❌ 无法找到匹配的TS3设备\n系统设备: " + deviceName;
				if (modeID && std::string(modeID) != "default") ts3Functions.freeMemory(modeID);
				return errorMsg;
			}

			std::string result = "正在切换到：\n";
			result += "  系统设备: " + deviceName + "\n";
			result += "  TS3模式: " + std::string(modeID ? modeID : "default") + "\n";
			result += "  TS3设备ID: " + ts3Device + "\n";

			for (uint64_t schid : connections) {
				// First close the existing device to avoid ERROR_sound_handler_has_device
				ts3Functions.closePlaybackDevice(schid);

				// Now open the new device
				unsigned int error = ts3Functions.openPlaybackDevice(schid, modeID, ts3Device.c_str());
				if (error != ERROR_ok) {
					char* errorMsg = nullptr;
					ts3Functions.getErrorMessage(error, &errorMsg);
					result += "❌ 切换失败: " + std::string(errorMsg ? errorMsg : "未知错误") + "\n";
					if (errorMsg) ts3Functions.freeMemory(errorMsg);
				} else {
					result += "✓ 连接 " + std::to_string(schid) + " 切换成功\n";
				}
			}

			if (modeID && std::string(modeID) != "default") {
				ts3Functions.freeMemory(modeID);
			}

			// Add TS3 current playback device name to result
			result += "\n---\n";
			if (!connections.empty()) {
				char* currentDevice = nullptr;
				int isDefault = 0;
				unsigned int error = ts3Functions.getCurrentPlaybackDeviceName(connections[0], &currentDevice, &isDefault);
				if (error == ERROR_ok && currentDevice) {
					result += "TS3当前播放设备: " + std::string(currentDevice) + "\n";
					result += "是否为默认: " + std::string(isDefault ? "是" : "否");
					ts3Functions.freeMemory(currentDevice);
				}
			}

			return result;
		});

		// Set up TS3 devices listing handler
		configObject->setShowTS3DevicesHandler([]() -> std::string {
			std::string result = "TS3 可用播放设备列表：\n";
			result += "=" + std::string(80, '=') + "\n";

			// Try to get default playback mode
			char* defaultMode = nullptr;
			unsigned int modeError = ts3Functions.getDefaultPlayBackMode(&defaultMode);
			if (modeError != ERROR_ok || !defaultMode) {
				result += "❌ 无法获取默认播放模式\n";
				return result;
			}

			// Get playback device list for the default mode
			char*** deviceList = nullptr;
			unsigned int error = ts3Functions.getPlaybackDeviceList(defaultMode, &deviceList);
			if (error != ERROR_ok || !deviceList) {
				result += "❌ 无法获取设备列表\n";
				if (defaultMode) ts3Functions.freeMemory(defaultMode);
				return result;
			}

			// List all available devices
			// Actual format: deviceList[i][0] = displayName, deviceList[i][1] = deviceID(GUID)
			result += "播放模式: " + std::string(defaultMode) + "\n\n";
			int deviceCount = 0;
			for (int i = 0; deviceList[i] != nullptr; ++i) {
				if (deviceList[i][0] && deviceList[i][1]) {
					deviceCount++;
					result += std::to_string(deviceCount) + ". [显示名] " + std::string(deviceList[i][0]) + "\n";
					result += "   [设备ID] " + std::string(deviceList[i][1]) + "\n";
				}
			}

			if (deviceCount == 0) {
				result += "   (无可用设备)\n";
			}

			result += "=" + std::string(80, '=') + "\n";

			// Clean up
			ts3Functions.freeMemory(deviceList);
			if (defaultMode) ts3Functions.freeMemory(defaultMode);

			// Add TS3 current playback device name to result
			result += "\n当前TS3播放设备: ";

			// Get the first active connection to query current device
			auto connections = g_audioDeviceManager ? g_audioDeviceManager->getActiveConnections() : std::vector<uint64_t>();
			if (!connections.empty()) {
				char* currentDevice = nullptr;
				int isDefault = 0;
				unsigned int error = ts3Functions.getCurrentPlaybackDeviceName(connections[0], &currentDevice, &isDefault);
				if (error == ERROR_ok && currentDevice) {
					result += std::string(currentDevice);
					ts3Functions.freeMemory(currentDevice);
				} else {
					result += "无法获取";
				}
			} else {
				result += "无活跃连接";
			}

			return result;
		});
	}

	int expectedSize = rnnoise_get_frame_size();
	if (expectedSize != 480) {
		std::cout << "RNNoise seems to be broken, refusing to load!" << std::endl;
		return 1;
	}

	return 0;
}

void ts3plugin_shutdown() {
	if (g_audioDeviceManager) {
		delete g_audioDeviceManager;
		g_audioDeviceManager = nullptr;
	}

	if (configObject) {
		configObject->close();
		delete configObject;
		configObject = nullptr;
	}

#ifdef HAVE_QT_CHARTS
	if (visualizeWindow) {
		visualizeWindow->close();
		delete visualizeWindow;
		visualizeWindow = nullptr;
	}
#endif

	if (pluginID) {
		free(pluginID);
		pluginID = NULL;
	}

	// free TX

	// free global RX


	// TODO: free all entries in rxStateMap
}

enum {
	MENU_ID_GLOBAL_SETTINGS = 1,
	MENU_ID_GLOBAL_VISUALIZE = 2
};

// Sync audio device manager settings from config
static void syncAudioDeviceManagerSettings() {
	if (!g_audioDeviceManager || !configObject) return;
	g_audioDeviceManager->setEnabled(configObject->getConfigOption("autoDeviceSwitch").toBool());
}

void ts3plugin_initMenus(struct PluginMenuItem ***menuItems, char **menuIcon) {
#ifdef HAVE_QT_CHARTS
	BEGIN_CREATE_MENUS(2);
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_GLOBAL_SETTINGS, "Settings", "");
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_GLOBAL_VISUALIZE, "Visualize", "");
	END_CREATE_MENUS;
#else
	BEGIN_CREATE_MENUS(1);
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_GLOBAL_SETTINGS, "Settings", "");
	END_CREATE_MENUS;
#endif
	menuIcon = NULL;
}

void ts3plugin_onMenuItemEvent(uint64 serverConnectionHandlerID, enum PluginMenuType type, int menuItemID, uint64 selectedItemID) {
	switch (type) {
	case PLUGIN_MENU_TYPE_GLOBAL:
		switch (menuItemID) {
		case MENU_ID_GLOBAL_SETTINGS:
			if (configObject->isVisible()) {
				configObject->raise();
				configObject->activateWindow();
			}
			else
				configObject->show();
			break;
#ifdef HAVE_QT_CHARTS
		case MENU_ID_GLOBAL_VISUALIZE:
			if (visualizeWindow->isVisible()) {
				visualizeWindow->raise();
				visualizeWindow->activateWindow();
			}
			else
				visualizeWindow->show();
			break;
#endif
		}
		break;
	}
}

/*-------------------------- DON'T TOUCH --------------------------*/
/*
 * Those functions are setup nicely and
 * should be configured using the definitions.hpp file.
 */
const char *ts3plugin_name() {
	return PLUGIN_NAME;
}

const char *ts3plugin_version() {
	return PLUGIN_VERSION;
}

int ts3plugin_apiVersion() {
	return PLUGIN_API_VERSION;
}

const char *ts3plugin_author() {
	return PLUGIN_AUTHOR;
}

const char *ts3plugin_description() {
	return PLUGIN_DESCRIPTION;
}

void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) {
	ts3Functions = funcs;
}

int ts3plugin_offersConfigure() {
	return PLUGIN_OFFERS_CONFIGURE_NEW_THREAD;
}

void ts3plugin_configure(void *handle, void *qParentWidget) {
	if (configObject->isVisible()) {
		configObject->raise();
		configObject->activateWindow();
	}
	else {
		configObject->show();
	}
	// Sync settings when config dialog is shown
	syncAudioDeviceManagerSettings();
}

void ts3plugin_registerPluginID(const char *id) {
	const size_t sz = strlen(id) + 1;
	pluginID = (char *)malloc(sz * sizeof(char));
	_strcpy(pluginID, sz, id);
}

void ts3plugin_freeMemory(void *data) {
	free(data);
}

void ts3plugin_onEditPlaybackVoiceDataEvent(uint64 serverConnectionHandlerID, anyID clientID, short *samples, int sampleCount, int channels) {
	return;

	// ensure 480 samples (10ms 48kHz)
	if (configObject->getConfigOption("outputFilter").toBool()) {
		// we will not filter independent client audio, when all audio is filtered!
		return;
	}
	if (sampleCount != 480) {
		std::ostringstream logMessage;
		logMessage << "Unexpected number of RX samples (" << sampleCount << ") from " << clientID << " on " << serverConnectionHandlerID << ", skipping!";
		ts3Functions.logMessage(logMessage.str().c_str(), LogLevel_WARNING, PLUGIN_NAME, serverConnectionHandlerID);
		return;
	}
	char* uuid;
	if (ts3Functions.getClientVariableAsString(serverConnectionHandlerID, clientID, ClientProperties::CLIENT_UNIQUE_IDENTIFIER, &uuid) != ERROR_ok) {
		return;
	}
	QStringList filterUuids = configObject->getConfigOption("filterIncomingUuids").toStringList();
	if (std::find(filterUuids.begin(), filterUuids.end(), uuid) == filterUuids.end())
		// uuid is not set up for filtering, skipping
		return;

	ChannelMap* cm = rxStatePerChannelPerUser->get_or_init({ uuid }, [] {return new ChannelMap(MAX_RX_CHANNEL); });
	float buf[480];
	for (int chan = 0; chan < channels; chan++) {
		DenoiseState* ds = cm->get_or_init(chan, [] {return rnnoise_create(NULL); });
		for (int j = 0; j < 480; j++) buf[j] = samples[chan + channels * j];
		rnnoise_process_frame(ds, buf, buf);
		for (int j = 0; j < 480; j++) samples[chan + channels * j] = buf[j];
	}
}

// void ts3plugin_onEditPostProcessVoiceDataEvent(uint64 serverConnectionHandlerID, anyID clientID, short *samples, int sampleCount, int channels, const unsigned int *channelSpeakerArray, unsigned int *channelFillMask)
// {
// }

void ts3plugin_onEditMixedPlaybackVoiceDataEvent(uint64 serverConnectionHandlerID, short *samples, int sampleCount, int channels, const unsigned int *channelSpeakerArray, unsigned int *channelFillMask)
{
	return;
	// ensure 480 samples (10ms 48kHz)
	if (sampleCount != 480) {
		std::ostringstream logMessage;
		logMessage << "Unexpected number of RX samples (" << sampleCount << "), skipping!";
		ts3Functions.logMessage(logMessage.str().c_str(), LogLevel_WARNING, PLUGIN_NAME, serverConnectionHandlerID);
	}

	float buf[480];
	for (int chan = 0; chan < channels; chan++) {
		if (!(channelSpeakerArray[chan] & *channelFillMask)) {
			return;
		}
		DenoiseState* ds = rxStatePerChannel->get_or_init(chan, [] {return rnnoise_create(NULL); });
		for (int j = 0; j < 480; j++) buf[j] = samples[chan + channels*j];
		rnnoise_process_frame(ds, buf, buf);
		for (int j = 0; j < 480; j++) samples[chan + channels*j] = buf[j];
	}
}

void ts3plugin_onEditCapturedVoiceDataEvent(uint64 serverConnectionHandlerID, short *samples, int sampleCount, int channels, int *edited)
{
	// Check and auto-switch device if it has changed (every 2 seconds)
	if (g_audioDeviceManager && g_audioDeviceManager->checkAndSwitchDeviceIfChanged()) {
		// Device changed - perform automatic switch
		std::string systemDevice = g_audioDeviceManager->getLastDetectedDevice();

		configObject->appendLog(QString::fromUtf8("系统输出设备已变更，正在自动切换..."));
		configObject->appendLog(QString::fromUtf8(("检测到设备: " + systemDevice).c_str()));

		// Get the playback mode
		char* modeID = nullptr;
		unsigned int modeError = ts3Functions.getDefaultPlayBackMode(&modeID);
		if (modeError != ERROR_ok || !modeID) {
			modeID = const_cast<char*>("default");
		}

		// Use existing switch function which handles finding device and switching
		switchPlaybackDeviceForAllConnections(g_audioDeviceManager, systemDevice, modeID);
		configObject->appendLog(QString::fromUtf8("✓ 自动切换设备完成"));

		if (modeID && std::string(modeID) != "default") {
			ts3Functions.freeMemory(modeID);
		}
	}

	// ensure 480 samples (10ms 48kHz)
	if (sampleCount != 480) {
		std::ostringstream logMessage;
		logMessage << "Unexpected number of TX samples (" << sampleCount << "), skipping!";
		ts3Functions.logMessage(logMessage.str().c_str(), LogLevel_WARNING, PLUGIN_NAME, serverConnectionHandlerID);
	}
	
	//std::span<short> samples(rawSamples, sampleCount);

	bool filter = configObject->getConfigOption("inputFilter").toBool();
	bool vad = configObject->getConfigOption("inputVAD").toBool();
	bool agc = configObject->getConfigOption("inputAGC").toBool();
	if (!filter && !vad && !agc) return;
	if (filter || agc) *edited |= 1;
	float vad_cutoff = (float)configObject->getConfigOption("vadCutoff").toInt() / 100.;
	int vad_rolloff = configObject->getConfigOption("vadRolloff").toInt();

	float buf[480];

	float vad_prob{ 0 };
	for (int i = 0; i < channels; i++) {
		DenoiseState* ds = txStatePerChannel->get_or_init(i, [] {return rnnoise_create(NULL); });
		for (int j = 0; j < 480; j++) buf[j] = samples[i + channels*j];
		vad_prob += rnnoise_process_frame(ds, buf, buf);
		if (filter) {
			for (int j = 0; j < 480; j++) samples[i + channels*j] = buf[j];
		}
	}
	vad_prob /= channels;
	vadProbOverTime.push(vad_prob);

	if (agc) {
		float loudness = 0;
		float adjustment = 0;
		float adjustedLoudness = 0;
		float smoothedVadProb = vadProbOverTime.accumulate(0, [](float current, std::size_t index, float next) {
			return current + (vadProbOverTime.size() - index) * next;
		});
		smoothedVadProb /= vadProbOverTime.size() / 2 * (1 + vadProbOverTime.size());
		float targetGain = SILENCE_TARGET_GAIN + (VOICE_TARGET_GAIN - SILENCE_TARGET_GAIN) * smoothedVadProb;
		for (int i = 0; i < 480; i++) {
			CompressorMetaData cmd = compressor.process(&samples[channels * i], channels, targetGain);
			loudness += cmd.loudness;
			adjustment += cmd.adjustment;
			adjustedLoudness += cmd.adjustedLoudness;
		}
		loudness /= sampleCount;
		adjustment /= sampleCount;
		adjustedLoudness /= sampleCount;
#ifdef HAVE_QT_CHARTS
		visualizeWindow->addLufsData(loudness);
		visualizeWindow->addAgcData(adjustment);
		visualizeWindow->addLufsAgcData(adjustedLoudness);
#endif
	}

	if (vad) {
		if (vad_prob >= vad_cutoff) {
			*edited |= 2; // force send
			vadRolloff = vad_rolloff;
		}
		else if (vadRolloff) {
			*edited |= 2; // force send
			--vadRolloff;
		}
		else {
			*edited &= ~2; // force silence
		}
	}
}

/*
 * Device Switching Helper Functions
 * These functions require TS3 API and are implemented here instead of in the header
 */

/*
 * Audio Device Management Callbacks
 */

void ts3plugin_currentServerConnectionChanged(uint64 serverConnectionHandlerID)
{
	if (!g_audioDeviceManager) return;

	if (serverConnectionHandlerID != 0) {
		g_audioDeviceManager->registerConnection(serverConnectionHandlerID);
	}
}

void ts3plugin_onConnectStatusChangeEvent(uint64 serverConnectionHandlerID, int newStatus, unsigned int errorNumber)
{
	if (!g_audioDeviceManager) return;

	// Register connection when established
	if (newStatus == STATUS_CONNECTION_ESTABLISHED) {
		g_audioDeviceManager->registerConnection(serverConnectionHandlerID);
	}
	// Unregister when disconnected
	else if (newStatus == STATUS_DISCONNECTED) {
		g_audioDeviceManager->unregisterConnection(serverConnectionHandlerID);
	}
}

void ts3plugin_onSoundDeviceListChangedEvent(const char* modeID, int playOrCap)
{
	if (!g_audioDeviceManager || !modeID) return;

	// Sync config settings
	syncAudioDeviceManagerSettings();

	// Only handle playback device changes
	if (playOrCap != PLAYBACK) return;

	// Check if feature is enabled
	if (!g_audioDeviceManager->isEnabled()) return;

	// Get system default device
	std::string systemDevice = g_audioDeviceManager->getSystemDefaultDevice();
	if (systemDevice.empty()) {
		return;
	}

	// Switch playback device for all active connections
	switchPlaybackDeviceForAllConnections(g_audioDeviceManager, systemDevice, modeID);
}