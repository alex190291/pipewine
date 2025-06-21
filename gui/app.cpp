#include <QtWidgets/QApplication>

#include "dialog.hpp"

// Use C API instead of C++ API to avoid symbol linking issues
extern "C" {
#include "../pw_helper_c.h"
#include "../pw_helper_common.h"
}

int main(int argc, char *argv[]) {
	QApplication app(argc, argv);
	app.setApplicationName("PipeWire ASIO Settings");
	
	// Initialize PipeWire helper using C API
	struct pw_helper_init_args init_args = {
		.app_name = "pw-asio settings",
	};
	struct user_pw_helper *helper = user_pw_create_helper(argc, argv, &init_args);
	if (!helper) {
		return 1;
	}
	
	PwAsioDialog dialog(helper);
	int result = dialog.exec();
	
	user_pw_destroy_helper(helper);
	return result;
}
