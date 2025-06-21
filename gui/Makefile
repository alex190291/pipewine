#!/usr/bin/make -f

ifneq ($(M),)
ARCH_FLAG = -m$(M)
endif

CXXFLAGS = -fpic -std=c++20 -ggdb $(ARCH_FLAG) -fvisibility=hidden -fPIC -I/usr/include/qt6 -I/usr/include/qt6/QtWidgets -I/usr/include/qt6/QtCore -I/usr/include/qt6/QtGui
CFLAGS = -fpic -std=c11 -ggdb $(ARCH_FLAG) -fvisibility=hidden
LFLAGS = $(ARCH_FLAG) -lQt6Core -lQt6Widgets -lQt6Gui -fpic -lpipewire-0.3 -shared

BUILD_DIR = ../build$(M)
COMMON_OBJS := dialog.o device_selector.o device_chooser.o

QT_CFLAGS = -isystem /usr/include/qt6
PW_CFLAGS = -isystem /usr/include/pipewire-0.3 -isystem /usr/include/spa-0.2

# Use the Qt6 uic binary
UIC = /usr/lib64/qt6/libexec/uic
MOC = /usr/lib64/qt6/libexec/moc

# Main target
$(BUILD_DIR)/libpwasio_gui.so: $(BUILD_DIR)/dialog.o $(BUILD_DIR)/device_selector.o $(BUILD_DIR)/device_chooser.o $(BUILD_DIR)/gui.o $(BUILD_DIR)/pw_helper.o $(BUILD_DIR)/pw_config_utils.o
	@$(shell mkdir -p $(BUILD_DIR))
	$(CXX) -o $@ $(LFLAGS) $^

# Dependencies
$(BUILD_DIR)/dialog.o: dialog.cpp dialog.moc ui_dialog.hpp
$(BUILD_DIR)/device_selector.o: device_selector.cpp device_selector.moc ui_device_selector.hpp  
$(BUILD_DIR)/device_chooser.o: device_chooser.cpp device_chooser.moc
$(BUILD_DIR)/gui.o: gui.cpp
$(BUILD_DIR)/pw_helper.o: ../pw_helper.cpp
$(BUILD_DIR)/pw_config_utils.o: ../pw_config_utils.c

# Object file compilation
$(BUILD_DIR)/%.o: %.cpp
	@$(shell mkdir -p $(BUILD_DIR))
	$(CXX) -c -o $@ $(CXXFLAGS) $(QT_CFLAGS) $(PW_CFLAGS) $<

# Object file compilation for parent directory files
$(BUILD_DIR)/pw_helper.o: ../pw_helper.cpp
	@$(shell mkdir -p $(BUILD_DIR))
	$(CXX) -c -o $@ $(CXXFLAGS) $(QT_CFLAGS) $(PW_CFLAGS) $<

$(BUILD_DIR)/pw_config_utils.o: ../pw_config_utils.c
	@$(shell mkdir -p $(BUILD_DIR))
	$(CC) -c -o $@ $(CFLAGS) $(PW_CFLAGS) $<

# MOC file generation
%.moc: %.hpp
	$(MOC) -o $@ $<

# UI header generation  
ui_%.hpp: %.ui
	$(UIC) $< -o $@

# Clean target
clean:
	rm -f *.moc ui_*.hpp $(BUILD_DIR)/*.o $(BUILD_DIR)/libpwasio_gui.so

.PHONY: clean 