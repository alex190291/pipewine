from PyQt5.QtWidgets import QGroupBox

from ui_device_selector import Ui_PwIODeviceSelector

class PwIODeviceSelector(QGroupBox, Ui_PwIODeviceSelector):
	def __init__(self, parent):
		QGroupBox.__init__(self, parent)
		self.setupUi(self)
