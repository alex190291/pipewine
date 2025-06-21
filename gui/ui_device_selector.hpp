/********************************************************************************
** Form generated from reading UI file 'device_selector.ui'
**
** Created by: Qt User Interface Compiler version 6.9.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_DEVICE_SELECTOR_H
#define UI_DEVICE_SELECTOR_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QListView>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_PwIODeviceSelector
{
public:
    QHBoxLayout *horizontalLayout;
    QListView *listView;
    QVBoxLayout *verticalLayout;
    QSpacerItem *verticalSpacer;
    QPushButton *pushButton;
    QPushButton *pushButton_2;
    QSpacerItem *verticalSpacer_2;
    QListView *listView_2;

    void setupUi(QGroupBox *PwIODeviceSelector)
    {
        if (PwIODeviceSelector->objectName().isEmpty())
            PwIODeviceSelector->setObjectName("PwIODeviceSelector");
        PwIODeviceSelector->resize(395, 304);
        horizontalLayout = new QHBoxLayout(PwIODeviceSelector);
        horizontalLayout->setObjectName("horizontalLayout");
        listView = new QListView(PwIODeviceSelector);
        listView->setObjectName("listView");

        horizontalLayout->addWidget(listView);

        verticalLayout = new QVBoxLayout();
        verticalLayout->setSpacing(6);
        verticalLayout->setObjectName("verticalLayout");
        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        pushButton = new QPushButton(PwIODeviceSelector);
        pushButton->setObjectName("pushButton");

        verticalLayout->addWidget(pushButton);

        pushButton_2 = new QPushButton(PwIODeviceSelector);
        pushButton_2->setObjectName("pushButton_2");

        verticalLayout->addWidget(pushButton_2);

        verticalSpacer_2 = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        verticalLayout->addItem(verticalSpacer_2);


        horizontalLayout->addLayout(verticalLayout);

        listView_2 = new QListView(PwIODeviceSelector);
        listView_2->setObjectName("listView_2");

        horizontalLayout->addWidget(listView_2);


        retranslateUi(PwIODeviceSelector);

        QMetaObject::connectSlotsByName(PwIODeviceSelector);
    } // setupUi

    void retranslateUi(QGroupBox *PwIODeviceSelector)
    {
        PwIODeviceSelector->setWindowTitle(QCoreApplication::translate("PwIODeviceSelector", "GroupBox", nullptr));
        pushButton->setText(QCoreApplication::translate("PwIODeviceSelector", "Add", nullptr));
        pushButton_2->setText(QCoreApplication::translate("PwIODeviceSelector", "Remove", nullptr));
    } // retranslateUi

};

namespace Ui {
    class PwIODeviceSelector: public Ui_PwIODeviceSelector {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_DEVICE_SELECTOR_H
