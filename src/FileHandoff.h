#pragma once

#include <QObject>
#include <QString>

class FileHandoff : public QObject {
    Q_OBJECT

public:
    explicit FileHandoff(QObject *parent = nullptr);

    bool write(const QString &directory, const QString &text, QString *errorMessage);
};
