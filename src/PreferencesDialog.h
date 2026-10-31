#pragma once

#include "AppSettings.h"

#include <QDialog>
#include <QProcess>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QLabel;
class QPushButton;
class QDialogButtonBox;

class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(const AppSettings &settings, QWidget *parent = nullptr);

    AppSettings editedSettings() const;

protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;

private:
    void startDiarizationSetup();
    void nextDiarizationSetupStep();
    void finishDiarizationSetup(const QString &message);
private slots:
    void updateEngineFields();

private:
    AppSettings baseSettings_;
    QLineEdit *hotkeyDictateEdit_ = nullptr;
    QLineEdit *hotkeyTranslateEnEdit_ = nullptr;
    QSpinBox *pushToTalkButtonSpin_ = nullptr;
    QCheckBox *pushToTalkEnterCheck_ = nullptr;
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
    QLineEdit *parakeetPythonEdit_ = nullptr;
    QLineEdit *parakeetScriptEdit_ = nullptr;
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
    QLineEdit *meetingsDirectoryEdit_ = nullptr;
    QLineEdit *huggingFaceTokenEdit_ = nullptr;
    QLineEdit *huggingFaceHomeEdit_ = nullptr;
    QLineEdit *diarizationRuntimeEdit_ = nullptr;
    QListWidget *analysisOllamaServers_ = nullptr;
    QLineEdit *analysisOllamaUrlEdit_ = nullptr;
    QLabel *analysisCheckStatus_ = nullptr;
    QProcess ollamaCheckProcess_;
    QLabel *diarizationStatus_ = nullptr;
    QPushButton *diarizationSetup_ = nullptr;
    QPushButton *diarizationStop_ = nullptr;
    QDialogButtonBox *buttonBox_ = nullptr;
    QProcess setupProcess_;
    int setupStage_ = 0;
    bool setupStopping_ = false;
    QByteArray setupOutput_;
};
