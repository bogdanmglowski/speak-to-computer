#include "MeetingAnalysisDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

namespace {
QString elapsedText(qint64 seconds)
{
    return QStringLiteral("%1:%2:%3").arg(seconds / 3600, 2, 10, QLatin1Char('0'))
            .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QJsonObject metadata(const QByteArray &data)
{
    const QByteArray prefix("<!-- STC_ANALYSIS ");
    if (!data.startsWith(prefix)) return {};
    const qsizetype end = data.indexOf(" -->\n");
    if (end < 0) return {};
    return QJsonDocument::fromJson(data.mid(prefix.size(), end - prefix.size())).object();
}
}

MeetingAnalysisDialog::MeetingAnalysisDialog(const MeetingEntry &meeting, const AppSettings &settings, QWidget *parent)
    : QDialog(parent), meeting_(meeting), settings_(settings)
{
    setWindowTitle(QStringLiteral("Meeting analysis"));
    resize(960, 680);
    auto *layout = new QVBoxLayout(this);
    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(QStringLiteral("Analysis mode:"), this));
    backendCombo_ = new QComboBox(this);
    backendCombo_->setObjectName(QStringLiteral("analysisBackend"));
    backendCombo_->addItem(QStringLiteral("Ollama karaczan"), QStringLiteral("ollama"));
    backendCombo_->addItem(QStringLiteral("Ollama local"), QStringLiteral("ollama_local"));
    backendCombo_->addItem(QStringLiteral("OpenCode Go"), QStringLiteral("opencode"));
    backendCombo_->addItem(QStringLiteral("Codex CLI"), QStringLiteral("codex"));
    controls->addWidget(backendCombo_);
    controls->addWidget(new QLabel(QStringLiteral("Model:"), this));
    modelCombo_ = new QComboBox(this);
    modelCombo_->setObjectName(QStringLiteral("analysisModel"));
    modelCombo_->setEditable(true);
    modelCombo_->setInsertPolicy(QComboBox::NoInsert);
    modelCombo_->setMinimumWidth(190);
    modelCombo_->setToolTip(QStringLiteral("Select an available model or enter its ID."));
    controls->addWidget(modelCombo_, 1);
    refreshModels_ = new QPushButton(QStringLiteral("Refresh models"), this);
    refreshModels_->setObjectName(QStringLiteral("analysisRefreshModels"));
    controls->addWidget(refreshModels_);
    analyze_ = new QPushButton(QStringLiteral("Analyze"), this);
    analyze_->setObjectName(QStringLiteral("analysisStart"));
    cancel_ = new QPushButton(QStringLiteral("Cancel analysis"), this);
    cancel_->setObjectName(QStringLiteral("analysisCancel")); cancel_->setEnabled(false);
    controls->addWidget(analyze_); controls->addWidget(cancel_);
    layout->addLayout(controls);
    reasoningRow_ = new QWidget(this);
    auto *reasoningLayout = new QHBoxLayout(reasoningRow_);
    reasoningLayout->setContentsMargins(0, 0, 0, 0);
    reasoningLayout->addWidget(new QLabel(QStringLiteral("Reasoning effort:"), reasoningRow_));
    reasoningCombo_ = new QComboBox(reasoningRow_);
    reasoningCombo_->setObjectName(QStringLiteral("analysisReasoningEffort"));
    reasoningCombo_->addItem(QStringLiteral("Default"), QString());
    for (const QString &effort : {QStringLiteral("none"), QStringLiteral("minimal"), QStringLiteral("low"),
                                 QStringLiteral("medium"), QStringLiteral("high"), QStringLiteral("xhigh"), QStringLiteral("max")})
        reasoningCombo_->addItem(effort, effort);
    reasoningLayout->addWidget(reasoningCombo_);
    auto *reasoningInfo = new QLabel(QStringLiteral("Supported levels depend on the model. Default uses Codex's built-in setting. Higher levels can take longer and use more of your allowance."), reasoningRow_);
    reasoningInfo->setWordWrap(true); reasoningLayout->addWidget(reasoningInfo, 1);
    layout->addWidget(reasoningRow_);
    providerInfo_ = new QLabel(this); providerInfo_->setWordWrap(true); layout->addWidget(providerInfo_);
    modelStatus_ = new QLabel(this); modelStatus_->setObjectName(QStringLiteral("analysisModelsStatus"));
    modelStatus_->setWordWrap(true); layout->addWidget(modelStatus_);
    localModel_ = settings_.analysisOllamaModel;
    codexModel_ = QStringLiteral("Default (Codex)");
    if (!settings_.settingsPath.isEmpty()) {
        QSettings stored(settings_.settingsPath, QSettings::IniFormat);
        localModel_ = stored.value(QStringLiteral("analysis_ollama_model"), localModel_).toString();
        localOllamaModel_ = stored.value(QStringLiteral("analysis_local_ollama_model")).toString();
        opencodeModel_ = stored.value(QStringLiteral("analysis_opencode_model")).toString();
        const QString savedModel = stored.value(QStringLiteral("analysis_codex_model")).toString();
        if (!savedModel.isEmpty()) codexModel_ = savedModel;
        const int effortIndex = reasoningCombo_->findData(stored.value(QStringLiteral("analysis_codex_reasoning_effort")).toString());
        if (effortIndex >= 0) reasoningCombo_->setCurrentIndex(effortIndex);
    }
    connect(backendCombo_, &QComboBox::currentIndexChanged, this, &MeetingAnalysisDialog::switchProvider);
    connect(refreshModels_, &QPushButton::clicked, this, &MeetingAnalysisDialog::refreshModels);
    connect(modelCombo_->lineEdit(), &QLineEdit::textEdited, this, [this]() { modelManuallyEdited_ = true; });
    connect(modelCombo_, &QComboBox::activated, this, [this]() { modelManuallyEdited_ = true; });
    auto *splitter = new QSplitter(this);
    versions_ = new QListWidget(splitter); versions_->setObjectName(QStringLiteral("analysisVersions"));
    preview_ = new QPlainTextEdit(splitter); preview_->setObjectName(QStringLiteral("analysisPreview")); preview_->setReadOnly(true);
    splitter->addWidget(versions_); splitter->addWidget(preview_); splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter, 1);
    versionInfo_ = new QLabel(this); versionInfo_->setWordWrap(true); versionInfo_->setObjectName(QStringLiteral("analysisVersionInfo")); layout->addWidget(versionInfo_);
    status_ = new QLabel(this); status_->setObjectName(QStringLiteral("analysisStatus")); status_->setWordWrap(true); layout->addWidget(status_);
    auto *actions = new QHBoxLayout;
    open_ = new QPushButton(QStringLiteral("Open Markdown"), this);
    copy_ = new QPushButton(QStringLiteral("Copy analysis"), this);
    auto *close = new QPushButton(QStringLiteral("Close"), this);
    actions->addWidget(open_); actions->addWidget(copy_); actions->addStretch(); actions->addWidget(close); layout->addLayout(actions);
    connect(versions_, &QListWidget::currentRowChanged, this, [this]() { showVersion(); });
    connect(analyze_, &QPushButton::clicked, this, &MeetingAnalysisDialog::startAnalysis);
    connect(cancel_, &QPushButton::clicked, this, [this]() {
        if (process_.state() == QProcess::NotRunning) return;
        canceled_ = true; stage_ = QStringLiteral("Canceling analysis"); updateStatus(); process_.terminate();
        QTimer::singleShot(3000, this, [this]() { if (canceled_ && process_.state() != QProcess::NotRunning) process_.kill(); });
    });
    connect(close, &QPushButton::clicked, this, &MeetingAnalysisDialog::reject);
    connect(open_, &QPushButton::clicked, this, [this]() {
        if (versions_->currentItem()) QDesktopServices::openUrl(QUrl::fromLocalFile(versions_->currentItem()->data(Qt::UserRole).toString()));
    });
    connect(copy_, &QPushButton::clicked, this, [this]() { QApplication::clipboard()->setText(preview_->toPlainText()); });
    connect(&process_, &QProcess::readyReadStandardError, this, [this]() { readErrors(); });
    connect(&process_, &QProcess::finished, this, &MeetingAnalysisDialog::finishAnalysis);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finishAnalysis(-1, QProcess::CrashExit);
    });
    connect(&modelsProcess_, &QProcess::finished, this, &MeetingAnalysisDialog::finishModelList);
    connect(&modelsProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finishModelList(-1, QProcess::CrashExit);
    });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() { if (process_.state() != QProcess::NotRunning) updateStatus(); }); timer->start(1000);
    reloadVersions();
    switchProvider();
}

MeetingAnalysisDialog::~MeetingAnalysisDialog()
{
    destroying_ = true;
    if (modelsProcess_.state() != QProcess::NotRunning) {
        modelsProcess_.terminate();
        if (!modelsProcess_.waitForFinished(2000)) { modelsProcess_.kill(); modelsProcess_.waitForFinished(1000); }
    }
    if (process_.state() != QProcess::NotRunning) {
        process_.terminate();
        if (!process_.waitForFinished(3000)) { process_.kill(); process_.waitForFinished(1000); }
    }
}

void MeetingAnalysisDialog::reject()
{
    if (modelsProcess_.state() != QProcess::NotRunning) {
        ignoringModels_ = true; modelsProcess_.terminate();
        if (!modelsProcess_.waitForFinished(2000)) { modelsProcess_.kill(); modelsProcess_.waitForFinished(1000); }
        ignoringModels_ = false;
    }
    if (process_.state() != QProcess::NotRunning) {
        canceled_ = true; process_.terminate();
        if (!process_.waitForFinished(3000)) { process_.kill(); process_.waitForFinished(1000); }
    }
    QDialog::reject();
}

void MeetingAnalysisDialog::switchProvider()
{
    if (!activeBackend_.isEmpty()) {
        if (activeBackend_ == QStringLiteral("codex")) codexModel_ = modelCombo_->currentText().trimmed();
        else if (activeBackend_ == QStringLiteral("ollama_local")) localOllamaModel_ = modelCombo_->currentText().trimmed();
        else if (activeBackend_ == QStringLiteral("opencode")) opencodeModel_ = modelCombo_->currentText().trimmed();
        else localModel_ = modelCombo_->currentText().trimmed();
    }
    activeBackend_ = backendCombo_->currentData().toString();
    const QString selected = activeBackend_ == QStringLiteral("codex") ? codexModel_ :
            activeBackend_ == QStringLiteral("ollama_local") ? localOllamaModel_ :
            activeBackend_ == QStringLiteral("opencode") ? opencodeModel_ : localModel_;
    {
        const QSignalBlocker blocker(modelCombo_);
        modelCombo_->clear(); modelCombo_->setEditText(selected);
    }
    modelManuallyEdited_ = false;
    if (activeBackend_ == QStringLiteral("codex"))
        providerInfo_->setText(QStringLiteral("Codex CLI sends the transcript to OpenAI and uses your ChatGPT plan allowance. Sign in with ChatGPT using codex login."));
    else if (activeBackend_ == QStringLiteral("opencode"))
        providerInfo_->setText(QStringLiteral("OpenCode CLI sends the transcript to the selected OpenCode Go model using your connected account."));
    else if (activeBackend_ == QStringLiteral("ollama_local"))
        providerInfo_->setText(QStringLiteral("The transcript is sent to Ollama on this computer (127.0.0.1:11434)."));
    else providerInfo_->setText(QStringLiteral("The transcript is sent to the configured Ollama server. Only models installed on that server are listed."));
    reasoningRow_->setVisible(activeBackend_ == QStringLiteral("codex"));
    refreshModels();
}

void MeetingAnalysisDialog::refreshModels()
{
    if (process_.state() != QProcess::NotRunning) return;
    if (modelsProcess_.state() != QProcess::NotRunning) {
        ignoringModels_ = true; modelsProcess_.terminate();
        if (!modelsProcess_.waitForFinished(2000)) { modelsProcess_.kill(); modelsProcess_.waitForFinished(1000); }
        ignoringModels_ = false;
    }
    modelsBackend_ = activeBackend_;
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    const QString script = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("meeting_analysis.py"));
    if (python.isEmpty() || !QFileInfo::exists(script)) {
        modelStatus_->setText(QStringLiteral("Python 3 or the analysis helper is missing. You can enter a model ID manually.")); return;
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("OPENAI_API_KEY"));
    env.remove(QStringLiteral("CODEX_API_KEY"));
    modelsProcess_.setProcessEnvironment(env);
    modelStatus_->setText(QStringLiteral("Loading models…"));
    modelsProcess_.start(python, {script, QStringLiteral("--list-models"), QStringLiteral("--backend"), modelsBackend_,
            QStringLiteral("--endpoint"), settings_.analysisOllamaUrl});
}

void MeetingAnalysisDialog::finishModelList(int exitCode, QProcess::ExitStatus exitStatus)
{
    const QByteArray output = modelsProcess_.readAllStandardOutput();
    const QByteArray errors = modelsProcess_.readAllStandardError();
    if (destroying_ || ignoringModels_ || modelsBackend_ != activeBackend_) return;
    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        QString details = QString::fromUtf8(errors).trimmed().right(1000);
        const QString envKey = qEnvironmentVariable("OPENAI_API_KEY");
        if (!envKey.isEmpty()) details.replace(envKey, QStringLiteral("[key hidden]"));
        const QString codexKey = qEnvironmentVariable("CODEX_API_KEY");
        if (!codexKey.isEmpty()) details.replace(codexKey, QStringLiteral("[key hidden]"));
        modelStatus_->setText(QStringLiteral("Could not load models. %1 You can enter a model ID manually.").arg(details));
        return;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(output, &error);
    const QJsonValue modelsValue = document.object().value(QStringLiteral("models"));
    if (error.error != QJsonParseError::NoError || !modelsValue.isArray()) {
        modelStatus_->setText(QStringLiteral("Invalid model list. You can enter a model ID manually.")); return;
    }
    QStringList names;
    for (const QJsonValue &value : modelsValue.toArray()) {
        if (value.isString() && !value.toString().trimmed().isEmpty() && !names.contains(value.toString())) names.append(value.toString());
    }
    if (modelsBackend_ == QStringLiteral("codex")) names.prepend(QStringLiteral("Default (Codex)"));
    const QString previous = modelCombo_->currentText().trimmed();
    {
        const QSignalBlocker blocker(modelCombo_);
        modelCombo_->clear(); modelCombo_->addItems(names);
        const int index = modelCombo_->findText(previous);
        if (index >= 0) modelCombo_->setCurrentIndex(index);
        else if (modelManuallyEdited_ || names.isEmpty()) modelCombo_->setEditText(previous);
        else modelCombo_->setCurrentIndex(0);
    }
    if (modelsBackend_ == QStringLiteral("codex"))
        modelStatus_->setText(QStringLiteral("Codex models come from its local cache and may be outdated. You can enter a model ID manually."));
    else modelStatus_->setText(names.isEmpty() ? QStringLiteral("No models found. You can enter a model ID manually.")
                                               : QStringLiteral("Available models: %1. You can also enter a model ID manually.").arg(names.size()));
}

QByteArray MeetingAnalysisDialog::transcriptHash() const
{
    QFile file(MeetingStore::transcriptPath(meeting_));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}

void MeetingAnalysisDialog::startAnalysis()
{
    if (process_.state() != QProcess::NotRunning) return;
    if (modelsProcess_.state() != QProcess::NotRunning) {
        ignoringModels_ = true; modelsProcess_.terminate();
        if (!modelsProcess_.waitForFinished(2000)) { modelsProcess_.kill(); modelsProcess_.waitForFinished(1000); }
        ignoringModels_ = false;
        modelStatus_->setText(QStringLiteral("Model list loading stopped; the selected model will be used."));
    }
    QFile source(MeetingStore::transcriptPath(meeting_));
    if (!source.open(QIODevice::ReadOnly)) { status_->setText(QStringLiteral("Cannot read transcript: %1").arg(source.errorString())); return; }
    transcriptSnapshot_ = source.readAll();
    if (transcriptSnapshot_.trimmed().isEmpty()) { status_->setText(QStringLiteral("The transcript is empty.")); return; }
    snapshotHash_ = QCryptographicHash::hash(transcriptSnapshot_, QCryptographicHash::Sha256).toHex();
    backend_ = backendCombo_->currentData().toString();
    model_ = modelCombo_->currentText().trimmed();
    if (backend_ == QStringLiteral("codex") && model_ == QStringLiteral("Default (Codex)")) model_.clear();
    reasoningEffort_ = backend_ == QStringLiteral("codex") ? reasoningCombo_->currentData().toString() : QString();
    if (model_.isEmpty() && backend_ != QStringLiteral("codex")) { status_->setText(QStringLiteral("Select or enter an analysis model.")); return; }
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    const QString script = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("meeting_analysis.py"));
    if (python.isEmpty() || !QFileInfo::exists(script)) { status_->setText(QStringLiteral("Python 3 or the analysis helper is missing.")); return; }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("OPENAI_API_KEY"));
    env.remove(QStringLiteral("CODEX_API_KEY"));
    if (backend_ == QStringLiteral("codex")) codexModel_ = model_.isEmpty() ? QStringLiteral("Default (Codex)") : model_;
    else if (backend_ == QStringLiteral("ollama_local")) localOllamaModel_ = model_;
    else if (backend_ == QStringLiteral("opencode")) opencodeModel_ = model_;
    else localModel_ = model_;
    if (!settings_.settingsPath.isEmpty()) {
        QSettings stored(settings_.settingsPath, QSettings::IniFormat);
        const QString modelKey = backend_ == QStringLiteral("codex") ? QStringLiteral("analysis_codex_model") :
                backend_ == QStringLiteral("ollama_local") ? QStringLiteral("analysis_local_ollama_model") :
                backend_ == QStringLiteral("opencode") ? QStringLiteral("analysis_opencode_model") : QStringLiteral("analysis_ollama_model");
        stored.setValue(modelKey, model_);
        if (backend_ == QStringLiteral("codex")) stored.setValue(QStringLiteral("analysis_codex_reasoning_effort"), reasoningEffort_);
        stored.sync();
    }
    process_.setProcessEnvironment(env);
    errors_.clear(); errorPending_.clear(); canceled_ = false;
    stage_ = QStringLiteral("Starting analysis"); elapsed_.start(); updateStatus();
    analyze_->setEnabled(false); cancel_->setEnabled(true); backendCombo_->setEnabled(false); modelCombo_->setEnabled(false); refreshModels_->setEnabled(false); reasoningCombo_->setEnabled(false);
    emit busyChanged(true);
    QStringList arguments{script, QStringLiteral("--transcript"), QStringLiteral("-"),
            QStringLiteral("--backend"), backend_, QStringLiteral("--model"), model_,
            QStringLiteral("--endpoint"), settings_.analysisOllamaUrl, QStringLiteral("--codex-cli"), settings_.analysisCodexCli};
    if (!reasoningEffort_.isEmpty()) arguments << QStringLiteral("--reasoning-effort") << reasoningEffort_;
    process_.start(python, arguments);
    process_.write(transcriptSnapshot_);
    process_.closeWriteChannel();
}

void MeetingAnalysisDialog::readErrors(bool final)
{
    errorPending_ += process_.readAllStandardError();
    qsizetype end;
    while ((end = errorPending_.indexOf('\n')) >= 0 || (final && !errorPending_.isEmpty())) {
        if (end < 0) end = errorPending_.size();
        const QByteArray line = errorPending_.left(end);
        errorPending_.remove(0, qMin(end + 1, errorPending_.size()));
        if (line.startsWith("ANALYSIS_PROGRESS ")) {
            const QJsonObject event = QJsonDocument::fromJson(line.mid(18)).object();
            const QString stage = event.value(QStringLiteral("stage")).toString();
            if (!stage.isEmpty()) {
                stage_ = stage;
                const int total = event.value(QStringLiteral("total")).toInt();
                if (total > 0) stage_ += QStringLiteral(" %1/%2").arg(event.value(QStringLiteral("current")).toInt()).arg(total);
                updateStatus(); continue;
            }
        }
        errors_ += line + '\n';
        if (errors_.size() > 16000) errors_ = errors_.right(16000);
    }
    if (errorPending_.size() > 16000) errorPending_ = errorPending_.right(16000);
}

void MeetingAnalysisDialog::updateStatus()
{
    status_->setText(QStringLiteral("%1 · Elapsed: %2").arg(stage_, elapsedText(elapsed_.isValid() ? elapsed_.elapsed() / 1000 : 0)));
}

void MeetingAnalysisDialog::finishAnalysis(int exitCode, QProcess::ExitStatus exitStatus)
{
    readErrors(true);
    const QByteArray output = process_.readAllStandardOutput();
    if (destroying_) return;
    QString message;
    if (canceled_) message = QStringLiteral("Analysis canceled.");
    else if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        QString error = QString::fromUtf8(errors_).trimmed().right(3000);
        const QString envKey = qEnvironmentVariable("OPENAI_API_KEY");
        if (!envKey.isEmpty()) error.replace(envKey, QStringLiteral("[key hidden]"));
        const QString codexKey = qEnvironmentVariable("CODEX_API_KEY");
        if (!codexKey.isEmpty()) error.replace(codexKey, QStringLiteral("[key hidden]"));
        message = error.isEmpty() ? QStringLiteral("Could not start or complete analysis: %1").arg(process_.errorString()) : error;
    } else {
        const QJsonDocument doc = QJsonDocument::fromJson(output);
        const QString markdown = doc.object().value(QStringLiteral("markdown")).toString();
        if (markdown.trimmed().isEmpty()) message = QStringLiteral("Analysis returned an invalid or empty result.");
        else {
            const QDir meetingDir(meeting_.directory);
            if (!QDir().mkpath(meetingDir.filePath(QStringLiteral("analyses")))) message = QStringLiteral("Could not create the analysis directory.");
            else {
                const QDateTime now = QDateTime::currentDateTimeUtc();
                const QString name = now.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")) + QLatin1Char('-') + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) + QStringLiteral(".md");
                const QString path = QDir(meetingDir.filePath(QStringLiteral("analyses"))).filePath(name);
                QJsonObject meta{{QStringLiteral("created"), now.toString(Qt::ISODateWithMs)}, {QStringLiteral("backend"), backend_},
                        {QStringLiteral("model"), model_.isEmpty() ? QStringLiteral("Default (Codex)") : model_}, {QStringLiteral("transcript_sha256"), QString::fromLatin1(snapshotHash_)}};
                if (!reasoningEffort_.isEmpty()) meta.insert(QStringLiteral("reasoning_effort"), reasoningEffort_);
                const QByteArray contents = QByteArray("<!-- STC_ANALYSIS ") + QJsonDocument(meta).toJson(QJsonDocument::Compact) + " -->\n" + markdown.toUtf8();
                QSaveFile file(path);
                if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) message = QStringLiteral("Could not save analysis: %1").arg(file.errorString());
                else { message = QStringLiteral("Analysis saved."); reloadVersions(path); emit analysisSaved(); }
            }
        }
    }
    transcriptSnapshot_.clear();
    analyze_->setEnabled(true); cancel_->setEnabled(false); backendCombo_->setEnabled(true); modelCombo_->setEnabled(true); refreshModels_->setEnabled(true); reasoningCombo_->setEnabled(true);
    stage_ = message; updateStatus(); emit busyChanged(false);
}

void MeetingAnalysisDialog::reloadVersions(const QString &select)
{
    const QString old = select.isEmpty() && versions_->currentItem() ? versions_->currentItem()->data(Qt::UserRole).toString() : select;
    versions_->clear();
    const QDir dir(QDir(meeting_.directory).filePath(QStringLiteral("analyses")));
    for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.md")}, QDir::Files | QDir::NoSymLinks, QDir::Name | QDir::Reversed)) {
        QFile file(info.absoluteFilePath()); if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonObject meta = metadata(file.readLine());
        const QDateTime date = QDateTime::fromString(meta.value(QStringLiteral("created")).toString(), Qt::ISODateWithMs).toLocalTime();
        QString label = date.isValid() ? QStringLiteral("%1 — %2 / %3").arg(date.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), meta.value(QStringLiteral("backend")).toString(), meta.value(QStringLiteral("model")).toString()) : info.fileName();
        const QString effort = meta.value(QStringLiteral("reasoning_effort")).toString();
        if (!effort.isEmpty()) label += QStringLiteral(" / %1").arg(effort);
        auto *item = new QListWidgetItem(label, versions_); item->setData(Qt::UserRole, info.absoluteFilePath());
        if (info.absoluteFilePath() == old) versions_->setCurrentItem(item);
    }
    if (!versions_->currentItem() && versions_->count()) versions_->setCurrentRow(0);
    showVersion();
}

void MeetingAnalysisDialog::showVersion()
{
    const auto *item = versions_->currentItem();
    open_->setEnabled(item); copy_->setEnabled(item);
    if (!item) { preview_->clear(); versionInfo_->setText(QStringLiteral("No saved analyses.")); return; }
    QFile file(item->data(Qt::UserRole).toString());
    if (!file.open(QIODevice::ReadOnly)) { preview_->clear(); versionInfo_->setText(QStringLiteral("Could not read analysis.")); return; }
    const QByteArray contents = file.readAll();
    const QJsonObject meta = metadata(contents);
    const qsizetype header = contents.indexOf(" -->\n");
    preview_->setPlainText(QString::fromUtf8(!meta.isEmpty() && header >= 0 ? contents.mid(header + 5) : contents));
    const QByteArray savedHash = meta.value(QStringLiteral("transcript_sha256")).toString().toLatin1();
    const QByteArray currentHash = transcriptHash();
    if (savedHash.isEmpty() || currentHash.isEmpty()) versionInfo_->setText(QStringLiteral("Cannot verify whether the transcript has changed."));
    else if (savedHash != currentHash) versionInfo_->setText(QStringLiteral("This analysis is based on an older version of the transcript."));
    else versionInfo_->setText(QStringLiteral("Based on the current transcript."));
}
