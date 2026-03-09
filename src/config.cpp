#include "config.h"
#include "ui_config.h"
#include <QRegularExpression>
#include <QDateTime>
#include <QPlainTextEdit>
#include <QTextCursor>

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

	connect(m_ui->vad_cutoff, &QSlider::valueChanged, this, [&](int value) {
		m_ui->vad_cutoff_percentage->setText(QString::asprintf("%d \\%", value));
	});
	connect(m_ui->vad_rolloff, &QSlider::valueChanged, this, [&](int value) {
		m_ui->vad_rolloff_time->setText(QString::asprintf("%d ms", value * 10));
	});

	// Connect log buttons
	connect(m_ui->btn_clear_logs, &QPushButton::clicked, this, [&] {
		this->clearLogs();
	});

	connect(m_ui->btn_show_current_device, &QPushButton::clicked, this, &Config::onShowCurrentDevice);
	connect(m_ui->btn_switch_to_default, &QPushButton::clicked, this, &Config::onSwitchToDefaultDevice);
	connect(m_ui->btn_show_ts3_devices, &QPushButton::clicked, this, &Config::onShowTS3Devices);

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

void Config::appendLog(const QString& message) {
	QPlainTextEdit* logOutput = m_ui->logs_output;
	if (logOutput) {
		// Add timestamp
		QDateTime now = QDateTime::currentDateTime();
		QString timestampedMessage = QString("[%1] %2")
			.arg(now.toString("hh:mm:ss.zzz"))
			.arg(message);

		// Append to log
		logOutput->appendPlainText(timestampedMessage);

		// Auto-scroll to bottom
		QTextCursor cursor = logOutput->textCursor();
		cursor.movePosition(QTextCursor::End);
		logOutput->setTextCursor(cursor);
	}
}

void Config::clearLogs() {
	QPlainTextEdit* logOutput = m_ui->logs_output;
	if (logOutput) {
		logOutput->clear();
	}
}

void Config::onShowCurrentDevice() {
	if (m_showDeviceHandler) {
		std::string deviceInfo = m_showDeviceHandler();
		appendLog(QString::fromUtf8(deviceInfo.c_str()));
	} else {
		appendLog(QString::fromUtf8("设备查询处理程序未设置"));
	}
}

void Config::onSwitchToDefaultDevice() {
	if (m_switchDeviceHandler) {
		appendLog(QString::fromUtf8("用户触发：切换到默认设备"));
		// Handler will return formatted message about the result
		std::string result = m_switchDeviceHandler("");
		if (!result.empty()) {
			appendLog(QString::fromUtf8(result.c_str()));
		}
	} else {
		appendLog(QString::fromUtf8("设备切换处理程序未设置"));
	}
}

void Config::onShowTS3Devices() {
	if (m_showTS3DevicesHandler) {
		appendLog(QString::fromUtf8("用户触发：显示TS3可用设备"));
		std::string result = m_showTS3DevicesHandler();
		if (!result.empty()) {
			appendLog(QString::fromUtf8(result.c_str()));
		}
	} else {
		appendLog(QString::fromUtf8("设备查询处理程序未设置"));
	}
}