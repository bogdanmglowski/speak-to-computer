#include "PreferencesDialog.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QFormLayout *createFormLayout(QWidget *parent)
{
    auto *layout = new QFormLayout(parent);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return layout;
}

void setComboValue(QComboBox *combo, const QString &value)
{
    const int existingIndex = combo->findText(value);
    if (existingIndex >= 0) {
        combo->setCurrentIndex(existingIndex);
        return;
    }

    combo->addItem(value);
    combo->setCurrentIndex(combo->count() - 1);
}

} // namespace

PreferencesDialog::PreferencesDialog(const AppSettings &settings, QWidget *parent)
    : QDialog(parent)
    , baseSettings_(settings)
{
    setWindowTitle(QStringLiteral("Preferences"));
    resize(920, 760);

    auto *mainLayout = new QVBoxLayout(this);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    mainLayout->addWidget(scrollArea);

    auto *content = new QWidget(scrollArea);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);

    auto *infoLabel = new QLabel(QStringLiteral("Update runtime, hotkeys, wake word, audio, and VAD settings in one place."), content);
    infoLabel->setWordWrap(true);
    contentLayout->addWidget(infoLabel);

    auto *sectionsLayout = new QGridLayout();
    sectionsLayout->setHorizontalSpacing(16);
    sectionsLayout->setVerticalSpacing(12);
    sectionsLayout->setColumnStretch(0, 1);
    sectionsLayout->setColumnStretch(1, 1);
    contentLayout->addLayout(sectionsLayout);

    auto *hotkeysSection = new QGroupBox(QStringLiteral("Hotkeys"), content);
    auto *hotkeysLayout = createFormLayout(hotkeysSection);
    hotkeyDictateEdit_ = new QLineEdit(settings.hotkeyDictate, hotkeysSection);
    hotkeyTranslateEnEdit_ = new QLineEdit(settings.hotkeyTranslateEn, hotkeysSection);
    hotkeysLayout->addRow(QStringLiteral("Dictation"), hotkeyDictateEdit_);
    hotkeysLayout->addRow(QStringLiteral("Translate to English"), hotkeyTranslateEnEdit_);
    pushToTalkButtonSpin_ = new QSpinBox(hotkeysSection);
    pushToTalkButtonSpin_->setRange(0, 255);
    pushToTalkButtonSpin_->setValue(settings.pushToTalkButton);
    pushToTalkButtonSpin_->setToolTip(QStringLiteral("X11 button number; 11 is Hex Mouse 7. 0 disables push-to-talk. Buttons 9 and 10 are reserved."));
    pushToTalkEnterCheck_ = new QCheckBox(QStringLiteral("Press Enter after pasting"), hotkeysSection);
    pushToTalkEnterCheck_->setChecked(settings.pushToTalkSendEnter);
    pushToTalkEnterCheck_->setToolTip(QStringLiteral("Warning: Enter can execute commands in a terminal."));
    hotkeysLayout->addRow(QStringLiteral("Push-to-talk X11 button"), pushToTalkButtonSpin_);
    hotkeysLayout->addRow(QStringLiteral("Push-to-talk"), pushToTalkEnterCheck_);
    hotkeysSection->setLayout(hotkeysLayout);
    sectionsLayout->addWidget(hotkeysSection, 0, 0);

    auto *transcriptionSection = new QGroupBox(QStringLiteral("Transcription"), content);
    auto *transcriptionLayout = createFormLayout(transcriptionSection);
    engineCombo_ = new QComboBox(transcriptionSection);
    engineCombo_->addItem(QStringLiteral("whisper.cpp"), QString::fromUtf8(AppSettings::kEngineWhisperCpp));
    engineCombo_->addItem(QStringLiteral("Realtime"), QString::fromUtf8(AppSettings::kEngineRealtime));
    engineCombo_->addItem(QStringLiteral("BuzzASR (Polish)"), QString::fromUtf8(AppSettings::kEngineBuzzAsr));
    engineCombo_->addItem(QStringLiteral("Parakeet v3 (auto language)"), QString::fromUtf8(AppSettings::kEngineParakeet));
    {
        const int engineIndex = engineCombo_->findData(AppSettings::normalizeEngine(settings.engine));
        engineCombo_->setCurrentIndex(engineIndex >= 0 ? engineIndex : 0);
    }
    connect(engineCombo_, &QComboBox::currentIndexChanged, this, &PreferencesDialog::updateEngineFields);

    audioBackendCombo_ = new QComboBox(transcriptionSection);
    audioBackendCombo_->addItems({QStringLiteral("auto"), QStringLiteral("pipewire"), QStringLiteral("pulseaudio"), QStringLiteral("alsa")});
    setComboValue(audioBackendCombo_, settings.audioBackend);
    languageEdit_ = new QLineEdit(settings.language, transcriptionSection);
    languageEdit_->setPlaceholderText(QStringLiteral("pl, en, auto"));
    whisperCliEdit_ = new QLineEdit(settings.whisperCli, transcriptionSection);
    modelEdit_ = new QLineEdit(settings.model, transcriptionSection);
    realtimeModelCombo_ = new QComboBox(transcriptionSection);
    realtimeModelCombo_->addItems(AppSettings::realtimeModelChoices());
    setComboValue(realtimeModelCombo_, settings.realtimeModel);
    realtimePythonEdit_ = new QLineEdit(settings.realtimePython, transcriptionSection);
    realtimeServerScriptEdit_ = new QLineEdit(settings.realtimeServerScript, transcriptionSection);
    buzzAsrPythonEdit_ = new QLineEdit(settings.buzzAsrPython, transcriptionSection);
    buzzAsrScriptEdit_ = new QLineEdit(settings.buzzAsrScript, transcriptionSection);
    parakeetPythonEdit_ = new QLineEdit(settings.parakeetPython, transcriptionSection);
    parakeetScriptEdit_ = new QLineEdit(settings.parakeetScript, transcriptionSection);
    auto *dictionaryRow = new QWidget(transcriptionSection);
    auto *dictionaryLayout = new QHBoxLayout(dictionaryRow);
    dictionaryLayout->setContentsMargins(0, 0, 0, 0);
    dictionaryPathEdit_ = new QLineEdit(settings.dictionaryPath, dictionaryRow);
    dictionaryPathEdit_->setToolTip(QStringLiteral("whisper.cpp / Realtime: one name or short phrase per line. Soft hints, not replacements. BuzzASR does not reliably support dictionary prompts."));
    auto *editDictionaryButton = new QPushButton(QStringLiteral("Edit"), dictionaryRow);
    dictionaryLayout->addWidget(dictionaryPathEdit_);
    dictionaryLayout->addWidget(editDictionaryButton);
    connect(editDictionaryButton, &QPushButton::clicked, this, [this]() {
        const QString path = AppSettings::expandUserPath(dictionaryPathEdit_->text().trimmed());
        if (path.isEmpty()) {
            return;
        }
        if (!QFile::exists(path)) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile dictionary(path);
            if (!dictionary.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                QMessageBox::warning(this, QStringLiteral("Dictionary"), dictionary.errorString());
                return;
            }
            dictionary.write("# One name or short phrase per line. Lines starting with # are ignored.\n");
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
            QMessageBox::warning(this, QStringLiteral("Dictionary"), QStringLiteral("Could not open the dictionary in a text editor."));
        }
    });
    pasteTargetCombo_ = new QComboBox(transcriptionSection);
    pasteTargetCombo_->addItem(QStringLiteral("Pin window (at start)"),
            QString::fromUtf8(AppSettings::kPasteTargetPin));
    pasteTargetCombo_->addItem(QStringLiteral("Follow focus"),
            QString::fromUtf8(AppSettings::kPasteTargetFocus));
    {
        const int pasteIndex = pasteTargetCombo_->findData(
                AppSettings::normalizePasteTarget(settings.pasteTarget));
        pasteTargetCombo_->setCurrentIndex(pasteIndex >= 0 ? pasteIndex : 0);
    }
    threadsSpin_ = new QSpinBox(transcriptionSection);
    miniOverlayCheck_ = new QCheckBox(QStringLiteral("Tryb mini"), transcriptionSection);
    miniOverlayCheck_->setChecked(settings.miniOverlayEnabled);
    miniOverlayCheck_->setToolTip(QStringLiteral("Recording: only the voice waveform, elapsed time and GPU memory on a transparent background."));
    threadsSpin_->setRange(1, 256);
    threadsSpin_->setValue(settings.threads);
    transcriptionLayout->addRow(QStringLiteral("Engine"), engineCombo_);
    transcriptionLayout->addRow(QStringLiteral("Audio backend"), audioBackendCombo_);
    transcriptionLayout->addRow(QStringLiteral("Language"), languageEdit_);
    transcriptionLayout->addRow(QStringLiteral("Paste target"), pasteTargetCombo_);
    transcriptionLayout->addRow(QStringLiteral("Overlay"), miniOverlayCheck_);
    transcriptionLayout->addRow(QStringLiteral("Whisper CLI"), whisperCliEdit_);
    transcriptionLayout->addRow(QStringLiteral("Model path (ggml)"), modelEdit_);
    transcriptionLayout->addRow(QStringLiteral("Realtime model"), realtimeModelCombo_);
    transcriptionLayout->addRow(QStringLiteral("Realtime Python"), realtimePythonEdit_);
    transcriptionLayout->addRow(QStringLiteral("Realtime server script"), realtimeServerScriptEdit_);
    transcriptionLayout->addRow(QStringLiteral("BuzzASR Python"), buzzAsrPythonEdit_);
    transcriptionLayout->addRow(QStringLiteral("BuzzASR script"), buzzAsrScriptEdit_);
    transcriptionLayout->addRow(QStringLiteral("Parakeet Python"), parakeetPythonEdit_);
    transcriptionLayout->addRow(QStringLiteral("Parakeet script"), parakeetScriptEdit_);
    transcriptionLayout->addRow(QStringLiteral("Dictionary (Whisper / Realtime)"), dictionaryRow);
    transcriptionLayout->addRow(QStringLiteral("Threads"), threadsSpin_);
    transcriptionSection->setLayout(transcriptionLayout);
    sectionsLayout->addWidget(transcriptionSection, 0, 1);
    updateEngineFields();

    auto *soundSection = new QGroupBox(QStringLiteral("Sounds"), content);
    auto *soundLayout = createFormLayout(soundSection);
    activationSoundEdit_ = new QLineEdit(settings.activationSound, soundSection);
    endSoundEdit_ = new QLineEdit(settings.endSound, soundSection);
    soundLayout->addRow(QStringLiteral("Activation sound"), activationSoundEdit_);
    soundLayout->addRow(QStringLiteral("End sound"), endSoundEdit_);
    soundSection->setLayout(soundLayout);
    sectionsLayout->addWidget(soundSection, 1, 0);

    wakeWordGroup_ = new QGroupBox(QStringLiteral("Wake Word"), content);
    wakeWordGroup_->setCheckable(true);
    wakeWordGroup_->setChecked(settings.wakeWordEnabled);
    auto *wakeWordLayout = createFormLayout(wakeWordGroup_);
    wakeWordPhraseEdit_ = new QLineEdit(settings.wakeWordPhrase, wakeWordGroup_);
    wakeWordModelPathEdit_ = new QLineEdit(settings.wakeWordModelPath, wakeWordGroup_);
    wakeWordThresholdSpin_ = new QDoubleSpinBox(wakeWordGroup_);
    wakeWordThresholdSpin_->setRange(0.01, 1.0);
    wakeWordThresholdSpin_->setDecimals(2);
    wakeWordThresholdSpin_->setSingleStep(0.05);
    wakeWordThresholdSpin_->setValue(settings.wakeWordThreshold);
    wakeWordSidecarExecutableEdit_ = new QLineEdit(settings.wakeWordSidecarExecutable, wakeWordGroup_);
    wakeWordSidecarScriptEdit_ = new QLineEdit(settings.wakeWordSidecarScript, wakeWordGroup_);
    wakeWordLayout->addRow(QStringLiteral("Phrase"), wakeWordPhraseEdit_);
    wakeWordLayout->addRow(QStringLiteral("Model path"), wakeWordModelPathEdit_);
    wakeWordLayout->addRow(QStringLiteral("Threshold"), wakeWordThresholdSpin_);
    wakeWordLayout->addRow(QStringLiteral("Sidecar executable"), wakeWordSidecarExecutableEdit_);
    wakeWordLayout->addRow(QStringLiteral("Sidecar script"), wakeWordSidecarScriptEdit_);
    wakeWordGroup_->setLayout(wakeWordLayout);
    sectionsLayout->addWidget(wakeWordGroup_, 1, 1);

    vadGroup_ = new QGroupBox(QStringLiteral("Voice Activity Auto-Stop"), content);
    vadGroup_->setCheckable(true);
    vadGroup_->setChecked(settings.vadAutostopEnabled);
    auto *vadLayout = createFormLayout(vadGroup_);
    vadAggressivenessSpin_ = new QSpinBox(vadGroup_);
    vadAggressivenessSpin_->setRange(0, 3);
    vadAggressivenessSpin_->setValue(settings.vadAggressiveness);
    vadEndSilenceMsSpin_ = new QSpinBox(vadGroup_);
    vadEndSilenceMsSpin_->setRange(1, 10000);
    vadEndSilenceMsSpin_->setSuffix(QStringLiteral(" ms"));
    vadEndSilenceMsSpin_->setValue(settings.vadEndSilenceMs);
    vadEndSilenceMsSpin_->setToolTip(QStringLiteral("1500 ms leaves room for short pauses. Longer values delay auto-stop. Mouse finish buttons stop immediately."));
    vadMinSpeechMsSpin_ = new QSpinBox(vadGroup_);
    vadMinSpeechMsSpin_->setRange(1, 10000);
    vadMinSpeechMsSpin_->setSuffix(QStringLiteral(" ms"));
    vadMinSpeechMsSpin_->setValue(settings.vadMinSpeechMs);
    vadLayout->addRow(QStringLiteral("Aggressiveness"), vadAggressivenessSpin_);
    vadLayout->addRow(QStringLiteral("End silence"), vadEndSilenceMsSpin_);
    vadLayout->addRow(QStringLiteral("Minimum speech"), vadMinSpeechMsSpin_);
    vadGroup_->setLayout(vadLayout);
    sectionsLayout->addWidget(vadGroup_, 2, 0, 1, 2);

    outputGroup_ = new QGroupBox(QStringLiteral("Output"), content);
    auto *outputLayout = createFormLayout(outputGroup_);
    outputTargetCombo_ = new QComboBox(outputGroup_);
    outputTargetCombo_->addItems({
            QStringLiteral("Clipboard"),
            QStringLiteral("Handoff File"),
            QStringLiteral("Auto"),
    });
    if (settings.outputTarget == QStringLiteral("handoff_file")) {
        outputTargetCombo_->setCurrentIndex(1);
    } else if (settings.outputTarget == QStringLiteral("auto")) {
        outputTargetCombo_->setCurrentIndex(2);
    }
    handoffDirectoryEdit_ = new QLineEdit(settings.handoffDirectory, outputGroup_);
    handoffDirectoryEdit_->setPlaceholderText(QStringLiteral("/tmp/agent"));
    handoffTriggerWordsEdit_ = new QLineEdit(settings.handoffTriggerWords, outputGroup_);
    handoffTriggerWordsEdit_->setPlaceholderText(QStringLiteral("agent, computer"));
    outputLayout->addRow(QStringLiteral("Output target"), outputTargetCombo_);
    outputLayout->addRow(QStringLiteral("Handoff directory"), handoffDirectoryEdit_);
    outputLayout->addRow(QStringLiteral("Handoff trigger words"), handoffTriggerWordsEdit_);
    outputGroup_->setLayout(outputLayout);
    sectionsLayout->addWidget(outputGroup_, 3, 0, 1, 2);

    contentLayout->addStretch();
    scrollArea->setWidget(content);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void PreferencesDialog::updateEngineFields()
{
    const QString engine = engineCombo_->currentData().toString();
    const bool realtime = engine == QString::fromUtf8(AppSettings::kEngineRealtime);
    const bool whisper = engine == QString::fromUtf8(AppSettings::kEngineWhisperCpp);
    const bool buzzAsr = engine == QString::fromUtf8(AppSettings::kEngineBuzzAsr);
    const bool parakeet = engine == QString::fromUtf8(AppSettings::kEngineParakeet);
    whisperCliEdit_->setEnabled(whisper);
    modelEdit_->setEnabled(whisper);
    threadsSpin_->setEnabled(whisper);
    realtimeModelCombo_->setEnabled(realtime);
    realtimePythonEdit_->setEnabled(realtime);
    realtimeServerScriptEdit_->setEnabled(realtime);
    buzzAsrPythonEdit_->setEnabled(buzzAsr);
    buzzAsrScriptEdit_->setEnabled(buzzAsr);
    parakeetPythonEdit_->setEnabled(parakeet);
    parakeetScriptEdit_->setEnabled(parakeet);
    languageEdit_->setEnabled(!parakeet);
    languageEdit_->setToolTip(parakeet ? QStringLiteral("Parakeet detects the language from audio.") : QString());
}

AppSettings PreferencesDialog::editedSettings() const
{
    AppSettings edited = baseSettings_;
    edited.hotkeyDictate = hotkeyDictateEdit_->text().trimmed();
    edited.hotkeyTranslateEn = hotkeyTranslateEnEdit_->text().trimmed();
    edited.pushToTalkButton = pushToTalkButtonSpin_->value();
    edited.pushToTalkSendEnter = pushToTalkEnterCheck_->isChecked();
    edited.engine = engineCombo_->currentData().toString();
    edited.audioBackend = audioBackendCombo_->currentText().trimmed();
    edited.language = languageEdit_->text().trimmed();
    edited.whisperCli = whisperCliEdit_->text().trimmed();
    edited.model = modelEdit_->text().trimmed();
    edited.realtimeModel = realtimeModelCombo_->currentText().trimmed();
    edited.realtimePython = realtimePythonEdit_->text().trimmed();
    edited.realtimeServerScript = realtimeServerScriptEdit_->text().trimmed();
    edited.buzzAsrPython = buzzAsrPythonEdit_->text().trimmed();
    edited.buzzAsrScript = buzzAsrScriptEdit_->text().trimmed();
    edited.parakeetPython = parakeetPythonEdit_->text().trimmed();
    edited.parakeetScript = parakeetScriptEdit_->text().trimmed();
    edited.dictionaryPath = dictionaryPathEdit_->text().trimmed();
    edited.pasteTarget = pasteTargetCombo_->currentData().toString();
    edited.miniOverlayEnabled = miniOverlayCheck_->isChecked();
    edited.activationSound = activationSoundEdit_->text().trimmed();
    edited.endSound = endSoundEdit_->text().trimmed();
    edited.wakeWordEnabled = wakeWordGroup_->isChecked();
    edited.wakeWordPhrase = wakeWordPhraseEdit_->text().trimmed();
    edited.wakeWordModelPath = wakeWordModelPathEdit_->text().trimmed();
    edited.wakeWordThreshold = wakeWordThresholdSpin_->value();
    edited.wakeWordSidecarExecutable = wakeWordSidecarExecutableEdit_->text().trimmed();
    edited.wakeWordSidecarScript = wakeWordSidecarScriptEdit_->text().trimmed();
    edited.vadAutostopEnabled = vadGroup_->isChecked();
    edited.vadAggressiveness = vadAggressivenessSpin_->value();
    edited.vadEndSilenceMs = vadEndSilenceMsSpin_->value();
    edited.vadMinSpeechMs = vadMinSpeechMsSpin_->value();
    edited.threads = threadsSpin_->value();
    edited.outputTarget = [&]() -> QString {
        switch (outputTargetCombo_->currentIndex()) {
        case 1: return QStringLiteral("handoff_file");
        case 2: return QStringLiteral("auto");
        default: return QStringLiteral("clipboard");
        }
    }();
    edited.handoffDirectory = handoffDirectoryEdit_->text().trimmed();
    edited.handoffTriggerWords = handoffTriggerWordsEdit_->text().trimmed();
    return edited;
}
