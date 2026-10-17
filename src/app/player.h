/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Playback through MLT. An `sdl2_audio` consumer plays the sound and paces the
// video: on every "consumer-frame-show" event (consumer thread) the frame's
// image, already rendered as RGBA at preview size by MLT's read-ahead thread,
// is copied into a QImage and handed to the GUI thread (VideoItem, scopes).
// Without an audio device (e.g. Xvfb) the SDL dummy driver keeps the clock.

#pragma once

#include "model.h"
#include "plan.h"

#include <Mlt.h>
#include <QImage>
#include <QMutex>
#include <atomic>
#include <QObject>
#include <memory>

namespace Studio {
class MltBuilder;
}

class Player : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int position READ position NOTIFY positionChanged)
    Q_PROPERTY(int duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double speed READ speed NOTIFY playingChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(QString sourceMediaId READ sourceMediaId NOTIFY modeChanged)
    Q_PROPERTY(int sourceIn READ sourceIn WRITE setSourceIn NOTIFY sourceMarksChanged)
    Q_PROPERTY(int sourceOut READ sourceOut WRITE setSourceOut NOTIFY sourceMarksChanged)
    Q_PROPERTY(int markIn READ markIn WRITE setMarkIn NOTIFY marksChanged)
    Q_PROPERTY(int markOut READ markOut WRITE setMarkOut NOTIFY marksChanged)
    Q_PROPERTY(int timelinePosition READ timelinePosition NOTIFY positionChanged)
    Q_PROPERTY(double levelLeft READ levelLeft NOTIFY levelsChanged)
    Q_PROPERTY(double levelRight READ levelRight NOTIFY levelsChanged)
    Q_PROPERTY(int previewHeight READ previewHeight WRITE setPreviewHeight NOTIFY previewHeightChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(int frameSerial READ frameSerial NOTIFY frameReady)

public:
    explicit Player(QObject *parent = nullptr);
    ~Player() override;

    void setProfile(const Studio::ProjectSettings &settings);
    void setPlan(const Studio::RenderPlan &plan);
    // A still of the given plan at a frame (before/after comparisons), off the consumer
    QImage renderStill(const Studio::RenderPlan &plan, int frame, int width);

    int position() const { return m_position; }
    int duration() const { return m_mode == QLatin1String("source") ? m_sourceLength : m_timelineLength; }
    bool playing() const { return m_speed != 0.0; }
    double speed() const { return m_speed; }
    QString mode() const { return m_mode; }
    QString sourceMediaId() const { return m_sourceMediaId; }
    int sourceIn() const { return m_sourceIn; }
    int sourceOut() const { return m_sourceOut; }
    void setSourceIn(int f);
    void setSourceOut(int f);
    int markIn() const { return m_markIn; }
    int markOut() const { return m_markOut; }
    void setMarkIn(int f);
    void setMarkOut(int f);
    int timelinePosition() const { return m_mode == QLatin1String("timeline") ? m_position : m_timelinePosition; }
    double levelLeft() const { return m_levelL; }
    double levelRight() const { return m_levelR; }
    int previewHeight() const { return m_previewHeight; }
    // fraction of the project size the preview renders at
    double previewScale() const;
    void setPreviewHeight(int h);
    double volume() const { return m_volume; }
    void setVolume(double v);
    int frameSerial() const { return m_serial; }
    QImage currentImage() const;

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(int frame);
    Q_INVOKABLE void step(int frames);
    // J/K/L: -1 reverse (repeat = faster), 0 stop, +1 forward
    Q_INVOKABLE void shuttle(int direction);
    Q_INVOKABLE void setMode(const QString &mode);
    Q_INVOKABLE void openSource(const QString &mediaId, const QString &path, int length);
    Q_INVOKABLE void markInHere();
    Q_INVOKABLE void markOutHere();
    Q_INVOKABLE void clearMarks();

Q_SIGNALS:
    void positionChanged();
    void durationChanged();
    void playingChanged();
    void modeChanged();
    void sourceMarksChanged();
    void marksChanged();
    void levelsChanged();
    void previewHeightChanged();
    void volumeChanged();
    void frameReady();

private:
    static void onFrameShow(mlt_properties owner, void *self, mlt_event_data data);
    void deliver(const QImage &image, int position, double levelL, double levelR);
    void createConsumer();
    void connectProducer(Mlt::Producer *producer, int position);
    void refreshFrame();
    void setSpeed(double speed);
    Mlt::Producer *activeProducer() const;
    QSize previewSize() const;

    Studio::ProjectSettings m_settings;
    std::unique_ptr<Mlt::Profile> m_profile;
    std::unique_ptr<Studio::MltBuilder> m_builder;
    std::unique_ptr<Studio::MltBuilder> m_stillBuilder;
    std::unique_ptr<Mlt::Tractor> m_tractor;
    std::unique_ptr<Mlt::Producer> m_source;
    std::unique_ptr<Mlt::FilteredConsumer> m_consumer;
    std::unique_ptr<Mlt::Event> m_event;

    mutable QMutex m_imageLock;
    QImage m_image;
    int m_serial = 0;
    int m_position = 0;
    int m_timelinePosition = 0;
    int m_timelineLength = 1;
    int m_sourceLength = 1;
    int m_sourceIn = -1;
    int m_sourceOut = -1;
    int m_markIn = -1;
    int m_markOut = -1;
    double m_speed = 0.0;
    double m_levelL = 0.0;
    double m_levelR = 0.0;
    double m_volume = 1.0;
    int m_previewHeight = 540;
    std::atomic<int> m_previewW{960};
    std::atomic<int> m_previewH{540};
    QString m_mode = QStringLiteral("timeline");
    QString m_sourceMediaId;
    QString m_sourcePath;
};
