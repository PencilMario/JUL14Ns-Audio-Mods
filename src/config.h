#pragma once
#include <QtWidgets/QDialog>
#include <QtCore/QSettings>
#include <memory>
#include <functional>

namespace Ui {
	class configui;
}

class Config : public QDialog
{
	Q_OBJECT
public:
	Config(const QString& configLocation, QWidget *parent = nullptr);
	~Config();
	Config(const Config& other) = delete;
	Config& operator=(const Config& other) = delete;

	void setConfigOption(const QString& option, const QVariant& value);
	QVariant getConfigOption(const QString& option) const;

	// Logging functions
	void appendLog(const QString& message);
	void clearLogs();

	// Device query callbacks
	void onShowCurrentDevice();
	void onSwitchToDefaultDevice();
	void onShowTS3Devices();

	// Setters for external handlers
	void setShowDeviceHandler(std::function<std::string()> handler) { m_showDeviceHandler = handler; }
	void setSwitchDeviceHandler(std::function<std::string(const std::string&)> handler) { m_switchDeviceHandler = handler; }
	void setShowTS3DevicesHandler(std::function<std::string()> handler) { m_showTS3DevicesHandler = handler; }

protected:
	void showEvent(QShowEvent* e) override;
	void changeEvent(QEvent* e) override;

private:
	std::unique_ptr<Ui::configui> m_ui;
	std::unique_ptr<QSettings> m_settings;

	// Handlers for device operations
	std::function<std::string()> m_showDeviceHandler;
	std::function<std::string(const std::string&)> m_switchDeviceHandler;
	std::function<std::string()> m_showTS3DevicesHandler;

	void saveSettings();
	void loadSettings();
};