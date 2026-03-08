#pragma once
#include <QtWidgets/QDialog>
#include <QtCore/QSettings>
#include <memory>

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

	/**
	 * @brief Add a message to the log display
	 */
	void addLogMessage(const QString& message);

	/**
	 * @brief Clear all log messages
	 */
	void clearLogs();

protected:
	void showEvent(QShowEvent* e) override;
	void changeEvent(QEvent* e) override;

private:
	std::unique_ptr<Ui::configui> m_ui;
	std::unique_ptr<QSettings> m_settings;

	void saveSettings();
	void loadSettings();
};