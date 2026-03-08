#include "config.h"
#include "ui_config.h"
#include <QRegularExpression>
#include <QDateTime>
#include <QPushButton>
#include <QHBoxLayout>
#include "device_switcher.h"

/* Forward declaration for audio device manager */
class AudioDeviceManager;
extern AudioDeviceManager* g_audioDeviceManager;

Config::Config(const QString& configLocation, QWidget* parent /* = nullptr */) : QDialog(parent),
m_ui(std::make_unique<Ui::configui>()),
m_settings(std::make_unique<QSettings>(configLocation, QSettings::IniFormat, this))
{
	m_ui->setupUi(this);

	setWindowTitle("JUL14Ns Audio Mods :: Config");

	// Connect UI Elements.
	connect(m_ui->pbOk, &QPushButton::clicked, this, [&] {
		this->saveSettings();
		this->close();
	});
	connect(m_ui->pbApply, &QPushButton::clicked, this, [&] {
		this->saveSettings();
	});
	connect(m_ui->pbCancel, &QPushButton::clicked, this, &QWidget::close);

	// Connect log clear button
	connect(m_ui->clear_logs, &QPushButton::clicked, this, &Config::clearLogs);

	// Create and connect device control buttons
	QPushButton* showDeviceButton = new QPushButton("显示当前设备", this);
	QPushButton* switchDeviceButton = new QPushButton("切换到默认设备", this);

	connect(showDeviceButton, &QPushButton::clicked, this, &Config::onShowCurrentDeviceInfo);
	connect(switchDeviceButton, &QPushButton::clicked, this, &Config::onSwitchToDefaultDevice);

	// Try to add buttons to the log layout if it exists
	if (m_ui->message_log && m_ui->message_log->parentWidget()) {
		QWidget* logParent = m_ui->message_log->parentWidget();
		QLayout* parentLayout = logParent->layout();
		if (parentLayout) {
			QHBoxLayout* buttonLayout = new QHBoxLayout();
			buttonLayout->addWidget(showDeviceButton);
			buttonLayout->addWidget(switchDeviceButton);
			buttonLayout->addStretch();
			parentLayout->addItem(buttonLayout);
		}
	}

	connect(m_ui->vad_cutoff, &QSlider::valueChanged, this, [&](int value) {
		m_ui->vad_cutoff_percentage->setText(QString::asprintf("%d \\%", value));
	});
	connect(m_ui->vad_rolloff, &QSlider::valueChanged, this, [&](int value) {
		m_ui->vad_rolloff_time->setText(QString::asprintf("%d ms", value * 10));
	});

	adjustSize();
	setWindowFlags(Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint);
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
}

Config::~Config() {
	m_settings->sync();
}


void Config::setConfigOption(const QString& option, const QVariant& value) {
	m_settings->setValue(option, value);
}

QVariant Config::getConfigOption(const QString& option) const {
	return m_settings->value(option);
}

void Config::showEvent(QShowEvent* /* e */) {
	adjustSize();
	loadSettings();
}

void Config::changeEvent(QEvent* e) {
	if (e->type() == QEvent::StyleChange && isVisible()) {
		adjustSize();
	}
}

void Config::saveSettings() {
	setConfigOption("inputFilter", m_ui->input_filter->isChecked());
	setConfigOption("inputVAD", m_ui->input_vad->isChecked());
	setConfigOption("vadCutoff", m_ui->vad_cutoff->value());
	setConfigOption("vadRolloff", m_ui->vad_rolloff->value());
	setConfigOption("inputAGC", m_ui->input_agc->isChecked());

	setConfigOption("outputFilter", m_ui->output_filter->isChecked());
	setConfigOption("autoDeviceSwitch", m_ui->auto_device_switch->isChecked());
	QStringList uuids{};
	//for (auto& u : m_ui->uuids->toPlainText().split(QRegExp{ "[\n,;|:]" })) {
	for (auto& u : m_ui->uuids->toPlainText().split(QRegularExpression{ "[\n,;|:]" })) {
		uuids.push_back(u.trimmed());
	}
	setConfigOption("filterIncomingUuids", uuids);
}

void Config::loadSettings() {
	m_ui->input_filter->setChecked(getConfigOption("inputFilter").toBool());
	m_ui->input_vad->setChecked(getConfigOption("inputVAD").toBool());
	m_ui->vad_cutoff->setValue(getConfigOption("vadCutoff").toInt());
	m_ui->vad_rolloff->setValue(getConfigOption("vadRolloff").toInt());
	m_ui->input_agc->setChecked(getConfigOption("inputAGC").toBool());

	m_ui->output_filter->setChecked(getConfigOption("outputFilter").toBool());
	m_ui->auto_device_switch->setChecked(getConfigOption("autoDeviceSwitch").toBool());
	QStringList uuids = getConfigOption("filterIncomingUuids").toStringList();
	bool first = true;
	QString uuid_string = "";
	for (auto& uuid : uuids) {
		if (!first) uuid_string += "\n";
		else first = false;
		uuid_string += uuid;
	}
	m_ui->uuids->setPlainText(uuid_string);
}

void Config::addLogMessage(const QString& message) {
	if (m_ui && m_ui->message_log) {
		QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
		QString formattedMessage = QString("[%1] %2").arg(timestamp, message);
		m_ui->message_log->appendPlainText(formattedMessage);
	}
}

void Config::clearLogs() {
	if (m_ui && m_ui->message_log) {
		m_ui->message_log->clear();
	}
}

void Config::onShowCurrentDeviceInfo() {
	if (!g_audioDeviceManager) {
		addLogMessage("Error: Audio device manager not initialized");
		return;
	}

	// Get the current system default device
	std::string currentDevice = g_audioDeviceManager->getCurrentSystemDevice();

	if (currentDevice.empty()) {
		addLogMessage("Could not detect current system default device");
	} else {
		addLogMessage("Current system default device: [" + QString::fromStdString(currentDevice) + "]");
	}

	// Get the last detected device
	std::string lastDetected = g_audioDeviceManager->getLastSwitchedDevice();
	if (!lastDetected.empty()) {
		addLogMessage("Last switched TS3 device: [" + QString::fromStdString(lastDetected) + "]");
	}
}

void Config::onSwitchToDefaultDevice() {
	if (!g_audioDeviceManager) {
		addLogMessage("Error: Audio device manager not initialized");
		return;
	}

	// Get the current system default device
	std::string systemDevice = g_audioDeviceManager->getCurrentSystemDevice();
	if (systemDevice.empty()) {
		addLogMessage("Error: Could not detect system default device");
		return;
	}

	addLogMessage("Attempting to switch to system default device: [" + QString::fromStdString(systemDevice) + "]");

	// Get the last used modeID
	std::string modeID = g_audioDeviceManager->getLastUsedModeID();
	if (modeID.empty()) {
		addLogMessage("Error: No modeID available. Please connect to a server first.");
		return;
	}

	// Create a log callback
	auto logCallback = [this](const std::string& message) {
		addLogMessage(QString::fromStdString(message));
	};

	// Trigger the device switch using the device_switcher function
	switchPlaybackDeviceForAllConnections(g_audioDeviceManager, systemDevice, modeID.c_str(), logCallback);
}