#pragma once

#include <QtWidgets/QDialog>
#include <QtCore/QObject>

#include "ui_dialog.hpp"

// Use C API instead of C++ API to avoid symbol linking issues
extern "C" {
#include "../pw_helper_c.h"
#include "../pw_helper_common.h"
}

class PwAsioDialog: public QDialog {
	Q_OBJECT

	public:
	PwAsioDialog(struct user_pw_helper *pw_helper);
	~PwAsioDialog() override;

	Q_PROPERTY(uint32_t bufferSize READ getBufferSize WRITE setBufferSize)
	Q_PROPERTY(uint32_t inputChannels READ getInputChannels WRITE setInputChannels)
	Q_PROPERTY(uint32_t outputChannels READ getOutputChannels WRITE setOutputChannels)
	Q_PROPERTY(uint32_t sampleRate READ getSampleRate WRITE setSampleRate)
	Q_PROPERTY(bool autoConnect READ getAutoConnect WRITE setAutoConnect)

	inline uint32_t getBufferSize() const { return bufferSize; };
	void setBufferSize(uint32_t bufferSize);
	
	inline uint32_t getInputChannels() const { return inputChannels; };
	void setInputChannels(uint32_t channels);
	
	inline uint32_t getOutputChannels() const { return outputChannels; };
	void setOutputChannels(uint32_t channels);
	
	inline uint32_t getSampleRate() const { return sampleRate; };
	void setSampleRate(uint32_t rate);
	
	inline bool getAutoConnect() const { return autoConnect; };
	void setAutoConnect(bool connect);

	// Load and save configuration
	void loadConfiguration();
	void saveConfiguration();
	
	// Set PipeWire quantum to match buffer size
	void setPipeWireQuantum(uint32_t quantum);

	private slots:
	void bufferSizeSet(QString const &text);
	void inputChannelsChanged(int channels);
	void outputChannelsChanged(int channels);
	void sampleRateChanged(QString const &text);
	void autoConnectChanged(bool enabled);

	private:
	Ui::PwAsioDialog ui;

	// Configuration values
	uint32_t bufferSize;
	uint32_t inputChannels;
	uint32_t outputChannels;
	uint32_t sampleRate;
	bool autoConnect;

	// Use C API helper instead of C++ API
	struct user_pw_helper *pw_helper;
};
