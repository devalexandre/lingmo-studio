/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "recorder.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

Recorder::Recorder(QObject *parent)
    : QObject(parent)
{
    // ffmpeg needs its PulseAudio input (PipeWire serves it too)
    if (!QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
        QProcess p;
        p.start(QStringLiteral("ffmpeg"), {QStringLiteral("-hide_banner"), QStringLiteral("-devices")});
        if (p.waitForFinished(5000))
            m_available = QString::fromUtf8(p.readAll()).contains(QStringLiteral(" pulse "));
    }
    m_tick.setInterval(200);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        m_seconds = m_clock.elapsed() / 1000.0;
        Q_EMIT secondsChanged();
    });
}

Recorder::~Recorder()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

bool Recorder::start(const QString &directory)
{
    if (!m_available || m_process)
        return false;
    const QString dir = directory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + QStringLiteral("/Lingmo Studio")
        : directory;
    QDir().mkpath(dir);
    m_path = QDir(dir).filePath(QStringLiteral("voice-over-%1.wav").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    auto *p = new QProcess(this);
    m_process = p;
    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus) {
        p->deleteLater();
        m_process = nullptr;
        m_tick.stop();
        Q_EMIT recordingChanged();
        if (QFileInfo(m_path).size() > 1024)
            Q_EMIT finished(m_path);
        else
            Q_EMIT failed(code == 0 ? tr("Nothing was recorded") : QString::fromUtf8(p->readAllStandardError()).trimmed().section(QLatin1Char('\n'), -1));
    });
    p->start(QStringLiteral("ffmpeg"), {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
                                        QStringLiteral("-f"), QStringLiteral("pulse"), QStringLiteral("-i"), QStringLiteral("default"),
                                        QStringLiteral("-ac"), QStringLiteral("1"), QStringLiteral("-ar"), QStringLiteral("48000"), m_path});
    m_clock.start();
    m_seconds = 0;
    m_tick.start();
    Q_EMIT recordingChanged();
    return true;
}

void Recorder::stop()
{
    if (!m_process)
        return;
    m_process->write("q");
    m_process->closeWriteChannel();
    if (!m_process->waitForFinished(3000))
        m_process->terminate();
}
