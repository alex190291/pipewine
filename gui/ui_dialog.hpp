/********************************************************************************
** Form generated from reading UI file 'dialog.ui'
**
** Created by: Qt User Interface Compiler version 6.9.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_DIALOG_H
#define UI_DIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_PwAsioDialog
{
public:
    QVBoxLayout *verticalLayout_3;
    QVBoxLayout *verticalLayout;
    QFormLayout *formLayout;
    QLabel *label;
    QComboBox *bufferSize;
    QLabel *label_5;
    QSpinBox *inputChannels;
    QLabel *label_6;
    QSpinBox *outputChannels;
    QLabel *label_7;
    QComboBox *sampleRate;
    QLabel *label_8;
    QCheckBox *autoConnect;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *PwAsioDialog)
    {
        if (PwAsioDialog->objectName().isEmpty())
            PwAsioDialog->setObjectName("PwAsioDialog");
        PwAsioDialog->resize(375, 278);
        verticalLayout_3 = new QVBoxLayout(PwAsioDialog);
        verticalLayout_3->setObjectName("verticalLayout_3");
        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName("verticalLayout");
        formLayout = new QFormLayout();
        formLayout->setObjectName("formLayout");
        label = new QLabel(PwAsioDialog);
        label->setObjectName("label");

        formLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, label);

        bufferSize = new QComboBox(PwAsioDialog);
        bufferSize->addItem(QString::fromUtf8("256"));
        bufferSize->addItem(QString::fromUtf8("384"));
        bufferSize->addItem(QString::fromUtf8("512"));
        bufferSize->addItem(QString::fromUtf8("1024"));
        bufferSize->addItem(QString::fromUtf8("2024"));
        bufferSize->setObjectName("bufferSize");
        bufferSize->setFocusPolicy(Qt::StrongFocus);
        bufferSize->setInputMethodHints(Qt::ImhDigitsOnly);
        bufferSize->setEditable(true);
        bufferSize->setCurrentText(QString::fromUtf8("256"));
        bufferSize->setPlaceholderText(QString::fromUtf8(""));

        formLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, bufferSize);

        label_5 = new QLabel(PwAsioDialog);
        label_5->setObjectName("label_5");

        formLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, label_5);

        inputChannels = new QSpinBox(PwAsioDialog);
        inputChannels->setObjectName("inputChannels");
        inputChannels->setMinimum(0);
        inputChannels->setMaximum(64);
        inputChannels->setValue(16);

        formLayout->setWidget(1, QFormLayout::ItemRole::FieldRole, inputChannels);

        label_6 = new QLabel(PwAsioDialog);
        label_6->setObjectName("label_6");

        formLayout->setWidget(2, QFormLayout::ItemRole::LabelRole, label_6);

        outputChannels = new QSpinBox(PwAsioDialog);
        outputChannels->setObjectName("outputChannels");
        outputChannels->setMinimum(0);
        outputChannels->setMaximum(64);
        outputChannels->setValue(16);

        formLayout->setWidget(2, QFormLayout::ItemRole::FieldRole, outputChannels);

        label_7 = new QLabel(PwAsioDialog);
        label_7->setObjectName("label_7");

        formLayout->setWidget(3, QFormLayout::ItemRole::LabelRole, label_7);

        sampleRate = new QComboBox(PwAsioDialog);
        sampleRate->addItem(QString::fromUtf8("8000"));
        sampleRate->addItem(QString::fromUtf8("11025"));
        sampleRate->addItem(QString::fromUtf8("16000"));
        sampleRate->addItem(QString::fromUtf8("22050"));
        sampleRate->addItem(QString::fromUtf8("32000"));
        sampleRate->addItem(QString::fromUtf8("44100"));
        sampleRate->addItem(QString::fromUtf8("48000"));
        sampleRate->addItem(QString::fromUtf8("88200"));
        sampleRate->addItem(QString::fromUtf8("96000"));
        sampleRate->addItem(QString::fromUtf8("176400"));
        sampleRate->addItem(QString::fromUtf8("192000"));
        sampleRate->setObjectName("sampleRate");
        sampleRate->setCurrentText(QString::fromUtf8("44100"));

        formLayout->setWidget(3, QFormLayout::ItemRole::FieldRole, sampleRate);

        label_8 = new QLabel(PwAsioDialog);
        label_8->setObjectName("label_8");

        formLayout->setWidget(4, QFormLayout::ItemRole::LabelRole, label_8);

        autoConnect = new QCheckBox(PwAsioDialog);
        autoConnect->setObjectName("autoConnect");
        autoConnect->setChecked(true);

        formLayout->setWidget(4, QFormLayout::ItemRole::FieldRole, autoConnect);


        verticalLayout->addLayout(formLayout);


        verticalLayout_3->addLayout(verticalLayout);

        buttonBox = new QDialogButtonBox(PwAsioDialog);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout_3->addWidget(buttonBox);


        retranslateUi(PwAsioDialog);
        QObject::connect(buttonBox, &QDialogButtonBox::accepted, PwAsioDialog, qOverload<>(&QDialog::accept));
        QObject::connect(buttonBox, &QDialogButtonBox::rejected, PwAsioDialog, qOverload<>(&QDialog::reject));
        QObject::connect(bufferSize, SIGNAL(editTextChanged(QString)), PwAsioDialog, SLOT(bufferSizeSet(QString)));
        QObject::connect(inputChannels, SIGNAL(valueChanged(int)), PwAsioDialog, SLOT(inputChannelsChanged(int)));
        QObject::connect(outputChannels, SIGNAL(valueChanged(int)), PwAsioDialog, SLOT(outputChannelsChanged(int)));
        QObject::connect(sampleRate, SIGNAL(currentIndexChanged(QString)), PwAsioDialog, SLOT(sampleRateChanged(QString)));
        QObject::connect(autoConnect, SIGNAL(toggled(bool)), PwAsioDialog, SLOT(autoConnectChanged(bool)));

        QMetaObject::connectSlotsByName(PwAsioDialog);
    } // setupUi

    void retranslateUi(QDialog *PwAsioDialog)
    {
        PwAsioDialog->setWindowTitle(QCoreApplication::translate("PwAsioDialog", "PipeWire ASIO Settings", nullptr));
        label->setText(QCoreApplication::translate("PwAsioDialog", "Buffer size", nullptr));

        label_5->setText(QCoreApplication::translate("PwAsioDialog", "Input channels", nullptr));
        label_6->setText(QCoreApplication::translate("PwAsioDialog", "Output channels", nullptr));
        label_7->setText(QCoreApplication::translate("PwAsioDialog", "Sample rate", nullptr));

        label_8->setText(QCoreApplication::translate("PwAsioDialog", "Auto-connect", nullptr));
        autoConnect->setText(QCoreApplication::translate("PwAsioDialog", "Automatically connect to hardware", nullptr));
    } // retranslateUi

};

namespace Ui {
    class PwAsioDialog: public Ui_PwAsioDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_DIALOG_H
