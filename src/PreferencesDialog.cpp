#include "PreferencesDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
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
    hotkeysSection->setLayout(hotkeysLayout);
    sectionsLayout->addWidget(hotkeysSection, 0, 0);

    auto *transcriptionSection = new QGroupBox(QStringLiteral("Transcription"), content);
    auto *transcriptionLayout = createFormLayout(transcriptionSection);
    audioBackendCombo_ = new QComboBox(transcriptionSection);
    audioBackendCombo_->addItems({QStringLiteral("auto"), QStringLiteral("pipewire"), QStringLiteral("pulseaudio"), QStringLiteral("alsa")});
    setComboValue(audioBackendCombo_, settings.audioBackend);
    languageEdit_ = new QLineEdit(settings.language, transcriptionSection);
    languageEdit_->setPlaceholderText(QStringLiteral("pl, en, auto"));
    whisperCliEdit_ = new QLineEdit(settings.whisperCli, transcriptionSection);
    modelEdit_ = new QLineEdit(settings.model, transcriptionSection);
    threadsSpin_ = new QSpinBox(transcriptionSection);
    threadsSpin_->setRange(1, 256);
    threadsSpin_->setValue(settings.threads);
    transcriptionLayout->addRow(QStringLiteral("Audio backend"), audioBackendCombo_);
    transcriptionLayout->addRow(QStringLiteral("Language"), languageEdit_);
    transcriptionLayout->addRow(QStringLiteral("Whisper CLI"), whisperCliEdit_);
    transcriptionLayout->addRow(QStringLiteral("Model path"), modelEdit_);
    transcriptionLayout->addRow(QStringLiteral("Threads"), threadsSpin_);
    transcriptionSection->setLayout(transcriptionLayout);
    sectionsLayout->addWidget(transcriptionSection, 0, 1);

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

AppSettings PreferencesDialog::editedSettings() const
{
    AppSettings edited = baseSettings_;
    edited.hotkeyDictate = hotkeyDictateEdit_->text().trimmed();
    edited.hotkeyTranslateEn = hotkeyTranslateEnEdit_->text().trimmed();
    edited.audioBackend = audioBackendCombo_->currentText().trimmed();
    edited.language = languageEdit_->text().trimmed();
    edited.whisperCli = whisperCliEdit_->text().trimmed();
    edited.model = modelEdit_->text().trimmed();
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
