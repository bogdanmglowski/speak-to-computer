#include "MeetingDialog.h"
#include "MeetingAnalysisDialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QSaveFile>
#include <QShowEvent>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

namespace {
QString speakerDataPath(const MeetingEntry &entry)
{
    return QDir(entry.directory).filePath(QStringLiteral("speakers.json"));
}

bool saveSpeakers(const MeetingEntry &entry, const QJsonObject &document, QString *error)
{
    const QJsonArray segments = document.value(QStringLiteral("segments")).toArray();
    if (segments.isEmpty()) { *error = QStringLiteral("No speaker segments returned."); return false; }
    QString text;
    for (const QJsonValue &value : segments) {
        const QJsonObject segment = value.toObject();
        if (!segment.value(QStringLiteral("start")).isDouble() || !segment.value(QStringLiteral("speaker")).isString()
                || !segment.value(QStringLiteral("text")).isString()) {
            *error = QStringLiteral("Invalid speaker segment returned."); return false;
        }
        const qint64 seconds = qMax<qint64>(0, static_cast<qint64>(segment.value(QStringLiteral("start")).toDouble()));
        text += QStringLiteral("[%1:%2:%3] %4: %5\n")
                .arg(seconds / 3600, 2, 10, QLatin1Char('0')).arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
                .arg(seconds % 60, 2, 10, QLatin1Char('0'))
                .arg(segment.value(QStringLiteral("speaker")).toString(), segment.value(QStringLiteral("text")).toString());
    }
    QSaveFile json(speakerDataPath(entry));
    QSaveFile transcript(MeetingStore::transcriptPath(entry));
    const QByteArray jsonBytes = QJsonDocument(document).toJson();
    const QByteArray textBytes = text.toUtf8();
    if (!json.open(QIODevice::WriteOnly) || json.write(jsonBytes) != jsonBytes.size()
            || !transcript.open(QIODevice::WriteOnly) || transcript.write(textBytes) != textBytes.size()) {
        *error = QStringLiteral("Could not write speaker transcript."); return false;
    }
    if (!json.commit() || !transcript.commit()) {
        *error = QStringLiteral("Could not save speaker transcript."); return false;
    }
    return true;
}

QString commandOutput(const QStringList &args, QString *error)
{
    QProcess process;
    process.start(QStringLiteral("pactl"), args);
    if (!process.waitForStarted(2000) || !process.waitForFinished(3000) || process.exitCode() != 0) {
        if (error) *error = QStringLiteral("Could not query audio devices with pactl: %1")
                .arg(QString::fromUtf8(process.readAllStandardError()).trimmed());
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}
QString duration(qint64 seconds)
{
    return QStringLiteral("%1:%2:%3").arg(seconds / 3600, 2, 10, QLatin1Char('0'))
            .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
QString fileSize(qint64 bytes)
{
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    static const QStringList units{QStringLiteral("KiB"), QStringLiteral("MiB"), QStringLiteral("GiB"), QStringLiteral("TiB")};
    double size = static_cast<double>(bytes);
    int unit = -1;
    do {
        size /= 1024.0;
        ++unit;
    } while (size >= 1024.0 && unit + 1 < units.size());
    return QStringLiteral("%1 %2").arg(QString::number(size, 'f', 1), units[unit]);
}
}

MeetingDialog::MeetingDialog(const AppSettings &settings, WhisperRunner *whisper, std::function<bool()> appIdle, QWidget *parent)
    : QDialog(parent), settings_(settings), whisper_(whisper), appIdle_(std::move(appIdle))
{
    setWindowTitle(QStringLiteral("Meeting Recordings"));
    resize(1180, 740);
    setMinimumSize(1040, 640);
    setAttribute(Qt::WA_DeleteOnClose, false);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(12);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("Meeting Recordings"), this);
    title->setObjectName(QStringLiteral("meetingHeaderTitle"));
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(false);
    title->setFont(titleFont);
    header->addWidget(title);
    header->addStretch();
    recordingIndicator_ = new QLabel(this);
    recordingIndicator_->setObjectName(QStringLiteral("meetingRecordingIndicator"));
    recordingIndicator_->setAccessibleName(QStringLiteral("Meeting recording indicator"));
    recordingIndicator_->setAlignment(Qt::AlignCenter);
    recordingIndicator_->setStyleSheet(QStringLiteral(
            "QLabel { color: #991b1b; background: #fee2e2; border: 1px solid #fecaca; "
            "border-radius: 10px; padding: 7px 12px; font-weight: 700; }"));
    recordingIndicator_->hide();
    header->addWidget(recordingIndicator_, 0, Qt::AlignTop);
    auto *hideToTray = new QPushButton(QStringLiteral("Hide to tray"), this);
    hideToTray->setObjectName(QStringLiteral("meetingHideToTray"));
    hideToTray->setToolTip(QStringLiteral("Hide this window; recording continues in the tray."));
    header->addWidget(hideToTray, 0, Qt::AlignTop);
    layout->addLayout(header);

    auto *devicesGroup = new QGroupBox(QStringLiteral("Audio sources"), this);
    auto *devices = new QGridLayout(devicesGroup);
    microphone_ = new QComboBox(this);
    output_ = new QComboBox(this);
    auto *refreshDevices = new QPushButton(QStringLiteral("Refresh devices"), this);
    devices->addWidget(new QLabel(QStringLiteral("Microphone"), this), 0, 0);
    devices->addWidget(microphone_, 0, 1);
    devices->addWidget(new QLabel(QStringLiteral("Output monitor"), this), 1, 0);
    devices->addWidget(output_, 1, 1);
    devices->addWidget(refreshDevices, 0, 2, 2, 1);
    layout->addWidget(devicesGroup);
    auto *note = new QLabel(QStringLiteral("Records the microphone and all sound played through the selected output. Muting Jitsi does not mute this microphone recording."), this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(mid); padding: 0 2px;"));
    layout->addWidget(note);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    auto *recordsPane = new QWidget(splitter);
    auto *recordsLayout = new QVBoxLayout(recordsPane);
    recordsLayout->setContentsMargins(0, 0, 8, 0);
    auto *recordsHeading = new QLabel(QStringLiteral("Recordings"), recordsPane);
    QFont sectionFont = recordsHeading->font();
    sectionFont.setBold(true);
    recordsHeading->setFont(sectionFont);
    recordsLayout->addWidget(recordsHeading);
    table_ = new QTableWidget(0, 4, recordsPane);
    table_->setObjectName(QStringLiteral("meetingTable"));
    table_->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Date"), QStringLiteral("Duration"), QStringLiteral("Status")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(0, 180);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setWordWrap(true);
    table_->setTextElideMode(Qt::ElideNone);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    recordsLayout->addWidget(table_);

    auto *detailsPane = new QFrame(splitter);
    detailsPane->setFrameShape(QFrame::StyledPanel);
    auto *detailsLayout = new QVBoxLayout(detailsPane);
    detailsLayout->setContentsMargins(16, 14, 16, 14);
    detailsLayout->setSpacing(10);
    auto *detailsHeading = new QLabel(QStringLiteral("Selected recording"), detailsPane);
    detailsHeading->setStyleSheet(QStringLiteral("color: palette(mid);"));
    detailsLayout->addWidget(detailsHeading);
    detailsTitle_ = new QLabel(QStringLiteral("Select a recording"), detailsPane);
    detailsTitle_->setObjectName(QStringLiteral("meetingDetailsTitle"));
    detailsTitle_->setWordWrap(true);
    QFont detailsTitleFont = detailsTitle_->font();
    detailsTitleFont.setPointSize(detailsTitleFont.pointSize() + 2);
    detailsTitleFont.setBold(true);
    detailsTitle_->setFont(detailsTitleFont);
    detailsLayout->addWidget(detailsTitle_);
    detailsStatus_ = new QLabel(QStringLiteral("No recording selected"), detailsPane);
    detailsStatus_->setObjectName(QStringLiteral("meetingDetailsStatus"));
    detailsLayout->addWidget(detailsStatus_);

    auto *metadata = new QGroupBox(QStringLiteral("Details"), detailsPane);
    auto *metadataLayout = new QFormLayout(metadata);
    detailsStarted_ = new QLabel(QStringLiteral("—"), metadata);
    detailsStarted_->setObjectName(QStringLiteral("meetingDetailsStarted"));
    detailsDuration_ = new QLabel(QStringLiteral("—"), metadata);
    detailsDuration_->setObjectName(QStringLiteral("meetingDetailsDuration"));
    detailsLocation_ = new QLabel(QStringLiteral("—"), metadata);
    detailsLocation_->setObjectName(QStringLiteral("meetingDetailsLocation"));
    detailsLocation_->setWordWrap(true);
    detailsLocation_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    metadataLayout->addRow(QStringLiteral("Started"), detailsStarted_);
    metadataLayout->addRow(QStringLiteral("Duration"), detailsDuration_);
    metadataLayout->addRow(QStringLiteral("Folder"), detailsLocation_);
    detailsLayout->addWidget(metadata);

    auto *filesHeading = new QLabel(QStringLiteral("Files"), detailsPane);
    filesHeading->setFont(sectionFont);
    detailsLayout->addWidget(filesHeading);
    filesTable_ = new QTableWidget(0, 3, detailsPane);
    filesTable_->setObjectName(QStringLiteral("meetingFilesTable"));
    filesTable_->setHorizontalHeaderLabels({QStringLiteral("File"), QStringLiteral("Size"), QStringLiteral("Modified")});
    filesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    filesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    filesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    filesTable_->verticalHeader()->hide();
    filesTable_->setSelectionMode(QAbstractItemView::NoSelection);
    filesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    filesTable_->setAlternatingRowColors(true);
    filesTable_->setMinimumHeight(150);
    detailsLayout->addWidget(filesTable_, 1);
    splitter->addWidget(recordsPane);
    splitter->addWidget(detailsPane);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({680, 430});
    layout->addWidget(splitter, 1);

    auto *buttons = new QGridLayout;
    record_ = new QPushButton(QStringLiteral("Start recording"), this);
    record_->setObjectName(QStringLiteral("meetingRecord"));
    stop_ = new QPushButton(QStringLiteral("Stop recording"), this);
    stop_->setObjectName(QStringLiteral("meetingStop"));
    transcribe_ = new QPushButton(QStringLiteral("Transcribe"), this);
    transcribe_->setObjectName(QStringLiteral("meetingTranscribe"));
    transcribeSpeakers_ = new QPushButton(QStringLiteral("Transcribe with speakers"), this);
    transcribeSpeakers_->setObjectName(QStringLiteral("meetingTranscribeSpeakers"));
    renameSpeaker_ = new QPushButton(QStringLiteral("Name speaker"), this);
    renameSpeaker_->setObjectName(QStringLiteral("meetingRenameSpeaker"));
    play_ = new QPushButton(QStringLiteral("Play"), this);
    open_ = new QPushButton(QStringLiteral("Open folder"), this);
    transcript_ = new QPushButton(QStringLiteral("Open transcript"), this);
    copy_ = new QPushButton(QStringLiteral("Copy transcript"), this);
    analysis_ = new QPushButton(QStringLiteral("Analyze transcript"), this);
    analysis_->setObjectName(QStringLiteral("meetingAnalyze"));
    rename_ = new QPushButton(QStringLiteral("Rename"), this);
    remove_ = new QPushButton(QStringLiteral("Delete"), this);
    auto *closeWindow = new QPushButton(QStringLiteral("Close window"), this);
    closeWindow->setObjectName(QStringLiteral("meetingCloseWindow"));
    closeWindow->setToolTip(QStringLiteral("Hide this window without stopping an active recording."));
    const QList<QPushButton *> all{record_, stop_, transcribe_, transcribeSpeakers_, renameSpeaker_, play_, open_, transcript_, copy_, analysis_, rename_, remove_, closeWindow};
    for (int i = 0; i < all.size(); ++i) buttons->addWidget(all[i], i / 5, i % 5);
    layout->addLayout(buttons);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("meetingStatus"));
    status_->setWordWrap(true);
    layout->addWidget(status_);
    transcriptionProgress_ = new QProgressBar(this);
    transcriptionProgress_->setObjectName(QStringLiteral("meetingTranscriptionProgress"));
    transcriptionProgress_->hide();
    layout->addWidget(transcriptionProgress_);
    connect(refreshDevices, &QPushButton::clicked, this, &MeetingDialog::loadSources);
    connect(record_, &QPushButton::clicked, this, &MeetingDialog::startRecording);
    connect(stop_, &QPushButton::clicked, this, &MeetingDialog::stopRecording);
    connect(transcribe_, &QPushButton::clicked, this, &MeetingDialog::startTranscription);
    connect(transcribeSpeakers_, &QPushButton::clicked, this, &MeetingDialog::startSpeakerTranscription);
    connect(renameSpeaker_, &QPushButton::clicked, this, &MeetingDialog::renameSpeaker);
    connect(analysis_, &QPushButton::clicked, this, [this]() {
        if (isBusy() || !appIdle_()) return;
        MeetingAnalysisDialog dialog(selectedEntry(), settings_, this);
        connect(&dialog, &MeetingAnalysisDialog::busyChanged, this, [this](bool busy) {
            setJob(busy ? Job::Analyzing : Job::None);
            emit activityChanged();
        });
        connect(&dialog, &MeetingAnalysisDialog::analysisSaved, this, &MeetingDialog::refresh);
        dialog.exec();
        if (job_ == Job::Analyzing) { setJob(Job::None); emit activityChanged(); }
    });
    connect(hideToTray, &QPushButton::clicked, this, &QWidget::close);
    connect(closeWindow, &QPushButton::clicked, this, &QWidget::close);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this]() {
        updateButtons();
        updateDetails();
    });
    connect(microphone_, &QComboBox::currentIndexChanged, this, &MeetingDialog::updateButtons);
    connect(output_, &QComboBox::currentIndexChanged, this, &MeetingDialog::updateButtons);
    connect(play_, &QPushButton::clicked, this, [this]() {
        if (!QProcess::startDetached(QStringLiteral("ffplay"), {QStringLiteral("-nodisp"), QStringLiteral("-autoexit"), MeetingStore::audioPath(selectedEntry())}))
            report(QStringLiteral("Could not start ffplay. Install FFmpeg's ffplay to play recordings."));
    });
    connect(open_, &QPushButton::clicked, this, [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(selectedEntry().directory)); });
    connect(transcript_, &QPushButton::clicked, this, [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(MeetingStore::transcriptPath(selectedEntry()))); });
    connect(copy_, &QPushButton::clicked, this, [this]() {
        QFile file(MeetingStore::transcriptPath(selectedEntry()));
        if (!file.open(QIODevice::ReadOnly)) { report(file.errorString()); return; }
        QApplication::clipboard()->setText(QString::fromUtf8(file.readAll()));
    });
    connect(rename_, &QPushButton::clicked, this, [this]() {
        MeetingEntry entry = selectedEntry();
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Rename meeting"), QStringLiteral("Name"), QLineEdit::Normal, entry.name, &ok).trimmed();
        if (ok && !name.isEmpty()) { entry.name = name; if (saveEntry(entry)) refresh(); }
    });
    connect(remove_, &QPushButton::clicked, this, [this]() {
        const MeetingEntry entry = selectedEntry();
        if (QMessageBox::question(this, QStringLiteral("Delete meeting"), QStringLiteral("Delete '%1' and all files in its folder?").arg(entry.name)) != QMessageBox::Yes) return;
        if (!QDir(entry.directory).removeRecursively()) report(QStringLiteral("Could not delete the meeting folder: %1").arg(entry.directory));
        refresh();
    });
    connect(&process_, &QProcess::readyReadStandardError, this, [this]() {
        const QByteArray chunk = process_.readAllStandardError();
        if (job_ == Job::Diarizing) consumeDiarizationError(chunk);
        else errors_.append(chunk);
        if (errors_.size() > 32768) errors_.remove(0, errors_.size() - 32768);
    });
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this]() {
        const QByteArray output = process_.readAllStandardOutput();
        if (job_ == Job::Diarizing) outputBuffer_.append(output);
    });
    connect(&process_, &QProcess::finished, this, &MeetingDialog::processFinished);
    connect(&process_, &QProcess::errorOccurred, this, &MeetingDialog::processError);
    connect(whisper_, &WhisperRunner::transcriptionReady, this, [this](const QString &text) { if (ownsWhisper()) finishWhisper(text, {}); });
    connect(whisper_, &WhisperRunner::failed, this, [this](const QString &error) { if (ownsWhisper()) finishWhisper({}, error); });
    connect(whisper_, &WhisperRunner::progressChanged, this, [this](int percent) {
        if (ownsWhisper()) setTranscriptionProgress(QStringLiteral("Transcribing meeting"), percent);
    });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (job_ == Job::Recording && !stopping_) {
            status_->setText(QStringLiteral("Recording %1").arg(duration(elapsed_.elapsed() / 1000)));
            emit activityChanged();
        }
        if (job_ == Job::Converting || job_ == Job::Whisper || job_ == Job::Diarizing)
            updateTranscriptionStatus();
        updateRecordingIndicator();
        updateDetails();
        updateButtons();
    });
    timer->start(1000);
    refresh();
}

void MeetingDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!sourcesLoaded_) loadSources();
}

void MeetingDialog::closeEvent(QCloseEvent *event)
{
    hide();
    event->accept();
}

MeetingDialog::~MeetingDialog()
{
    shuttingDown_ = true;
    if (job_ == Job::Recording && process_.state() != QProcess::NotRunning) {
        stopping_ = true;
        process_.write("q\n");
        process_.waitForBytesWritten(1000);
        if (!process_.waitForFinished(5000)) { forcedStop_ = true; process_.kill(); process_.waitForFinished(1000); }
    } else if (process_.state() != QProcess::NotRunning) {
        process_.terminate();
        if (!process_.waitForFinished(2000)) { process_.kill(); process_.waitForFinished(1000); }
    }
    if (!temporaryWav_.isEmpty()) QFile::remove(temporaryWav_);
}

void MeetingDialog::setSettings(const AppSettings &settings) { settings_ = settings; refresh(); }
bool MeetingDialog::isBusy() const { return job_ != Job::None; }
bool MeetingDialog::isRecording() const { return job_ == Job::Recording; }
bool MeetingDialog::canStopRecording() const { return isRecording() && !stopping_; }
bool MeetingDialog::ownsWhisper() const { return job_ == Job::Whisper; }
QString MeetingDialog::activityStatus() const
{
    if (job_ == Job::Recording) {
        if (stopping_) return QStringLiteral("Finishing meeting recording…");
        return QStringLiteral("Recording meeting — %1").arg(duration(elapsed_.elapsed() / 1000));
    }
    if (job_ == Job::Converting || job_ == Job::Whisper || job_ == Job::Diarizing) return QStringLiteral("Transcribing meeting.");
    if (job_ == Job::Analyzing) return QStringLiteral("Analyzing meeting.");
    return {};
}
void MeetingDialog::setJob(Job job)
{
    job_ = job;
    if (job == Job::None) transcriptionProgress_->hide();
    updateButtons();
    updateDetails();
    updateRecordingIndicator();
    emit busyChanged();
}

void MeetingDialog::setTranscriptionProgress(const QString &stage, int percent)
{
    if (stage != transcriptionStage_) {
        transcriptionStage_ = stage;
        transcriptionPercent_ = -1;
        stageElapsed_.start();
    }
    if (percent >= 0 && percent <= 100 && percent > transcriptionPercent_) transcriptionPercent_ = percent;
    updateTranscriptionStatus();
}

QString MeetingDialog::transcriptionDuration() const
{
    return duration(transcriptionElapsed_.isValid() ? transcriptionElapsed_.elapsed() / 1000 : 0);
}

void MeetingDialog::updateTranscriptionStatus()
{
    if (!transcriptionElapsed_.isValid() || transcriptionStage_.isEmpty()) return;
    QString text = transcriptionStage_;
    if (transcriptionPercent_ >= 0) {
        transcriptionProgress_->setRange(0, 100);
        transcriptionProgress_->setValue(transcriptionPercent_);
        text += QStringLiteral(": %1%").arg(transcriptionPercent_);
    } else {
        transcriptionProgress_->setRange(0, 0);
    }
    text += QStringLiteral("  ·  Elapsed: %1").arg(transcriptionDuration());
    if (transcriptionPercent_ >= 10 && transcriptionPercent_ < 100 && stageElapsed_.elapsed() >= 10000) {
        const qint64 remaining = stageElapsed_.elapsed() * (100 - transcriptionPercent_) / transcriptionPercent_ / 1000;
        text += QStringLiteral("  ·  Stage remaining (estimate): %1").arg(duration(remaining));
    }
    status_->setText(text);
}

void MeetingDialog::consumeDiarizationError(const QByteArray &chunk, bool final)
{
    diarizationErrorPending_.append(chunk);
    qsizetype end;
    while ((end = diarizationErrorPending_.indexOf('\n')) >= 0 || (final && !diarizationErrorPending_.isEmpty())) {
        if (end < 0) end = diarizationErrorPending_.size();
        const QByteArray line = diarizationErrorPending_.left(end);
        diarizationErrorPending_.remove(0, qMin(end + 1, diarizationErrorPending_.size()));
        if (line.startsWith("STC_PROGRESS ")) {
            const QJsonObject event = QJsonDocument::fromJson(line.mid(13)).object();
            const QString key = event.value(QStringLiteral("stage")).toString();
            const QMap<QString, QString> stages{
                    {QStringLiteral("loading_model"), QStringLiteral("Loading speaker model")},
                    {QStringLiteral("prepare_microphone"), QStringLiteral("Preparing microphone audio")},
                    {QStringLiteral("transcribe_microphone"), QStringLiteral("Transcribing microphone")},
                    {QStringLiteral("prepare_remote"), QStringLiteral("Preparing remote audio")},
                    {QStringLiteral("transcribe_remote"), QStringLiteral("Transcribing remote speakers")},
                    {QStringLiteral("speaker_segmentation"), QStringLiteral("Detecting speech")},
                    {QStringLiteral("speaker_counting"), QStringLiteral("Counting speakers")},
                    {QStringLiteral("speaker_embeddings"), QStringLiteral("Identifying speakers")},
                    {QStringLiteral("speaker_finalize"), QStringLiteral("Finalizing speaker identification")},
                    {QStringLiteral("saving"), QStringLiteral("Preparing transcript")}};
            const auto it = stages.constFind(key);
            if (it != stages.cend()) {
                const QJsonValue value = event.value(QStringLiteral("percent"));
                const int percent = value.isDouble() ? value.toInt(-1) : -1;
                setTranscriptionProgress(it.value(), percent >= 0 && percent <= 100 ? percent : -1);
                continue;
            }
        }
        errors_ += line + '\n';
        if (errors_.size() > 32768) errors_.remove(0, errors_.size() - 32768);
    }
    if (diarizationErrorPending_.size() > 32768) {
        errors_ += diarizationErrorPending_.left(diarizationErrorPending_.size() - 1024);
        diarizationErrorPending_ = diarizationErrorPending_.right(1024);
        if (errors_.size() > 32768) errors_.remove(0, errors_.size() - 32768);
    }
}
void MeetingDialog::report(const QString &message) {
    status_->setText(message);
    if (shuttingDown_) qWarning().noquote() << message;
    else QMessageBox::warning(this, QStringLiteral("Meeting Recordings"), message);
}

void MeetingDialog::loadSources()
{
    if (isBusy()) return;
    sourcesLoaded_ = true;
    QString error;
    const QString sourceList = commandOutput({QStringLiteral("list"), QStringLiteral("short"), QStringLiteral("sources")}, &error);
    const QString defaultSource = commandOutput({QStringLiteral("get-default-source")}, nullptr);
    const QString defaultSink = commandOutput({QStringLiteral("get-default-sink")}, nullptr);
    microphone_->clear(); output_->clear();
    for (const QString &line : sourceList.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() < 2) continue;
        const QString name = fields[1];
        QComboBox *combo = name.endsWith(QStringLiteral(".monitor")) ? output_ : microphone_;
        combo->addItem(name, name);
    }
    const int sourceIndex = microphone_->findData(defaultSource);
    const int sinkIndex = output_->findData(defaultSink + QStringLiteral(".monitor"));
    if (sourceIndex >= 0) microphone_->setCurrentIndex(sourceIndex);
    if (sinkIndex >= 0) output_->setCurrentIndex(sinkIndex);
    if (!error.isEmpty()) status_->setText(error);
    else if (!microphone_->count() || !output_->count()) status_->setText(QStringLiteral("A microphone and an output monitor are required. Check PulseAudio/PipeWire and refresh devices."));
    else status_->setText(QStringLiteral("Ready. Please inform meeting participants before recording."));
    updateButtons();
}

MeetingEntry MeetingDialog::selectedEntry() const
{
    const int row = table_->currentRow();
    return row >= 0 && row < entries_.size() ? entries_[row] : MeetingEntry{};
}

void MeetingDialog::refresh()
{
    const QString selected = selectedEntry().directory;
    entries_ = MeetingStore::list(settings_.meetingsDirectory);
    table_->setRowCount(entries_.size());
    for (int i = 0; i < entries_.size(); ++i) {
        const auto &entry = entries_[i];
        QString state = entry.status;
        if (state == QStringLiteral("Recording") && (job_ != Job::Recording || current_.directory != entry.directory)) state = QStringLiteral("Interrupted");
        else if (state == QStringLiteral("Transcribing")
                && ((job_ != Job::Converting && job_ != Job::Whisper && job_ != Job::Diarizing) || current_.directory != entry.directory))
            state = QStringLiteral("Transcription interrupted");
        else if (QFileInfo::exists(MeetingStore::transcriptPath(entry)) && state == QStringLiteral("Ready")) state = QStringLiteral("Transcribed");
        const QStringList values{entry.name, entry.started.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")), duration(entry.durationSeconds), state};
        for (int c = 0; c < values.size(); ++c) table_->setItem(i, c, new QTableWidgetItem(values[c]));
        if (entry.directory == selected) table_->selectRow(i);
    }
    table_->resizeRowsToContents();
    updateButtons();
    updateDetails();
}

void MeetingDialog::updateButtons()
{
    const MeetingEntry entry = selectedEntry();
    const bool selected = !entry.directory.isEmpty();
    const bool idle = !isBusy() && appIdle_();
    const bool audio = selected && QFileInfo(MeetingStore::audioPath(entry)).size() > 0;
    const bool text = selected && QFileInfo::exists(MeetingStore::transcriptPath(entry));
    record_->setEnabled(idle && microphone_->count() && output_->count());
    stop_->setEnabled(job_ == Job::Recording && !stopping_);
    transcribe_->setEnabled(idle && audio && !whisper_->isRunning());
    transcribeSpeakers_->setEnabled(idle && audio && !whisper_->isRunning());
    renameSpeaker_->setEnabled(idle && selected && QFileInfo::exists(speakerDataPath(entry)));
    analysis_->setEnabled(idle && text);
    play_->setEnabled(!isBusy() && audio);
    open_->setEnabled(selected);
    transcript_->setEnabled(text);
    copy_->setEnabled(text);
    rename_->setEnabled(!isBusy() && selected);
    remove_->setEnabled(!isBusy() && selected);
    microphone_->setEnabled(!isBusy()); output_->setEnabled(!isBusy());
}

void MeetingDialog::updateDetails()
{
    const MeetingEntry entry = selectedEntry();
    if (entry.directory.isEmpty()) {
        detailsTitle_->setText(QStringLiteral("Select a recording"));
        detailsStatus_->setText(QStringLiteral("No recording selected"));
        detailsLocation_->setText(QStringLiteral("—"));
        detailsStarted_->setText(QStringLiteral("—"));
        detailsDuration_->setText(QStringLiteral("—"));
        filesTable_->setRowCount(0);
        return;
    }

    QString state = entry.status;
    if (state == QStringLiteral("Recording") && (job_ != Job::Recording || current_.directory != entry.directory)) {
        state = QStringLiteral("Interrupted");
    } else if (state == QStringLiteral("Transcribing")
            && ((job_ != Job::Converting && job_ != Job::Whisper && job_ != Job::Diarizing) || current_.directory != entry.directory)) {
        state = QStringLiteral("Transcription interrupted");
    } else if (QFileInfo::exists(MeetingStore::transcriptPath(entry)) && state == QStringLiteral("Ready")) {
        state = QStringLiteral("Transcribed");
    }

    detailsTitle_->setText(entry.name);
    detailsStatus_->setText(state);
    if (state == QStringLiteral("Recording") || state == QStringLiteral("Interrupted")
            || state == QStringLiteral("Recording interrupted")) {
        detailsStatus_->setStyleSheet(QStringLiteral("color: #b91c1c; font-weight: 700;"));
    } else if (state == QStringLiteral("Transcribing")) {
        detailsStatus_->setStyleSheet(QStringLiteral("color: #1d4ed8; font-weight: 700;"));
    } else {
        detailsStatus_->setStyleSheet(QStringLiteral("color: palette(text); font-weight: 600;"));
    }
    detailsLocation_->setText(entry.directory);
    detailsStarted_->setText(entry.started.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    const qint64 seconds = isRecording() && current_.directory == entry.directory
            ? elapsed_.elapsed() / 1000 : entry.durationSeconds;
    detailsDuration_->setText(duration(seconds));

    const QFileInfoList files = QDir(entry.directory).entryInfoList(
            QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDir::Name);
    filesTable_->setRowCount(files.size());
    for (int row = 0; row < files.size(); ++row) {
        const QFileInfo &file = files[row];
        auto *name = new QTableWidgetItem(file.fileName());
        auto *size = new QTableWidgetItem(fileSize(file.size()));
        auto *modified = new QTableWidgetItem(file.lastModified().toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        filesTable_->setItem(row, 0, name);
        filesTable_->setItem(row, 1, size);
        filesTable_->setItem(row, 2, modified);
    }
}

void MeetingDialog::updateRecordingIndicator()
{
    if (!isRecording()) {
        recordingIndicator_->hide();
        return;
    }
    const QString state = stopping_ ? QStringLiteral("Finishing") : duration(elapsed_.elapsed() / 1000);
    recordingIndicator_->setText(QStringLiteral("● REC · %1").arg(state));
    recordingIndicator_->setAccessibleDescription(QStringLiteral("Meeting recording active: %1").arg(state));
    recordingIndicator_->show();
}

bool MeetingDialog::saveEntry(const MeetingEntry &entry)
{
    QString error;
    if (MeetingStore::save(entry, &error)) return true;
    report(error);
    return false;
}

void MeetingDialog::startRecording()
{
    if (isBusy() || !appIdle_() || microphone_->currentData().toString().isEmpty() || output_->currentData().toString().isEmpty()) return;
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) { report(QStringLiteral("FFmpeg was not found in PATH.")); return; }
    QString error;
    if (!MeetingStore::create(settings_.meetingsDirectory, &current_, &error)) { report(error); return; }
    errors_.clear(); stopping_ = false; forcedStop_ = false; elapsed_.start();
    setJob(Job::Recording);
    process_.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    const QString filter = QStringLiteral("[0:a]aresample=48000:async=1:first_pts=0,aformat=sample_fmts=s16:channel_layouts=mono[m];[1:a]aresample=48000:async=1:first_pts=0,aformat=sample_fmts=s16:channel_layouts=mono[o];[m][o]amerge=inputs=2[out]");
    process_.start(QStringLiteral("ffmpeg"), {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-f"), QStringLiteral("pulse"), QStringLiteral("-i"), microphone_->currentData().toString(),
            QStringLiteral("-f"), QStringLiteral("pulse"), QStringLiteral("-i"), output_->currentData().toString(),
            QStringLiteral("-filter_complex"), filter, QStringLiteral("-map"), QStringLiteral("[out]"),
            QStringLiteral("-c:a"), QStringLiteral("flac"), QStringLiteral("-sample_fmt"), QStringLiteral("s16"),
            QStringLiteral("-y"), MeetingStore::audioPath(current_)});
    status_->setText(QStringLiteral("Starting recording…"));
    refresh();
}

void MeetingDialog::stopRecording()
{
    if (job_ != Job::Recording || stopping_) return;
    stopping_ = true;
    status_->setText(QStringLiteral("Finishing recording…"));
    updateRecordingIndicator();
    updateDetails();
    emit activityChanged();
    if (process_.state() == QProcess::Starting) {
        connect(&process_, &QProcess::started, this, [this]() {
            if (job_ == Job::Recording && stopping_) process_.write("q\n");
        }, Qt::SingleShotConnection);
    } else {
        process_.write("q\n");
    }
    updateButtons();
    const QString recordingDirectory = current_.directory;
    QTimer::singleShot(10000, this, [this, recordingDirectory]() {
        if (job_ == Job::Recording && current_.directory == recordingDirectory && stopping_
                && process_.state() != QProcess::NotRunning) { forcedStop_ = true; process_.kill(); }
    });
}

void MeetingDialog::startTranscription()
{
    if (isBusy() || !appIdle_() || whisper_->isRunning()) return;
    current_ = selectedEntry();
    if (current_.directory.isEmpty() || !QFileInfo::exists(MeetingStore::audioPath(current_))) return;
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) { report(QStringLiteral("FFmpeg was not found in PATH.")); return; }
    temporaryWav_ = QDir(current_.directory).filePath(QStringLiteral(".transcription-%1.wav").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    current_.status = QStringLiteral("Transcribing");
    if (!saveEntry(current_)) return;
    errors_.clear();
    transcriptionElapsed_.start(); transcriptionStage_.clear();
    setJob(Job::Converting);
    transcriptionProgress_->show();
    setTranscriptionProgress(QStringLiteral("Preparing audio for transcription"));
    process_.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    process_.start(QStringLiteral("ffmpeg"), {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-i"), MeetingStore::audioPath(current_), QStringLiteral("-ac"), QStringLiteral("1"),
            QStringLiteral("-ar"), QStringLiteral("16000"), QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), QStringLiteral("-y"), temporaryWav_});
    refresh();
}

void MeetingDialog::startSpeakerTranscription()
{
    if (isBusy() || !appIdle_() || whisper_->isRunning()) return;
    current_ = selectedEntry();
    if (current_.directory.isEmpty() || !QFileInfo::exists(MeetingStore::audioPath(current_))) return;
    const QString runtime = settings_.diarizationRuntimeDirectory.isEmpty()
            ? QDir::home().filePath(QStringLiteral(".local/share/speak-to-computer/diarization/.venv"))
            : settings_.diarizationRuntimeDirectory;
    const QString python = qEnvironmentVariable("STC_DIARIZATION_PYTHON", QDir(runtime).filePath(QStringLiteral("bin/python")));
    const QString script = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("meeting_diarization.py"));
    if (!QFileInfo::exists(python) || !QFileInfo::exists(script)) {
        report(QStringLiteral("Speaker diarization runtime is missing. See the Meeting recordings section in README for setup."));
        return;
    }
    current_.status = QStringLiteral("Transcribing");
    if (!saveEntry(current_)) return;
    errors_.clear(); outputBuffer_.clear(); diarizationErrorPending_.clear();
    transcriptionElapsed_.start(); transcriptionStage_.clear();
    setJob(Job::Diarizing);
    transcriptionProgress_->show();
    setTranscriptionProgress(QStringLiteral("Starting speaker transcription"));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (!settings_.huggingFaceToken.isEmpty()) environment.insert(QStringLiteral("HF_TOKEN"), settings_.huggingFaceToken);
    if (!settings_.huggingFaceHome.isEmpty()) {
        environment.insert(QStringLiteral("HF_HOME"), settings_.huggingFaceHome);
        environment.remove(QStringLiteral("HF_HUB_CACHE"));
        environment.remove(QStringLiteral("HUGGINGFACE_HUB_CACHE"));
    }
    process_.setProcessEnvironment(environment);
    process_.start(python, {script, QStringLiteral("--audio"), MeetingStore::audioPath(current_),
            QStringLiteral("--whisper"), settings_.whisperCli, QStringLiteral("--model"), settings_.model,
            QStringLiteral("--language"), settings_.language, QStringLiteral("--threads"), QString::number(settings_.threads)});
    refresh();
}

void MeetingDialog::renameSpeaker()
{
    const MeetingEntry entry = selectedEntry();
    QFile file(speakerDataPath(entry));
    if (!file.open(QIODevice::ReadOnly)) { report(file.errorString()); return; }
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) { report(QStringLiteral("Invalid speaker data.")); return; }
    QJsonObject root = document.object();
    QJsonArray segments = root.value(QStringLiteral("segments")).toArray();
    QStringList speakers;
    for (const QJsonValue &value : segments) {
        const QString name = value.toObject().value(QStringLiteral("speaker")).toString();
        if (!name.isEmpty() && name != QStringLiteral("Ja") && name != QStringLiteral("Osoba nieustalona")
                && !speakers.contains(name)) speakers.append(name);
    }
    if (speakers.isEmpty()) { report(QStringLiteral("No remote speakers found.")); return; }
    bool ok = false;
    const QString previous = QInputDialog::getItem(this, QStringLiteral("Name speaker"), QStringLiteral("Speaker"), speakers, 0, false, &ok);
    if (!ok) return;
    const QString name = QInputDialog::getText(this, QStringLiteral("Name speaker"), QStringLiteral("Name"), QLineEdit::Normal, previous, &ok).trimmed();
    if (!ok || name.isEmpty() || name == previous) return;
    if (name.contains(QLatin1Char('\n')) || name.contains(QLatin1Char('\r')) || name == QStringLiteral("Ja")
            || name == QStringLiteral("Osoba nieustalona") || speakers.contains(name)) {
        report(QStringLiteral("Choose a unique single-line name other than Ja.")); return;
    }
    for (qsizetype i = 0; i < segments.size(); ++i) {
        QJsonObject segment = segments[i].toObject();
        if (segment.value(QStringLiteral("speaker")).toString() == previous) {
            segment.insert(QStringLiteral("speaker"), name); segments[i] = segment;
        }
    }
    root.insert(QStringLiteral("segments"), segments);
    QString error;
    if (!saveSpeakers(entry, root, &error)) report(error);
    else { status_->setText(QStringLiteral("Speaker name saved.")); refresh(); }
}

void MeetingDialog::processError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart && (job_ == Job::Recording || job_ == Job::Converting || job_ == Job::Diarizing)) {
        errors_.append(process_.errorString().toUtf8());
        processFinished(-1, QProcess::CrashExit);
    }
}

void MeetingDialog::processFinished(int code, QProcess::ExitStatus exitStatus)
{
    if (job_ == Job::Diarizing) consumeDiarizationError(process_.readAllStandardError(), true);
    else errors_.append(process_.readAllStandardError());
    QString details = QString::fromUtf8(errors_).trimmed();
    if (!settings_.huggingFaceToken.isEmpty()) details.replace(settings_.huggingFaceToken, QStringLiteral("[token hidden]"));
    const QString envToken = qEnvironmentVariable("HF_TOKEN");
    if (!envToken.isEmpty()) details.replace(envToken, QStringLiteral("[token hidden]"));
    if (job_ == Job::Recording) {
        current_.durationSeconds = elapsed_.isValid() ? elapsed_.elapsed() / 1000 : 0;
        const bool ok = stopping_ && !forcedStop_ && exitStatus == QProcess::NormalExit && code == 0
                && QFileInfo(MeetingStore::audioPath(current_)).size() > 0;
        current_.status = ok ? QStringLiteral("Ready") : QStringLiteral("Recording interrupted");
        saveEntry(current_);
        setJob(Job::None);
        refresh();
        if (!ok) report(QStringLiteral("Recording did not finish successfully. Any captured audio has been kept in %1. %2")
                .arg(current_.directory, details));
        else status_->setText(QStringLiteral("Recording saved."));
    } else if (job_ == Job::Diarizing) {
        outputBuffer_.append(process_.readAllStandardOutput());
        setTranscriptionProgress(QStringLiteral("Saving transcript"));
        QString failure;
        if (exitStatus != QProcess::NormalExit || code != 0) failure = QStringLiteral("Speaker transcription failed: %1").arg(details);
        else {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(outputBuffer_, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) failure = QStringLiteral("Invalid output from speaker transcription.");
            else saveSpeakers(current_, doc.object(), &failure);
        }
        current_.status = failure.isEmpty() ? QStringLiteral("Ready") : QStringLiteral("Transcription failed");
        const QString elapsed = transcriptionDuration();
        saveEntry(current_); setJob(Job::None); refresh();
        if (!failure.isEmpty()) report(QStringLiteral("%1\nElapsed: %2").arg(failure, elapsed));
        else status_->setText(QStringLiteral("Speaker transcription saved. Elapsed: %1").arg(elapsed));
    } else if (job_ == Job::Converting) {
        if (exitStatus != QProcess::NormalExit || code != 0 || QFileInfo(temporaryWav_).size() == 0) {
            QFile::remove(temporaryWav_); temporaryWav_.clear();
            current_.status = QStringLiteral("Transcription failed");
            const QString elapsed = transcriptionDuration();
            saveEntry(current_);
            setJob(Job::None); refresh();
            report(QStringLiteral("Could not prepare the recording for transcription: %1\nElapsed: %2").arg(details, elapsed));
            return;
        }
        setJob(Job::Whisper);
        setTranscriptionProgress(QStringLiteral("Transcribing meeting"));
        AppSettings options = settings_;
        options.translateToEn = false;
        whisper_->transcribe(temporaryWav_, options, true);
    }
}

void MeetingDialog::finishWhisper(const QString &text, const QString &error)
{
    setTranscriptionProgress(QStringLiteral("Saving transcript"));
    if (!temporaryWav_.isEmpty()) { QFile::remove(temporaryWav_); temporaryWav_.clear(); }
    QString failure = error;
    if (failure.isEmpty() && text.trimmed().isEmpty()) failure = QStringLiteral("Whisper returned no text.");
    if (failure.isEmpty()) {
        QSaveFile file(MeetingStore::transcriptPath(current_));
        const QByteArray data = (text.trimmed() + QLatin1Char('\n')).toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
            failure = QStringLiteral("Could not save transcript: %1").arg(file.errorString());
        else QFile::remove(speakerDataPath(current_));
    }
    current_.status = failure.isEmpty() ? QStringLiteral("Ready") : QStringLiteral("Transcription failed");
    const QString elapsed = transcriptionDuration();
    saveEntry(current_);
    setJob(Job::None); refresh();
    if (!failure.isEmpty()) report(QStringLiteral("%1\nElapsed: %2").arg(failure, elapsed));
    else status_->setText(QStringLiteral("Transcription saved. Elapsed: %1").arg(elapsed));
}
