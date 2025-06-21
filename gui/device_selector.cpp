#include "device_selector.hpp"
#include "device_selector.moc"

#include <QtWidgets/QWidget>
#include <QtWidgets/QGroupBox>

PwIODeviceSelector::PwIODeviceSelector(QWidget *parent)
	: QGroupBox(parent)
{
	ui.setupUi(this);
}

PwIODeviceSelector::~PwIODeviceSelector() = default;
