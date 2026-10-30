#include "MeetingDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

namespace {
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
}

MeetingDialog::MeetingDialog(const AppSettings &settings, WhisperRunner *whisper, std::function<bool()> appIdle, QWidget *parent)
    : QDialog(parent), settings_(settings), whisper_(whisper), appIdle_(std::move(appIdle))
{
    setWindowTitle(QStringLiteral("Meeting Recordings"));
    resize(900, 480);
    auto *layout = new QVBoxLayout(this);
    auto *devices = new QGridLayout;
    microphone_ = new QComboBox(this);
    output_ = new QComboBox(this);
    auto *refreshDevices = new QPushButton(QStringLiteral("Refresh devices"), this);
    devices->addWidget(new QLabel(QStringLiteral("Microphone"), this), 0, 0);
    devices->addWidget(microphone_, 0, 1);
    devices->addWidget(new QLabel(QStringLiteral("Output monitor"), this), 1, 0);
    devices->addWidget(output_, 1, 1);
    devices->addWidget(refreshDevices, 0, 2, 2, 1);
    layout->addLayout(devices);
    auto *note = new QLabel(QStringLiteral("Records the microphone and all sound played through the selected output. Muting Jitsi does not mute this microphone recording."), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    table_ = new QTableWidget(0, 4, this);
    table_->setObjectName(QStringLiteral("meetingTable"));
    table_->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Date"), QStringLiteral("Duration"), QStringLiteral("Status")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(0, 180);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_);
    auto *buttons = new QGridLayout;
    record_ = new QPushButton(QStringLiteral("Start recording"), this);
    record_->setObjectName(QStringLiteral("meetingRecord"));
    stop_ = new QPushButton(QStringLiteral("Stop recording"), this);
    stop_->setObjectName(QStringLiteral("meetingStop"));
    transcribe_ = new QPushButton(QStringLiteral("Transcribe"), this);
    transcribe_->setObjectName(QStringLiteral("meetingTranscribe"));
    play_ = new QPushButton(QStringLiteral("Play"), this);
    open_ = new QPushButton(QStringLiteral("Open folder"), this);
    transcript_ = new QPushButton(QStringLiteral("Open transcript"), this);
    copy_ = new QPushButton(QStringLiteral("Copy transcript"), this);
    rename_ = new QPushButton(QStringLiteral("Rename"), this);
    remove_ = new QPushButton(QStringLiteral("Delete"), this);
    const QList<QPushButton *> all{record_, stop_, transcribe_, play_, open_, transcript_, copy_, rename_, remove_};
    for (int i = 0; i < all.size(); ++i) buttons->addWidget(all[i], i / 5, i % 5);
    layout->addLayout(buttons);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    connect(refreshDevices, &QPushButton::clicked, this, &MeetingDialog::loadSources);
    connect(record_, &QPushButton::clicked, this, &MeetingDialog::startRecording);
    connect(stop_, &QPushButton::clicked, this, &MeetingDialog::stopRecording);
    connect(transcribe_, &QPushButton::clicked, this, &MeetingDialog::startTranscription);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &MeetingDialog::updateButtons);
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
        errors_.append(process_.readAllStandardError());
        if (errors_.size() > 32768) errors_.remove(0, errors_.size() - 32768);
    });
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this]() { process_.readAllStandardOutput(); });
    connect(&process_, &QProcess::finished, this, &MeetingDialog::processFinished);
    connect(&process_, &QProcess::errorOccurred, this, &MeetingDialog::processError);
    connect(whisper_, &WhisperRunner::transcriptionReady, this, [this](const QString &text) { if (ownsWhisper()) finishWhisper(text, {}); });
    connect(whisper_, &WhisperRunner::failed, this, [this](const QString &error) { if (ownsWhisper()) finishWhisper({}, error); });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (job_ == Job::Recording && !stopping_) {
            status_->setText(QStringLiteral("Recording %1").arg(duration(elapsed_.elapsed() / 1000)));
            emit activityChanged();
        }
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

MeetingDialog::~MeetingDialog()
{
    shuttingDown_ = true;
    if (job_ == Job::Recording && process_.state() != QProcess::NotRunning) {
        stopping_ = true;
        process_.write("q\n");
        process_.waitForBytesWritten(1000);
        if (!process_.waitForFinished(5000)) { forcedStop_ = true; process_.kill(); process_.waitForFinished(1000); }
    } else if (process_.state() != QProcess::NotRunning) {
        process_.kill(); process_.waitForFinished(1000);
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
    if (job_ == Job::Converting || job_ == Job::Whisper) return QStringLiteral("Transcribing meeting.");
    return {};
}
void MeetingDialog::setJob(Job job) { job_ = job; updateButtons(); emit busyChanged(); }
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
                && ((job_ != Job::Converting && job_ != Job::Whisper) || current_.directory != entry.directory))
            state = QStringLiteral("Transcription interrupted");
        else if (QFileInfo::exists(MeetingStore::transcriptPath(entry)) && state == QStringLiteral("Ready")) state = QStringLiteral("Transcribed");
        const QStringList values{entry.name, entry.started.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")), duration(entry.durationSeconds), state};
        for (int c = 0; c < values.size(); ++c) table_->setItem(i, c, new QTableWidgetItem(values[c]));
        if (entry.directory == selected) table_->selectRow(i);
    }
    updateButtons();
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
    play_->setEnabled(!isBusy() && audio);
    open_->setEnabled(selected);
    transcript_->setEnabled(text);
    copy_->setEnabled(text);
    rename_->setEnabled(!isBusy() && selected);
    remove_->setEnabled(!isBusy() && selected);
    microphone_->setEnabled(!isBusy()); output_->setEnabled(!isBusy());
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
    setJob(Job::Converting);
    process_.start(QStringLiteral("ffmpeg"), {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-i"), MeetingStore::audioPath(current_), QStringLiteral("-ac"), QStringLiteral("1"),
            QStringLiteral("-ar"), QStringLiteral("16000"), QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), QStringLiteral("-y"), temporaryWav_});
    status_->setText(QStringLiteral("Preparing audio for transcription…"));
    refresh();
}

void MeetingDialog::processError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart && (job_ == Job::Recording || job_ == Job::Converting)) {
        errors_.append(process_.errorString().toUtf8());
        processFinished(-1, QProcess::CrashExit);
    }
}

void MeetingDialog::processFinished(int code, QProcess::ExitStatus exitStatus)
{
    errors_.append(process_.readAllStandardError());
    const QString details = QString::fromUtf8(errors_).trimmed();
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
    } else if (job_ == Job::Converting) {
        if (exitStatus != QProcess::NormalExit || code != 0 || QFileInfo(temporaryWav_).size() == 0) {
            QFile::remove(temporaryWav_); temporaryWav_.clear();
            current_.status = QStringLiteral("Transcription failed");
            saveEntry(current_);
            setJob(Job::None); refresh();
            report(QStringLiteral("Could not prepare the recording for transcription: %1").arg(details));
            return;
        }
        setJob(Job::Whisper);
        status_->setText(QStringLiteral("Transcribing meeting…"));
        AppSettings options = settings_;
        options.translateToEn = false;
        whisper_->transcribe(temporaryWav_, options, true);
    }
}

void MeetingDialog::finishWhisper(const QString &text, const QString &error)
{
    if (!temporaryWav_.isEmpty()) { QFile::remove(temporaryWav_); temporaryWav_.clear(); }
    QString failure = error;
    if (failure.isEmpty() && text.trimmed().isEmpty()) failure = QStringLiteral("Whisper returned no text.");
    if (failure.isEmpty()) {
        QSaveFile file(MeetingStore::transcriptPath(current_));
        const QByteArray data = (text.trimmed() + QLatin1Char('\n')).toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
            failure = QStringLiteral("Could not save transcript: %1").arg(file.errorString());
    }
    current_.status = failure.isEmpty() ? QStringLiteral("Ready") : QStringLiteral("Transcription failed");
    saveEntry(current_);
    setJob(Job::None); refresh();
    if (!failure.isEmpty()) report(failure);
    else status_->setText(QStringLiteral("Transcription saved."));
}
