/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Video scopes computed from the preview frames: luma waveform, vectorscope
// (Cb/Cr, BT.709) and RGB histogram. Analysis runs off the GUI thread, at most
// ~10 times a second, and only while a page shows scopes (enabled).

#pragma once

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QTimer>

class Player;

class Scopes : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int serial READ serial NOTIFY updated)

public:
    explicit Scopes(Player *player, QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    int serial() const { return m_serial; }

    struct Result {
        QImage waveform;
        QImage vectorscope;
        QImage histogram;
        QImage parade;
    };
    static Result analyze(const QImage &frame);
    static QImage latest(const QString &kind);

Q_SIGNALS:
    void enabledChanged();
    void updated();

private:
    void frameArrived();
    void run();

    Player *m_player;
    bool m_enabled = false;
    bool m_pending = false;
    int m_serial = 0;
    QTimer m_throttle;
    QFutureWatcher<Result> m_watcher;
};
