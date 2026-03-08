#if defined(WIN32) || defined(__WIN32__) || defined(_WIN32)
#pragma warning(disable : 4100) /* Disable Unreferenced parameter warning */
//#include <Windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>
#include <algorithm>
#include <span>
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
#include "visualize.h"
#include "compressor.h"

#include "rnnoise.h"
#include "device_switcher.h"

static struct TS3Functions ts3Functions;
static char *pluginID = NULL;
Config *configObject;
Visualize *visualizeWindow;
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


/*-------------------------- Configure Here --------------------------*/
/*
 * The following functions should be configured to your needs.
 */
int ts3plugin_init() {
	char configPath[PATH_BUFSIZE];
	ts3Functions.getConfigPath(configPath, PATH_BUFSIZE);
	configObject = new Config(QString::fromUtf8(configPath) + CONFIG_FILE);
	visualizeWindow = new Visualize();
	txStatePerChannel = new ChannelMap(MAX_RX_CHANNEL);
	rxStatePerChannel = new ChannelMap(MAX_RX_CHANNEL);
	rxStatePerChannelPerUser = new SchidClientIdMap(MAX_STREAM_FILTER);

	// Initialize audio device manager
	g_audioDeviceManager = new AudioDeviceManager();
	if (configObject) {
		g_audioDeviceManager->setEnabled(configObject->getConfigOption("autoDeviceSwitch").toBool());
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

	if (visualizeWindow) {
		visualizeWindow->close();
		delete visualizeWindow;
		visualizeWindow = nullptr;
	}

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
	BEGIN_CREATE_MENUS(2);
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_GLOBAL_SETTINGS, "Settings", "");
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_GLOBAL_VISUALIZE, "Visualize", "");
	END_CREATE_MENUS;
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
		case MENU_ID_GLOBAL_VISUALIZE:
			if (visualizeWindow->isVisible()) {
				visualizeWindow->raise();
				visualizeWindow->activateWindow();
			}
			else
				visualizeWindow->show();
			break;
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
	// ensure 480 samples (10ms 48kHz)
	if (sampleCount != 480) {
		std::ostringstream logMessage;
		logMessage << "Unexpected number of TX samples (" << sampleCount << "), skipping!";
		ts3Functions.logMessage(logMessage.str().c_str(), LogLevel_WARNING, PLUGIN_NAME, serverConnectionHandlerID);
	}

	// Periodically check if system default device has changed (we do this here because this function is called frequently)
	// We use an empty modeID here just for change detection, the actual switch will use the stored modeID
	if (g_audioDeviceManager && g_audioDeviceManager->checkAndSwitchIfDeviceChanged()) {
		std::string systemDevice = g_audioDeviceManager->getSystemDefaultDevice();
		std::string lastModeID = g_audioDeviceManager->getLastUsedModeID();

		if (!systemDevice.empty() && !lastModeID.empty()) {
			// Create log callback
			auto logCallback = [](const std::string& message) {
				if (configObject) {
					configObject->addLogMessage(QString::fromStdString(message));
				}
			};

			// Trigger device switch with the last known modeID
			switchPlaybackDeviceForAllConnections(g_audioDeviceManager, systemDevice, lastModeID.c_str(), logCallback);
		}
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
		// Linear interpolation: lerp(a, b, t) = a + t * (b - a)
		float targetGain = SILENCE_TARGET_GAIN + smoothedVadProb * (VOICE_TARGET_GAIN - SILENCE_TARGET_GAIN);
		for (int i = 0; i < 480; i++) {
			CompressorMetaData cmd = compressor.process(&samples[channels * i], channels, targetGain);
			loudness += cmd.loudness;
			adjustment += cmd.adjustment;
			adjustedLoudness += cmd.adjustedLoudness;
		}
		loudness /= sampleCount;
		adjustment /= sampleCount;
		adjustedLoudness /= sampleCount;
		visualizeWindow->addLufsData(loudness);
		visualizeWindow->addAgcData(adjustment);
		visualizeWindow->addLufsAgcData(adjustedLoudness);
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

	// Store the modeID for later use in periodic checks
	g_audioDeviceManager->updateLastUsedModeID(modeID);

	// Check if feature is enabled
	if (!g_audioDeviceManager->isEnabled()) return;

	// Get system default device
	std::string systemDevice = g_audioDeviceManager->getSystemDefaultDevice();
	if (systemDevice.empty()) {
		return;
	}

	// Create log callback
	auto logCallback = [](const std::string& message) {
		if (configObject) {
			configObject->addLogMessage(QString::fromStdString(message));
		}
	};

	// Switch playback device for all active connections
	switchPlaybackDeviceForAllConnections(g_audioDeviceManager, systemDevice, modeID, logCallback);
}