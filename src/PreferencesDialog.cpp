#include "PreferencesDialog.h"

#include <QComboBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QProcessEnvironment>
#include <QHBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
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

bool validOllamaUrl(const QString &value)
{
    const QUrl url(value.trimmed(), QUrl::StrictMode);
    return url.isValid() && (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"))
            && !url.host().isEmpty() && !url.authority(QUrl::FullyEncoded).contains(QLatin1Char('@'))
            && (url.path().isEmpty() || url.path() == QStringLiteral("/"))
            && !url.hasQuery() && !url.hasFragment();
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

    auto *meetingsSection = new QGroupBox(QStringLiteral("Meeting Recordings"), content);
    auto *meetingsLayout = createFormLayout(meetingsSection);
    auto *directoryRow = new QWidget(meetingsSection);
    auto *directoryLayout = new QHBoxLayout(directoryRow);
    directoryLayout->setContentsMargins(0, 0, 0, 0);
    meetingsDirectoryEdit_ = new QLineEdit(settings.meetingsDirectory, directoryRow);
    auto *browse = new QPushButton(QStringLiteral("Browse…"), directoryRow);
    directoryLayout->addWidget(meetingsDirectoryEdit_);
    directoryLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Meeting recordings directory"),
                AppSettings::expandUserPath(meetingsDirectoryEdit_->text()));
        if (!directory.isEmpty()) meetingsDirectoryEdit_->setText(directory);
    });
    meetingsLayout->addRow(QStringLiteral("Recordings directory"), directoryRow);
    sectionsLayout->addWidget(meetingsSection, 4, 0, 1, 2);

    auto *speakerSection = new QGroupBox(QStringLiteral("Meeting speakers — Hugging Face"), content);
    auto *speakerLayout = createFormLayout(speakerSection);
    auto *description = new QLabel(QStringLiteral("Install local speaker identification for meeting recordings. Accept the <a href=\"https://huggingface.co/pyannote/speaker-diarization-community-1\">model conditions</a> and create a <a href=\"https://huggingface.co/settings/tokens\">read token</a> first. Setup downloads Python packages and the model; it can use several GB."), speakerSection);
    description->setWordWrap(true);
    description->setOpenExternalLinks(true);
    speakerLayout->addRow(description);
    huggingFaceTokenEdit_ = new QLineEdit(settings.huggingFaceToken, speakerSection);
    huggingFaceTokenEdit_->setObjectName(QStringLiteral("huggingFaceToken"));
    huggingFaceTokenEdit_->setEchoMode(QLineEdit::Password);
    huggingFaceTokenEdit_->setPlaceholderText(QStringLiteral("Read token (or use HF_TOKEN environment variable)"));
    huggingFaceHomeEdit_ = new QLineEdit(settings.huggingFaceHome, speakerSection);
    huggingFaceHomeEdit_->setObjectName(QStringLiteral("huggingFaceHome"));
    diarizationRuntimeEdit_ = new QLineEdit(settings.diarizationRuntimeDirectory, speakerSection);
    diarizationRuntimeEdit_->setObjectName(QStringLiteral("diarizationRuntimeDirectory"));
    speakerLayout->addRow(QStringLiteral("Hugging Face token"), huggingFaceTokenEdit_);
    speakerLayout->addRow(QStringLiteral("Model cache directory"), huggingFaceHomeEdit_);
    speakerLayout->addRow(QStringLiteral("Python environment"), diarizationRuntimeEdit_);
    auto *tokenNote = new QLabel(QStringLiteral("The token is saved unencrypted in a separate owner-only file when you press OK. Setup uses the values shown here."), speakerSection);
    tokenNote->setWordWrap(true);
    speakerLayout->addRow(tokenNote);
    auto *setupButtons = new QHBoxLayout;
    diarizationSetup_ = new QPushButton(QStringLiteral("Install / check integration"), speakerSection);
    diarizationSetup_->setObjectName(QStringLiteral("diarizationSetup"));
    diarizationStop_ = new QPushButton(QStringLiteral("Stop setup"), speakerSection);
    diarizationStop_->setObjectName(QStringLiteral("diarizationStop"));
    diarizationStop_->setEnabled(false);
    setupButtons->addWidget(diarizationSetup_); setupButtons->addWidget(diarizationStop_);
    speakerLayout->addRow(setupButtons);
    diarizationStatus_ = new QLabel(QStringLiteral("Not checked."), speakerSection);
    diarizationStatus_->setObjectName(QStringLiteral("diarizationSetupStatus"));
    diarizationStatus_->setWordWrap(true);
    speakerLayout->addRow(diarizationStatus_);
    sectionsLayout->addWidget(speakerSection, 5, 0, 1, 2);

    auto *analysisSection = new QGroupBox(QStringLiteral("Meeting analysis"), content);
    auto *analysisLayout = createFormLayout(analysisSection);
    auto *analysisInfo = new QLabel(QStringLiteral("Analysis sends the transcript to one of the configured Ollama servers, or uses Codex CLI/OpenCode CLI installed on this computer."), analysisSection);
    analysisInfo->setWordWrap(true); analysisLayout->addRow(analysisInfo);
    analysisOllamaServers_ = new QListWidget(analysisSection);
    analysisOllamaServers_->setObjectName(QStringLiteral("analysisOllamaServers"));
    analysisOllamaServers_->setSelectionMode(QAbstractItemView::SingleSelection);
    analysisOllamaServers_->setMinimumHeight(76);
    for (const QString &url : settings.analysisOllamaUrls) {
        if (!url.trimmed().isEmpty()) analysisOllamaServers_->addItem(url.trimmed());
    }
    if (analysisOllamaServers_->count() == 0) analysisOllamaServers_->addItem(QStringLiteral("http://127.0.0.1:11434"));
    analysisOllamaServers_->setCurrentRow(0);
    analysisLayout->addRow(QStringLiteral("Ollama servers"), analysisOllamaServers_);

    auto *analysisAddressRow = new QWidget(analysisSection);
    auto *analysisAddressLayout = new QHBoxLayout(analysisAddressRow);
    analysisAddressLayout->setContentsMargins(0, 0, 0, 0);
    analysisOllamaUrlEdit_ = new QLineEdit(analysisOllamaServers_->currentItem()->text(), analysisAddressRow);
    analysisOllamaUrlEdit_->setObjectName(QStringLiteral("analysisOllamaUrl"));
    analysisOllamaUrlEdit_->setPlaceholderText(QStringLiteral("http://host:11434"));
    auto *addOllama = new QPushButton(QStringLiteral("Add"), analysisAddressRow);
    addOllama->setObjectName(QStringLiteral("analysisAddOllama"));
    auto *removeOllama = new QPushButton(QStringLiteral("Remove"), analysisAddressRow);
    removeOllama->setObjectName(QStringLiteral("analysisRemoveOllama"));
    analysisAddressLayout->addWidget(analysisOllamaUrlEdit_, 1);
    analysisAddressLayout->addWidget(addOllama);
    analysisAddressLayout->addWidget(removeOllama);
    analysisLayout->addRow(QStringLiteral("Address"), analysisAddressRow);
    auto *checkOllama = new QPushButton(QStringLiteral("Check Ollama connection"), analysisSection);
    checkOllama->setObjectName(QStringLiteral("analysisCheckOllama"));
    analysisCheckStatus_ = new QLabel(QStringLiteral("Not checked."), analysisSection);
    analysisCheckStatus_->setObjectName(QStringLiteral("analysisOllamaStatus"));
    analysisCheckStatus_->setWordWrap(true);
    analysisLayout->addRow(checkOllama, analysisCheckStatus_);
    sectionsLayout->addWidget(analysisSection, 6, 0, 1, 2);
    auto updateOllamaButtons = [this, removeOllama]() {
        removeOllama->setEnabled(analysisOllamaServers_->count() > 1 && analysisOllamaServers_->currentRow() >= 0);
    };
    connect(analysisOllamaServers_, &QListWidget::currentRowChanged, this, [this, updateOllamaButtons](int row) {
        if (row >= 0 && analysisOllamaServers_->item(row)) analysisOllamaUrlEdit_->setText(analysisOllamaServers_->item(row)->text());
        updateOllamaButtons();
    });
    connect(addOllama, &QPushButton::clicked, this, [this, updateOllamaButtons]() {
        const QString url = analysisOllamaUrlEdit_->text().trimmed();
        if (!validOllamaUrl(url)) {
            analysisCheckStatus_->setText(QStringLiteral("Enter an HTTP or HTTPS Ollama server address without a path or credentials."));
            return;
        }
        const QList<QListWidgetItem *> matches = analysisOllamaServers_->findItems(url, Qt::MatchExactly);
        if (!matches.isEmpty()) {
            analysisOllamaServers_->setCurrentItem(matches.first());
        } else {
            analysisOllamaServers_->addItem(url);
            analysisOllamaServers_->setCurrentRow(analysisOllamaServers_->count() - 1);
        }
        updateOllamaButtons();
    });
    connect(removeOllama, &QPushButton::clicked, this, [this, updateOllamaButtons]() {
        const int row = analysisOllamaServers_->currentRow();
        if (row < 0 || analysisOllamaServers_->count() <= 1) return;
        delete analysisOllamaServers_->takeItem(row);
        analysisOllamaServers_->setCurrentRow(qMin(row, analysisOllamaServers_->count() - 1));
        updateOllamaButtons();
    });
    updateOllamaButtons();
    connect(checkOllama, &QPushButton::clicked, this, [this, checkOllama]() {
        if (ollamaCheckProcess_.state() != QProcess::NotRunning) return;
        const QUrl url(analysisOllamaUrlEdit_->text().trimmed(), QUrl::StrictMode);
        if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))
                || url.host().isEmpty() || url.authority(QUrl::FullyEncoded).contains(QLatin1Char('@'))
                || (!url.path().isEmpty() && url.path() != QStringLiteral("/")) || url.hasQuery() || url.hasFragment()) {
            analysisCheckStatus_->setText(QStringLiteral("Enter an HTTP or HTTPS Ollama server address without a path or credentials.")); return;
        }
        const QString script = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("meeting_analysis.py"));
        if (!QFileInfo::exists(script)) { analysisCheckStatus_->setText(QStringLiteral("Analysis helper is missing.")); return; }
        analysisCheckStatus_->setText(QStringLiteral("Checking…")); checkOllama->setEnabled(false);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("OPENAI_API_KEY"));
        ollamaCheckProcess_.setProcessEnvironment(env);
        ollamaCheckProcess_.start(QStringLiteral("python3"), {script, QStringLiteral("--list-models"), QStringLiteral("--backend"), QStringLiteral("ollama"), QStringLiteral("--endpoint"), url.toString()});
    });
    connect(&ollamaCheckProcess_, &QProcess::finished, this, [this, checkOllama](int code, QProcess::ExitStatus status) {
        checkOllama->setEnabled(true);
        if (code == 0 && status == QProcess::NormalExit) {
            const QJsonDocument doc = QJsonDocument::fromJson(ollamaCheckProcess_.readAllStandardOutput());
            const QJsonValue models = doc.object().value(QStringLiteral("models"));
            if (!models.isArray()) { analysisCheckStatus_->setText(QStringLiteral("Ollama returned an invalid model list.")); return; }
            QStringList names;
            for (const QJsonValue &value : models.toArray()) if (value.isString()) names << value.toString();
            analysisCheckStatus_->setText(names.isEmpty() ? QStringLiteral("Connected. No supported server models found.")
                    : QStringLiteral("Connected. Models: %1").arg(names.join(QStringLiteral(", "))));
        } else analysisCheckStatus_->setText(QStringLiteral("Could not connect to %1. Check that it is running and the address is correct.").arg(analysisOllamaUrlEdit_->text().trimmed()));
    });
    connect(&ollamaCheckProcess_, &QProcess::errorOccurred, this, [this, checkOllama](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { checkOllama->setEnabled(true); analysisCheckStatus_->setText(QStringLiteral("Python 3 was not found.")); }
    });

    connect(diarizationSetup_, &QPushButton::clicked, this, &PreferencesDialog::startDiarizationSetup);
    connect(diarizationStop_, &QPushButton::clicked, this, [this]() {
        setupStopping_ = true;
        diarizationStatus_->setText(QStringLiteral("Stopping setup…"));
        setupProcess_.terminate();
        QTimer::singleShot(3000, this, [this]() { if (setupStopping_ && setupProcess_.state() != QProcess::NotRunning) setupProcess_.kill(); });
    });
    connect(&setupProcess_, &QProcess::readyReadStandardOutput, this, [this]() {
        setupOutput_ += setupProcess_.readAllStandardOutput();
        if (setupOutput_.size() > 16000) setupOutput_.remove(0, setupOutput_.size() - 16000);
    });
    connect(&setupProcess_, &QProcess::readyReadStandardError, this, [this]() {
        setupOutput_ += setupProcess_.readAllStandardError();
        if (setupOutput_.size() > 16000) setupOutput_.remove(0, setupOutput_.size() - 16000);
    });
    connect(&setupProcess_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        setupOutput_ += setupProcess_.readAllStandardError();
        if (setupStopping_) { finishDiarizationSetup(QStringLiteral("Setup stopped. You can retry.")); return; }
        if (code != 0 || status != QProcess::NormalExit) {
            QString details = QString::fromUtf8(setupOutput_).right(1500).trimmed();
            const QString token = huggingFaceTokenEdit_->text().trimmed();
            if (!token.isEmpty()) details.replace(token, QStringLiteral("[token hidden]"));
            const QString envToken = qEnvironmentVariable("HF_TOKEN");
            if (!envToken.isEmpty()) details.replace(envToken, QStringLiteral("[token hidden]"));
            finishDiarizationSetup(QStringLiteral("Setup failed. %1").arg(details)); return;
        }
        ++setupStage_;
        nextDiarizationSetupStep();
    });
    connect(&setupProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finishDiarizationSetup(QStringLiteral("Could not start setup: %1").arg(setupProcess_.errorString()));
    });

    contentLayout->addStretch();
    scrollArea->setWidget(content);

    buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox_, &QDialogButtonBox::accepted, this, [this]() {
        if (diarizationRuntimeEdit_->text().trimmed().isEmpty() || huggingFaceHomeEdit_->text().trimmed().isEmpty()) {
            diarizationStatus_->setText(QStringLiteral("Enter both the Python environment and model cache directories."));
            return;
        }
        accept();
    });
    connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox_);
}

void PreferencesDialog::closeEvent(QCloseEvent *event)
{
    if (setupStage_ != 0) { event->ignore(); return; }
    QDialog::closeEvent(event);
}

void PreferencesDialog::reject()
{
    if (setupStage_ == 0) QDialog::reject();
}

void PreferencesDialog::startDiarizationSetup()
{
    if (setupStage_ != 0) return;
    if (diarizationRuntimeEdit_->text().trimmed().isEmpty() || huggingFaceHomeEdit_->text().trimmed().isEmpty()) {
        diarizationStatus_->setText(QStringLiteral("Enter both the Python environment and model cache directories.")); return;
    }
    if (huggingFaceTokenEdit_->text().trimmed().isEmpty() && qEnvironmentVariableIsEmpty("HF_TOKEN")) {
        diarizationStatus_->setText(QStringLiteral("Enter a Hugging Face read token or set HF_TOKEN before starting the app.")); return;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty()) {
        diarizationStatus_->setText(QStringLiteral("Python 3 was not found in PATH.")); return;
    }
    setupStopping_ = false; setupStage_ = 1;
    diarizationSetup_->setEnabled(false); diarizationStop_->setEnabled(true); buttonBox_->setEnabled(false);
    huggingFaceTokenEdit_->setEnabled(false); huggingFaceHomeEdit_->setEnabled(false); diarizationRuntimeEdit_->setEnabled(false);
    nextDiarizationSetupStep();
}

void PreferencesDialog::nextDiarizationSetupStep()
{
    setupOutput_.clear();
    const QString runtime = AppSettings::expandUserPath(diarizationRuntimeEdit_->text().trimmed());
    const QString python = QDir(runtime).filePath(QStringLiteral("bin/python"));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QString program = python;
    QStringList arguments;
    if (setupStage_ == 1) {
        diarizationStatus_->setText(QStringLiteral("Creating Python environment…"));
        program = QStringLiteral("python3"); arguments = {QStringLiteral("-m"), QStringLiteral("venv"), runtime};
    } else if (setupStage_ == 2) {
        diarizationStatus_->setText(QStringLiteral("Installing CPU PyTorch (this may take several minutes)…"));
        arguments = {QStringLiteral("-m"), QStringLiteral("pip"), QStringLiteral("install"), QStringLiteral("torch"), QStringLiteral("torchaudio"), QStringLiteral("--index-url"), QStringLiteral("https://download.pytorch.org/whl/cpu")};
    } else if (setupStage_ == 3) {
        diarizationStatus_->setText(QStringLiteral("Installing speaker identification dependencies…"));
        arguments = {QStringLiteral("-m"), QStringLiteral("pip"), QStringLiteral("install"), QStringLiteral("pyannote.audio")};
    } else if (setupStage_ == 4) {
        diarizationStatus_->setText(QStringLiteral("Downloading and checking the speaker model…"));
        environment.insert(QStringLiteral("HF_HOME"), AppSettings::expandUserPath(huggingFaceHomeEdit_->text().trimmed()));
        environment.remove(QStringLiteral("HF_HUB_CACHE"));
        environment.remove(QStringLiteral("HUGGINGFACE_HUB_CACHE"));
        if (!huggingFaceTokenEdit_->text().trimmed().isEmpty()) environment.insert(QStringLiteral("HF_TOKEN"), huggingFaceTokenEdit_->text().trimmed());
        arguments = {QStringLiteral("-c"), QStringLiteral(
                "import os, torch, warnings\n"
                "warnings.filterwarnings('ignore', message=r'(?s).*torchcodec is not installed correctly.*', category=UserWarning, module=r'pyannote\\.audio\\.core\\.io')\n"
                "from pyannote.audio import Pipeline\n"
                "p = Pipeline.from_pretrained('pyannote/speaker-diarization-community-1', token=os.environ.get('HF_TOKEN'))\n"
                "assert p is not None, 'Model unavailable; accept its conditions and check the token'\n"
                "p({'waveform': torch.zeros((1, 16000 * 3)), 'sample_rate': 16000})\n")};
    } else {
        finishDiarizationSetup(QStringLiteral("Integration ready. Press OK to save these settings.")); return;
    }
    if (setupStage_ != 4) environment.remove(QStringLiteral("HF_TOKEN"));
    setupProcess_.setProcessEnvironment(environment);
    setupProcess_.start(program, arguments);
}

void PreferencesDialog::finishDiarizationSetup(const QString &message)
{
    setupStage_ = 0;
    diarizationSetup_->setEnabled(true); diarizationStop_->setEnabled(false); buttonBox_->setEnabled(true);
    huggingFaceTokenEdit_->setEnabled(true); huggingFaceHomeEdit_->setEnabled(true); diarizationRuntimeEdit_->setEnabled(true);
    diarizationStatus_->setText(message);
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
    edited.meetingsDirectory = meetingsDirectoryEdit_->text().trimmed();
    edited.huggingFaceToken = huggingFaceTokenEdit_->text().trimmed();
    edited.huggingFaceHome = huggingFaceHomeEdit_->text().trimmed();
    edited.diarizationRuntimeDirectory = diarizationRuntimeEdit_->text().trimmed();
    edited.analysisOllamaUrls.clear();
    for (int row = 0; row < analysisOllamaServers_->count(); ++row)
        edited.analysisOllamaUrls.append(analysisOllamaServers_->item(row)->text().trimmed());
    const int selectedRow = analysisOllamaServers_->currentRow();
    const QString editedUrl = analysisOllamaUrlEdit_->text().trimmed();
    if (selectedRow >= 0 && selectedRow < edited.analysisOllamaUrls.size() && validOllamaUrl(editedUrl))
        edited.analysisOllamaUrls[selectedRow] = editedUrl;
    if (!baseSettings_.settingsPath.isEmpty()) {
        QSettings stored(baseSettings_.settingsPath, QSettings::IniFormat);
        edited.analysisOllamaModel = stored.value(QStringLiteral("analysis_ollama_model"), edited.analysisOllamaModel).toString();
    }
    return edited;
}
