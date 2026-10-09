#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>

#include "dialog.hpp"
#include "gui.h"

// Use C API instead of C++ API to avoid symbol linking issues
extern "C" {
#include "../pw_helper_c.h"
#include "../pw_helper_common.h"
}

// GUI state management - simplified without threading
struct pwasio_gui {
	struct pwasio_gui_conf *conf;
	PwAsioDialog *dialog;
	QApplication *app;
	int argc = 1;
	char appName[16] = "pipewine";
	char *argv[2] = {appName, nullptr};
	
	pwasio_gui(struct pwasio_gui_conf *conf) 
		: conf(conf), dialog(nullptr), app(nullptr) {}
	
	~pwasio_gui() {
		delete dialog;
		delete app;
	}
};

extern "C" {

__attribute__((visibility("default")))
struct pwasio_gui *pwasio_init_gui(struct pwasio_gui_conf *conf) {
	if (!conf) {
		return nullptr;
	}
	
	auto *gui = new pwasio_gui(conf);
	
	// Create QApplication if it doesn't exist
	if (!QApplication::instance()) {
		gui->app = new QApplication(gui->argc, gui->argv);
		gui->app->setApplicationName("PipeWire ASIO Settings");
	}
	
	// Create dialog
	gui->dialog = new PwAsioDialog(gui->conf->pw_helper);
	
	// Connect dialog signals
	QObject::connect(gui->dialog, &QDialog::accepted, [gui]() {
		// Update configuration structure with current dialog values
		gui->conf->cf_buffer_size = gui->dialog->getBufferSize();
		gui->conf->cf_sample_rate = gui->dialog->getSampleRate();
		gui->conf->cf_input_channels = gui->dialog->getInputChannels();
		gui->conf->cf_output_channels = gui->dialog->getOutputChannels();
		gui->conf->cf_auto_connect = gui->dialog->getAutoConnect();
		
		// Apply configuration when user clicks OK
		if (gui->conf->apply_config) {
			gui->conf->apply_config(gui->conf);
		}
	});

	// exec() owns the modal lifetime. The caller destroys the GUI only after
	// it returns; deleting the dialog in accepted/rejected is unsafe.

	// Load current configuration
	if (gui->conf->load_config) {
		gui->conf->load_config(gui->conf);
	}
	
	// Set dialog values from configuration
	gui->dialog->setBufferSize(gui->conf->cf_buffer_size);
	gui->dialog->setSampleRate(gui->conf->cf_sample_rate);
	gui->dialog->setInputChannels(gui->conf->cf_input_channels);
	gui->dialog->setOutputChannels(gui->conf->cf_output_channels);
	gui->dialog->setAutoConnect(gui->conf->cf_auto_connect);
	
	// Show dialog modally
	gui->dialog->exec();
	
	return gui;
}

__attribute__((visibility("default")))
void pwasio_destroy_gui(struct pwasio_gui *gui) {
	if (!gui) {
		return;
	}
	
	// Close dialog if it exists
	if (gui->dialog) {
		gui->dialog->close();
	}
	
	delete gui;
}

}
