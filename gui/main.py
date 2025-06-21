#!/bin/env python3

import sys

from PyQt5.QtCore import pyqtSlot
from PyQt5.QtWidgets import QApplication, QDialog, QButtonGroup

from ui_dialog import Ui_PwAsioDialog

class PwConfigGUI(QDialog, Ui_PwAsioDialog):
	def __init__(self):
		QDialog.__init__(self, None)
		self.setupUi(self)
		self.layout_group = QButtonGroup()
		self.layout_group.addButton(self.layoutButton_0, 0)
		self.layout_group.addButton(self.layoutButton_1, 1)
		self.layout_group.buttonClicked.connect(self.layoutButtonClicked)

	def layoutButtonClicked(self, button):
		bid = self.layout_group.id(button)
		#print(f'[DBG] Button clicked: {bid} ({button})')
		checked = self.layout_group.checkedButton()
		cid = self.layout_group.id(checked)
		#print(f'[DBG] Currently checked: {cid} ({checked})')
		self.io_config.setCurrentIndex(cid)

def main():
	app = QApplication(sys.argv)
	app.setApplicationName('PipeWire ASIO Settings')

	gui = PwConfigGUI()
	gui.show()

	sys.exit(app.exec_())

if __name__ == '__main__':
	main()
