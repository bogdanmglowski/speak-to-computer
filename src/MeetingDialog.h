#pragma once

#include "AppSettings.h"
#include "MeetingStore.h"
#include "WhisperRunner.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QProcess>
#include <functional>

class QComboBox;
class QCloseEvent;
class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;
class QShowEvent;

class MeetingDialog : public QDialog {
    Q_OBJECT
public:
    MeetingDialog(const AppSettings &settings, WhisperRunner *whisper, std::function<bool()> appIdle, QWidget *parent = nullptr);
    ~MeetingDialog() override;
    void setSettings(const AppSettings &settings);
    bool isBusy() const;
    bool isRecording() const;
    bool canStopRecording() const;
    bool ownsWhisper() const;
    QString activityStatus() const;
    void refresh();
    void stopRecording();

signals:
    void busyChanged();
    void activityChanged();

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    enum class Job { None, Recording, Converting, Whisper };
    void loadSources();
    void startRecording();
    void startTranscription();
    void processFinished(int code, QProcess::ExitStatus status);
    void processError(QProcess::ProcessError error);
    void finishWhisper(const QString &text, const QString &error);
    void setJob(Job job);
    void updateButtons();
    void updateDetails();
    void updateRecordingIndicator();
    void report(const QString &message);
    MeetingEntry selectedEntry() const;
    bool saveEntry(const MeetingEntry &entry);

    AppSettings settings_;
    WhisperRunner *whisper_;
    std::function<bool()> appIdle_;
    QProcess process_;
    Job job_ = Job::None;
    MeetingEntry current_;
    QList<MeetingEntry> entries_;
    QString temporaryWav_;
    QByteArray errors_;
    bool stopping_ = false;
    bool forcedStop_ = false;
    bool shuttingDown_ = false;
    bool sourcesLoaded_ = false;
    QElapsedTimer elapsed_;
    QComboBox *microphone_ = nullptr;
    QComboBox *output_ = nullptr;
    QTableWidget *table_ = nullptr;
    QTableWidget *filesTable_ = nullptr;
    QLabel *recordingIndicator_ = nullptr;
    QLabel *detailsTitle_ = nullptr;
    QLabel *detailsStatus_ = nullptr;
    QLabel *detailsLocation_ = nullptr;
    QLabel *detailsStarted_ = nullptr;
    QLabel *detailsDuration_ = nullptr;
    QLabel *status_ = nullptr;
    QPushButton *record_ = nullptr;
    QPushButton *stop_ = nullptr;
    QPushButton *transcribe_ = nullptr;
    QPushButton *play_ = nullptr;
    QPushButton *open_ = nullptr;
    QPushButton *transcript_ = nullptr;
    QPushButton *copy_ = nullptr;
    QPushButton *rename_ = nullptr;
    QPushButton *remove_ = nullptr;
};
