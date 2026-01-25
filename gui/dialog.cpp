#include "dialog.hpp"
#include "dialog.moc"

// Use C API instead of C++ API
extern "C" {
#include "../pw_helper_c.h"
#include "../pw_helper_common.h"
}

#include <QtCore/qglobal.h>
#include <QtWidgets/QVBoxLayout>

#include <cstdlib>

PwAsioDialog::PwAsioDialog(struct user_pw_helper *helper)
	: pw_helper(helper)
	, bufferSize(1024)
	, inputChannels(16)
	, outputChannels(16)
	, sampleRate(48000)  // Changed from 44100 to match common default
	, autoConnect(true)
	, validationLabel(nullptr)
	, latencyLabel(nullptr)
{
	ui.setupUi(this);

	// Create validation label for error messages
	validationLabel = new QLabel(this);
	validationLabel->setStyleSheet("QLabel { color: red; font-weight: bold; }");
	validationLabel->setWordWrap(true);

	// Create latency display label
	latencyLabel = new QLabel(this);
	latencyLabel->setStyleSheet("QLabel { color: #666; font-style: italic; }");

	// Add labels to the layout (before the button box)
	QVBoxLayout *mainLayout = qobject_cast<QVBoxLayout*>(layout());
	if (mainLayout) {
		// Insert latency label and validation label before the button box
		mainLayout->insertWidget(mainLayout->count() - 1, latencyLabel);
		mainLayout->insertWidget(mainLayout->count() - 1, validationLabel);
	}

	// Populate dropdowns with preset values
	populateBufferSizePresets();
	populateSampleRatePresets();

	// Remove the old validator - we handle validation ourselves now
	// ui.bufferSize->setValidator(new QIntValidator(1, INT_MAX, this));

	// Connect accept signal to save configuration
	connect(this, &QDialog::accepted, this, &PwAsioDialog::saveConfiguration);

	// Load current configuration
	loadConfiguration();

	// Update latency display
	updateLatencyDisplay();
}

PwAsioDialog::~PwAsioDialog() {
}

void PwAsioDialog::setBufferSize(uint32_t newBufferSize) {
	bufferSize = newBufferSize;
	ui.bufferSize->setCurrentText(QString::asprintf("%u", newBufferSize));
	updateLatencyDisplay();
}

void PwAsioDialog::setInputChannels(uint32_t channels) {
	inputChannels = channels;
	ui.inputChannels->setValue(channels);
}

void PwAsioDialog::setOutputChannels(uint32_t channels) {
	outputChannels = channels;
	ui.outputChannels->setValue(channels);
}

void PwAsioDialog::setSampleRate(uint32_t rate) {
	sampleRate = rate;
	ui.sampleRate->setCurrentText(QString::asprintf("%u", rate));
	updateLatencyDisplay();
}

void PwAsioDialog::setAutoConnect(bool connect) {
	autoConnect = connect;
	ui.autoConnect->setChecked(connect);
}

void PwAsioDialog::loadConfiguration() {
	struct pw_helper_init_args config_args;
	pw_asio_init_default_config(&config_args);

	char *config_path = pw_asio_get_config_path();
	bool config_loaded = false;

	if (config_path) {
		if (pw_asio_load_config(&config_args, config_path) == 0) {
			config_loaded = true;
			printf("Loaded configuration from: %s\n", config_path);
		}
		free(config_path);
	}

	// Apply settings to UI
	setBufferSize(config_args.buffer_size);
	setInputChannels(config_args.num_input_channels);
	setOutputChannels(config_args.num_output_channels);
	setSampleRate(config_args.sample_rate);
	setAutoConnect(config_args.auto_connect);

	if (!config_loaded) {
		printf("No configuration file found, using defaults\n");
	}
}

void PwAsioDialog::saveConfiguration() {
	char *config_path = pw_asio_get_user_config_path();
	if (!config_path) {
		printf("Cannot save configuration: unable to determine config path\n");
		return;
	}

	struct pw_helper_init_args config_args;
	pw_asio_init_default_config(&config_args);

	config_args.buffer_size = bufferSize;
	config_args.num_input_channels = inputChannels;
	config_args.num_output_channels = outputChannels;
	config_args.sample_rate = sampleRate;
	config_args.auto_connect = autoConnect;

	if (pw_asio_save_config(&config_args, config_path) == 0) {
		printf("Configuration saved to: %s\n", config_path);

		// Inform user that buffer size preference is saved
		// (quantum change will apply on next audio start)
		setPipeWireQuantum(bufferSize);
	} else {
		printf("Failed to save configuration to: %s\n", config_path);
	}

	free(config_path);
}

void PwAsioDialog::setPipeWireQuantum(uint32_t quantum) {
	printf("GUI: Requesting PipeWire quantum change to %u samples\n", quantum);

	// Note: The quantum is best set via pipewire.conf or by the driver itself
	// when connecting the filter. The GUI should only store the preference
	// and let the driver handle the actual PipeWire configuration.
	//
	// Previously this used system("pw-metadata ...") which is a security risk
	// and may not work in sandboxed environments.

	// Store the quantum preference - driver will apply it on next connection
	bufferSize = quantum;

	// Inform user about manual steps if needed
	printf("GUI: Buffer size preference saved. Changes will apply on next audio start.\n");
	printf("GUI: For immediate effect, restart the audio application.\n");

	// TODO: If we need immediate quantum change, we should:
	// 1. Signal the driver to reconnect the filter
	// 2. Or expose a pw_metadata API through the helper
}

void PwAsioDialog::bufferSizeSet(QString const &text) {
	bool ok = false;
	uint32_t size = text.toUInt(&ok);
	if (!ok) {
		showValidationError("Buffer size must be a number");
		return;
	}

	if (!isValidBufferSize(size)) {
		showValidationError(QString("Buffer size must be a power of 2 between %1 and %2")
			.arg(PW_ASIO_MIN_BUFFER_SIZE)
			.arg(PW_ASIO_MAX_BUFFER_SIZE));
		// Reset to previous valid value
		ui.bufferSize->setCurrentText(QString::number(bufferSize));
		return;
	}

	printf("Buffer size changed: %u\n", size);
	bufferSize = size;
	clearValidationError();
	updateLatencyDisplay();
}

void PwAsioDialog::inputChannelsChanged(int channels) {
	if (channels < 1 || channels > 64) {
		showValidationError("Input channels must be between 1 and 64");
		ui.inputChannels->setValue(static_cast<int>(inputChannels));
		return;
	}

	printf("Input channels changed: %d\n", channels);
	inputChannels = static_cast<uint32_t>(channels);
	clearValidationError();
}

void PwAsioDialog::outputChannelsChanged(int channels) {
	if (channels < 1 || channels > 64) {
		showValidationError("Output channels must be between 1 and 64");
		ui.outputChannels->setValue(static_cast<int>(outputChannels));
		return;
	}

	printf("Output channels changed: %d\n", channels);
	outputChannels = static_cast<uint32_t>(channels);
	clearValidationError();
}

void PwAsioDialog::sampleRateChanged(QString const &text) {
	bool ok = false;
	uint32_t rate = text.toUInt(&ok);
	if (!ok || !isValidSampleRate(rate)) {
		showValidationError("Invalid sample rate. Use: 44100, 48000, 96000, etc.");
		ui.sampleRate->setCurrentText(QString::number(sampleRate));
		return;
	}

	printf("Sample rate changed: %u\n", rate);
	sampleRate = rate;
	clearValidationError();
	updateLatencyDisplay();
}

void PwAsioDialog::autoConnectChanged(bool enabled) {
	printf("Auto-connect changed: %s\n", enabled ? "true" : "false");
	autoConnect = enabled;
}

// --- Validation Helper Methods ---

bool PwAsioDialog::isValidBufferSize(uint32_t size) {
	// Must be power of 2
	if (size == 0 || (size & (size - 1)) != 0) {
		return false;
	}
	// Must be in valid range
	return size >= PW_ASIO_MIN_BUFFER_SIZE && size <= PW_ASIO_MAX_BUFFER_SIZE;
}

bool PwAsioDialog::isValidSampleRate(uint32_t rate) {
	static const uint32_t validRates[] = {
		8000, 11025, 16000, 22050, 32000, 44100,
		48000, 88200, 96000, 176400, 192000
	};
	for (size_t i = 0; i < sizeof(validRates)/sizeof(validRates[0]); i++) {
		if (rate == validRates[i]) return true;
	}
	return false;
}

void PwAsioDialog::showValidationError(const QString &message) {
	if (validationLabel) {
		validationLabel->setText(message);
		validationLabel->setStyleSheet("QLabel { color: red; font-weight: bold; }");
	}
}

void PwAsioDialog::clearValidationError() {
	if (validationLabel) {
		validationLabel->clear();
	}
}

// --- UI Update Helpers ---

void PwAsioDialog::updateLatencyDisplay() {
	if (!latencyLabel) return;

	if (sampleRate > 0 && bufferSize > 0) {
		double latency_ms = (double)bufferSize * 1000.0 / (double)sampleRate;
		latencyLabel->setText(QString("Latency: %1 ms (%2 samples @ %3 Hz)")
			.arg(latency_ms, 0, 'f', 2)
			.arg(bufferSize)
			.arg(sampleRate));
	} else {
		latencyLabel->setText("Latency: --");
	}
}

void PwAsioDialog::populateBufferSizePresets() {
	ui.bufferSize->clear();

	// Common buffer sizes (powers of 2)
	QStringList presets = {"32", "64", "128", "256", "512", "1024", "2048", "4096", "8192"};
	ui.bufferSize->addItems(presets);

	// Make it editable for custom values
	ui.bufferSize->setEditable(true);
	ui.bufferSize->setInsertPolicy(QComboBox::NoInsert);

	// Set default selection to 1024
	int defaultIndex = presets.indexOf("1024");
	if (defaultIndex >= 0) {
		ui.bufferSize->setCurrentIndex(defaultIndex);
	}
}

void PwAsioDialog::populateSampleRatePresets() {
	ui.sampleRate->clear();

	// Common sample rates - prioritize commonly used ones
	QStringList presets = {"44100", "48000", "88200", "96000", "176400", "192000"};
	ui.sampleRate->addItems(presets);

	ui.sampleRate->setEditable(true);
	ui.sampleRate->setInsertPolicy(QComboBox::NoInsert);

	// Default to 48000
	int defaultIndex = presets.indexOf("48000");
	if (defaultIndex >= 0) {
		ui.sampleRate->setCurrentIndex(defaultIndex);
	}
}
