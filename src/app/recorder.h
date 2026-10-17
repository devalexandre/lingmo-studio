/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Voice-over: records the default PulseAudio/PipeWire source with ffmpeg into
// a WAV next to the project, then the editor places it at the playhead.

#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>

class Recorder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(double seconds READ seconds NOTIFY secondsChanged)

public:
    explicit Recorder(QObject *parent = nullptr);
    ~Recorder() override;

    bool available() const { return m_available; }
    bool recording() const { return m_process != nullptr; }
    double seconds() const { return m_seconds; }

    Q_INVOKABLE bool start(const QString &directory);
    Q_INVOKABLE void stop();

Q_SIGNALS:
    void recordingChanged();
    void secondsChanged();
    void finished(const QString &path);
    void failed(const QString &message);

private:
    bool m_available = false;
    QPointer<QProcess> m_process;
    QString m_path;
    QElapsedTimer m_clock;
    QTimer m_tick;
    double m_seconds = 0;
};
