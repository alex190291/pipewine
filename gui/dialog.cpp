#include "dialog.hpp"
#include "dialog.moc"

// Use C API instead of C++ API
extern "C" {
#include "../pw_helper_c.h"
#include "../pw_helper_common.h"
}

#include <QtCore/qglobal.h>
#include <QtGui/QIntValidator>

PwAsioDialog::PwAsioDialog(struct user_pw_helper *helper)
	: pw_helper(helper)
	, bufferSize(1024)
	, inputChannels(16)
	, outputChannels(16)
	, sampleRate(44100)
	, autoConnect(true)
{
	ui.setupUi(this);
	ui.bufferSize->setValidator(new QIntValidator(1, INT_MAX, this));
	
	// Connect accept signal to save configuration
	connect(this, &QDialog::accepted, this, &PwAsioDialog::saveConfiguration);
	
	// Load current configuration
	loadConfiguration();
}

PwAsioDialog::~PwAsioDialog() {
}

void PwAsioDialog::setBufferSize(uint32_t newBufferSize) {
	bufferSize = newBufferSize;
	ui.bufferSize->setCurrentText(QString::asprintf("%u", newBufferSize));
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
}

void PwAsioDialog::setAutoConnect(bool connect) {
	autoConnect = connect;
	ui.autoConnect->setChecked(connect);
}

void PwAsioDialog::loadConfiguration() {
	// Try to load configuration from file
	struct pw_helper_init_args config_args;
	const char *config_paths[] = {
		"/etc/pipewine/pipewine.conf",
		nullptr,
		nullptr
	};
	char user_config_path[512];
	
	// Construct user config path
	const char *home = getenv("HOME");
	if (home) {
		snprintf(user_config_path, sizeof(user_config_path), 
				"%s/.config/pipewine/pipewine.conf", home);
		config_paths[1] = user_config_path;
	}
	
	// Try to load configuration from files (user config takes precedence)
	bool config_loaded = false;
	for (int i = 1; i >= 0 && !config_loaded; i--) {
		if (config_paths[i] && pw_asio_load_config(&config_args, config_paths[i]) == 0) {
			config_loaded = true;
			
			// Apply loaded settings to UI
			setBufferSize(config_args.buffer_size);
			setInputChannels(config_args.num_input_channels);
			setOutputChannels(config_args.num_output_channels);
			setSampleRate(config_args.sample_rate);
			setAutoConnect(config_args.auto_connect);
			
			printf("Loaded configuration from: %s\n", config_paths[i]);
			break;
		}
	}
	
	if (!config_loaded) {
		printf("No configuration file found, using defaults\n");
		// Set default values in UI
		setBufferSize(bufferSize);
		setInputChannels(inputChannels);
		setOutputChannels(outputChannels);
		setSampleRate(sampleRate);
		setAutoConnect(autoConnect);
	}
}

void PwAsioDialog::saveConfiguration() {
	// Save to user config directory
	const char *home = getenv("HOME");
	if (!home) {
		printf("Cannot save configuration: HOME environment variable not set\n");
		return;
	}
	
	char config_dir[512];
	char config_path[512];
	snprintf(config_dir, sizeof(config_dir), "%s/.config/pipewine", home);
	snprintf(config_path, sizeof(config_path), "%s/pipewine.conf", config_dir);
	
	// Create directory if it doesn't exist
	system(("mkdir -p " + std::string(config_dir)).c_str());
	
	// Prepare configuration structure
	struct pw_helper_init_args config_args;
	pw_asio_init_default_config(&config_args);
	
	config_args.buffer_size = bufferSize;
	config_args.num_input_channels = inputChannels;
	config_args.num_output_channels = outputChannels;
	config_args.sample_rate = sampleRate;
	config_args.auto_connect = autoConnect;
	
	// Save configuration
	if (pw_asio_save_config(&config_args, config_path) == 0) {
		printf("Configuration saved to: %s\n", config_path);
		
		// Set PipeWire quantum to match the buffer size for optimal performance
		setPipeWireQuantum(bufferSize);
	} else {
		printf("Failed to save configuration to: %s\n", config_path);
	}
}

void PwAsioDialog::setPipeWireQuantum(uint32_t quantum) {
	printf("GUI: Setting PipeWire quantum to %u samples for optimal sync\n", quantum);
	
	char quantum_cmd[256];
	snprintf(quantum_cmd, sizeof(quantum_cmd), 
			"pw-metadata -n settings 0 clock.quantum %u", quantum);
	
	int result = system(quantum_cmd);
	if (result == 0) {
		printf("GUI: Successfully set PipeWire quantum to %u samples\n", quantum);
		printf("GUI: PipeWire quantum synchronized with ASIO buffer size\n");
	} else {
		printf("GUI: Warning - Failed to set PipeWire quantum (command may not be available)\n");
		printf("GUI: You may need to install pipewire-utils package\n");
	}
}

void PwAsioDialog::bufferSizeSet(QString const &text) {
	bool ok = false;
	uint32_t size = text.toUInt(&ok);
	if (!ok)
		return;

	printf("Buffer size changed: %u\n", size);
	bufferSize = size;
}

void PwAsioDialog::inputChannelsChanged(int channels) {
	printf("Input channels changed: %d\n", channels);
	inputChannels = channels;
}

void PwAsioDialog::outputChannelsChanged(int channels) {
	printf("Output channels changed: %d\n", channels);
	outputChannels = channels;
}

void PwAsioDialog::sampleRateChanged(QString const &text) {
	bool ok = false;
	uint32_t rate = text.toUInt(&ok);
	if (!ok)
		return;

	printf("Sample rate changed: %u\n", rate);
	sampleRate = rate;
}

void PwAsioDialog::autoConnectChanged(bool enabled) {
	printf("Auto-connect changed: %s\n", enabled ? "true" : "false");
	autoConnect = enabled;
}
