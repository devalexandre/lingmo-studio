/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Background render queue: one `melt` process at a time, progress from
// -progress2, cancel = kill. Each job renders a frozen copy of the project
// (written when queued), so editing meanwhile does not change the render.

#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>

class RenderQueue : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    enum Roles { NameRole = Qt::UserRole + 1, OutputRole, PresetRole, StateRole, ProgressRole, EncoderRole, MessageRole, EtaRole };

    struct Job {
        QString name;
        QString output;
        QString preset;
        QString projectFile;
        QStringList args;
        QString encoder;
        QStringList fallbackArgs;   // software encode, tried when a GPU encode fails
        QString fallbackEncoder;
        QString state = QStringLiteral("queued"); // queued | running | done | failed | cancelled
        int progress = 0;
        QString message;
        qint64 etaSeconds = -1;
    };

    explicit RenderQueue(QObject *parent = nullptr);
    ~RenderQueue() override;

    void add(const Job &job);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return m_jobs.size(); }
    bool running() const { return m_current >= 0; }

    Q_INVOKABLE void cancel(int row);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE void openFolder(int row);

Q_SIGNALS:
    void countChanged();
    void runningChanged();
    void jobFinished(const QString &output, bool ok);

private:
    void startNext();
    void finish(int code, QProcess::ExitStatus status);

    QList<Job> m_jobs;
    int m_current = -1;
    QPointer<QProcess> m_process;
    QElapsedTimer m_clock;
    QString m_log;
};
