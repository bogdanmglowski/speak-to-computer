#pragma once

#include "AppSettings.h"

#include <QDialog>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QLineEdit;
class QSpinBox;

class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(const AppSettings &settings, QWidget *parent = nullptr);

    AppSettings editedSettings() const;

private slots:
    void updateEngineFields();

private:
    AppSettings baseSettings_;
    QLineEdit *hotkeyDictateEdit_ = nullptr;
    QLineEdit *hotkeyTranslateEnEdit_ = nullptr;
    QComboBox *engineCombo_ = nullptr;
    QComboBox *audioBackendCombo_ = nullptr;
    QLineEdit *languageEdit_ = nullptr;
    QLineEdit *whisperCliEdit_ = nullptr;
    QLineEdit *modelEdit_ = nullptr;
    QComboBox *realtimeModelCombo_ = nullptr;
    QLineEdit *realtimePythonEdit_ = nullptr;
    QLineEdit *realtimeServerScriptEdit_ = nullptr;
    QLineEdit *buzzAsrPythonEdit_ = nullptr;
    QLineEdit *buzzAsrScriptEdit_ = nullptr;
    QLineEdit *dictionaryPathEdit_ = nullptr;
    QComboBox *pasteTargetCombo_ = nullptr;
    QCheckBox *miniOverlayCheck_ = nullptr;
    QSpinBox *threadsSpin_ = nullptr;
    QLineEdit *activationSoundEdit_ = nullptr;
    QLineEdit *endSoundEdit_ = nullptr;
    QGroupBox *wakeWordGroup_ = nullptr;
    QLineEdit *wakeWordPhraseEdit_ = nullptr;
    QLineEdit *wakeWordModelPathEdit_ = nullptr;
    QDoubleSpinBox *wakeWordThresholdSpin_ = nullptr;
    QLineEdit *wakeWordSidecarExecutableEdit_ = nullptr;
    QLineEdit *wakeWordSidecarScriptEdit_ = nullptr;
    QGroupBox *vadGroup_ = nullptr;
    QSpinBox *vadAggressivenessSpin_ = nullptr;
    QSpinBox *vadEndSilenceMsSpin_ = nullptr;
    QSpinBox *vadMinSpeechMsSpin_ = nullptr;
    QGroupBox *outputGroup_ = nullptr;
    QComboBox *outputTargetCombo_ = nullptr;
    QLineEdit *handoffDirectoryEdit_ = nullptr;
    QLineEdit *handoffTriggerWordsEdit_ = nullptr;
};
