#pragma once

#include "AppSettings.h"
#include "MeetingStore.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QHash>
#include <QProcess>

class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

class MeetingAnalysisDialog : public QDialog {
    Q_OBJECT
public:
    MeetingAnalysisDialog(const MeetingEntry &meeting, const AppSettings &settings, QWidget *parent = nullptr);
    ~MeetingAnalysisDialog() override;

signals:
    void busyChanged(bool busy);
    void analysisSaved();

protected:
    void reject() override;

private:
    void startAnalysis();
    void refreshModels();
    void finishModelList(int exitCode, QProcess::ExitStatus exitStatus);
    void switchProvider();
    void finishAnalysis(int exitCode, QProcess::ExitStatus exitStatus);
    void readErrors(bool final = false);
    void reloadVersions(const QString &select = {});
    void showVersion();
    void updateStatus();
    QByteArray transcriptHash() const;
    QString currentOllamaUrl() const;

    MeetingEntry meeting_;
    AppSettings settings_;
    QProcess process_;
    QProcess modelsProcess_;
    QElapsedTimer elapsed_;
    QByteArray errorPending_;
    QByteArray errors_;
    QByteArray transcriptSnapshot_;
    QByteArray snapshotHash_;
    QString backend_;
    QString model_;
    QString stage_;
    QString reasoningEffort_;
    QString activeBackend_;
    QString modelsBackend_;
    QString activeOllamaUrl_;
    QString modelsEndpoint_;
    QStringList ollamaUrls_;
    QHash<QString, QString> ollamaModels_;
    QString opencodeModel_;
    QString codexModel_;
    bool canceled_ = false;
    bool destroying_ = false;
    bool ignoringModels_ = false;
    bool modelManuallyEdited_ = false;
    QComboBox *backendCombo_ = nullptr;
    QComboBox *modelCombo_ = nullptr;
    QComboBox *reasoningCombo_ = nullptr;
    QWidget *reasoningRow_ = nullptr;
    QLabel *modelStatus_ = nullptr;
    QPushButton *refreshModels_ = nullptr;
    QLabel *providerInfo_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *versionInfo_ = nullptr;
    QListWidget *versions_ = nullptr;
    QPlainTextEdit *preview_ = nullptr;
    QPushButton *analyze_ = nullptr;
    QPushButton *cancel_ = nullptr;
    QPushButton *open_ = nullptr;
    QPushButton *copy_ = nullptr;
};
