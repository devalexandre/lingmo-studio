/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "renderqueue.h"
#include "exporter.h"

#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

RenderQueue::RenderQueue(QObject *parent)
    : QAbstractListModel(parent)
{
}

RenderQueue::~RenderQueue()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

void RenderQueue::add(const Job &job)
{
    beginInsertRows({}, m_jobs.size(), m_jobs.size());
    m_jobs.append(job);
    endInsertRows();
    Q_EMIT countChanged();
    startNext();
}

int RenderQueue::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_jobs.size();
}

QVariant RenderQueue::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_jobs.size())
        return {};
    const Job &j = m_jobs.at(index.row());
    switch (role) {
    case NameRole:
        return j.name;
    case OutputRole:
        return j.output;
    case PresetRole:
        return j.preset;
    case StateRole:
        return j.state;
    case ProgressRole:
        return j.progress;
    case EncoderRole:
        return j.encoder;
    case MessageRole:
        return j.message;
    case EtaRole:
        return j.etaSeconds;
    }
    return {};
}

QHash<int, QByteArray> RenderQueue::roleNames() const
{
    return {{NameRole, "name"},       {OutputRole, "output"},   {PresetRole, "preset"},   {StateRole, "state"},
            {ProgressRole, "progress"}, {EncoderRole, "encoder"}, {MessageRole, "message"}, {EtaRole, "eta"}};
}

void RenderQueue::startNext()
{
    if (m_current >= 0)
        return;
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs.at(i).state != QLatin1String("queued"))
            continue;
        m_current = i;
        Job &j = m_jobs[i];
        j.state = QStringLiteral("running");
        j.progress = 0;
        m_log.clear();
        auto *p = new QProcess(this);
        m_process = p;
        p->setProcessChannelMode(QProcess::MergedChannels);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C")); // MLT/ffmpeg parse numbers with strtod
        env.remove(QStringLiteral("QT_STYLE_OVERRIDE"));
        p->setProcessEnvironment(env);
        connect(p, &QProcess::readyRead, this, [this, p] {
            const QString text = QString::fromUtf8(p->readAll());
            m_log += text;
            if (m_log.size() > 20000)
                m_log = m_log.right(10000);
            int pct = -1;
            for (const QString &line : text.split(QRegularExpression(QStringLiteral("[\\r\\n]")), Qt::SkipEmptyParts)) {
                const int v = Studio::Exporter::parseProgress(line);
                if (v >= 0)
                    pct = v;
            }
            if (pct >= 0 && m_current >= 0) {
                Job &job = m_jobs[m_current];
                if (pct != job.progress) {
                    job.progress = pct;
                    const qint64 ms = m_clock.elapsed();
                    job.etaSeconds = pct > 2 ? ms * (100 - pct) / pct / 1000 : -1;
                    Q_EMIT dataChanged(index(m_current), index(m_current));
                }
            }
        });
        connect(p, &QProcess::finished, this, &RenderQueue::finish);
        connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart)
                finish(-1, QProcess::CrashExit);
            Q_UNUSED(p)
        });
        m_clock.start();
        QFile::remove(j.output);
        p->start(QStringLiteral("melt"), j.args);
        Q_EMIT dataChanged(index(i), index(i));
        Q_EMIT runningChanged();
        return;
    }
}

void RenderQueue::finish(int code, QProcess::ExitStatus status)
{
    if (m_current < 0)
        return;
    Job &j = m_jobs[m_current];
    const bool cancelled = j.state == QLatin1String("cancelled");
    if (!cancelled) {
        const bool ok = status == QProcess::NormalExit && code == 0 && QFileInfo(j.output).size() > 0;
        if (!ok && !j.fallbackArgs.isEmpty()) {
            // the GPU encoder failed (driver, session limit…): render again on the CPU
            j.args = j.fallbackArgs;
            j.fallbackArgs.clear();
            j.message = tr("%1 failed; encoding with %2").arg(j.encoder, j.fallbackEncoder);
            j.encoder = j.fallbackEncoder;
            j.state = QStringLiteral("queued");
            j.progress = 0;
            if (m_process)
                m_process->deleteLater();
            const int row = m_current;
            m_current = -1;
            Q_EMIT dataChanged(index(row), index(row));
            startNext();
            return;
        }
        j.state = ok ? QStringLiteral("done") : QStringLiteral("failed");
        j.progress = ok ? 100 : j.progress;
        j.etaSeconds = -1;
        if (!ok) {
            const QStringList lines = m_log.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            QString last;
            for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
                if (!it->contains(QStringLiteral("percentage:")) && !it->contains(QStringLiteral("Duplicate"))) {
                    last = it->trimmed();
                    break;
                }
            }
            j.message = last.isEmpty() ? tr("melt could not render the project") : last;
        }
        Q_EMIT jobFinished(j.output, ok);
    } else {
        QFile::remove(j.output);
    }
    QFile::remove(j.projectFile);
    if (m_process)
        m_process->deleteLater();
    const int row = m_current;
    m_current = -1;
    Q_EMIT dataChanged(index(row), index(row));
    Q_EMIT runningChanged();
    startNext();
}

void RenderQueue::cancel(int row)
{
    if (row < 0 || row >= m_jobs.size())
        return;
    Job &j = m_jobs[row];
    if (j.state == QLatin1String("queued")) {
        j.state = QStringLiteral("cancelled");
        QFile::remove(j.projectFile);
        Q_EMIT dataChanged(index(row), index(row));
    } else if (j.state == QLatin1String("running") && m_process) {
        j.state = QStringLiteral("cancelled");
        m_process->kill();
        Q_EMIT dataChanged(index(row), index(row));
    }
}

void RenderQueue::remove(int row)
{
    if (row < 0 || row >= m_jobs.size() || row == m_current)
        return;
    beginRemoveRows({}, row, row);
    m_jobs.removeAt(row);
    endRemoveRows();
    if (m_current > row)
        --m_current;
    Q_EMIT countChanged();
}

void RenderQueue::clearFinished()
{
    for (int i = m_jobs.size() - 1; i >= 0; --i)
        if (m_jobs.at(i).state != QLatin1String("running") && m_jobs.at(i).state != QLatin1String("queued"))
            remove(i);
}

void RenderQueue::openFolder(int row)
{
    if (row < 0 || row >= m_jobs.size())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_jobs.at(row).output).absolutePath()));
}
