#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <utility>

// Sampling must never block the GUI, including when nvidia-smi is unavailable.
class GpuMonitor : public QObject {
public:
    explicit GpuMonitor(std::function<void(const QString &)> onSample,
            QString executable = QStringLiteral("nvidia-smi"), int intervalMs = 500)
        : onSample_(std::move(onSample)), executable_(std::move(executable))
    {
        timer_.setInterval(intervalMs);
        timeout_.setSingleShot(true);
        timeout_.setInterval(1500);
        connect(&timer_, &QTimer::timeout, this, [this]() { sample(); });
        connect(&timeout_, &QTimer::timeout, this, [this]() { process_.kill(); });
        connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            timeout_.stop();
            if (!timer_.isActive()) return;
            onSample_(code == 0 && status == QProcess::NormalExit
                            ? formatSample(QString::fromUtf8(process_.readAllStandardOutput())) : QString());
        });
        connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart && timer_.isActive()) {
                timeout_.stop();
                onSample_(QString());
            }
        });
    }

    void start()
    {
        timer_.start();
        sample();
    }

    void stop()
    {
        timer_.stop();
        timeout_.stop();
        if (process_.state() != QProcess::NotRunning) process_.kill();
    }

private:
    void sample()
    {
        if (process_.state() != QProcess::NotRunning) return;
        process_.start(executable_, {QStringLiteral("--query-gpu=utilization.gpu,memory.used,memory.total"),
                QStringLiteral("--format=csv,noheader,nounits")});
        timeout_.start();
    }

    static QString formatSample(const QString &output)
    {
        const QStringList fields = output.section(QLatin1Char('\n'), 0, 0).split(QLatin1Char(','));
        if (fields.size() != 3) return {};
        bool utilizationOk = false, usedOk = false, totalOk = false;
        const int utilization = fields.at(0).trimmed().toInt(&utilizationOk);
        const int used = fields.at(1).trimmed().toInt(&usedOk);
        const int total = fields.at(2).trimmed().toInt(&totalOk);
        if (!usedOk || !totalOk || used < 0 || total <= 0) return {};
        const QString percent = utilizationOk && utilization >= 0 && utilization <= 100
                ? QStringLiteral("%1%").arg(utilization) : QStringLiteral("—");
        return QStringLiteral("GPU %1 · %2/%3 MB").arg(percent).arg(used).arg(total);
    }

    std::function<void(const QString &)> onSample_;
    QString executable_;
    QProcess process_;
    QTimer timer_;
    QTimer timeout_;
};
