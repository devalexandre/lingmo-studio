/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "player.h"
#include "mltbuilder.h"

#include <QDebug>
#include <QMetaObject>
#include <cmath>

using namespace Studio;

Player::Player(QObject *parent)
    : QObject(parent)
{
    initMlt();
}

Player::~Player()
{
    if (m_consumer) {
        m_event.reset();
        m_consumer->stop();
    }
}

double Player::previewScale() const
{
    // the short side of the frame decides (vertical projects too)
    const int shortSide = std::min(m_settings.width, m_settings.height);
    return shortSide > 0 ? std::min(1.0, double(m_previewHeight) / shortSide) : 1.0;
}

// The preview renders with an MLT profile scaled down to the preview size
// (not a scaled consumer: MLT's text filters misplace text then).
QSize Player::previewSize() const
{
    const double s = previewScale();
    if (s >= 1.0)
        return {m_settings.width, m_settings.height};
    return {std::max(2, int(std::lround(m_settings.width * s / 2.0)) * 2), std::max(2, int(std::lround(m_settings.height * s / 2.0)) * 2)};
}

void Player::setProfile(const ProjectSettings &settings)
{
    if (m_consumer) {
        m_event.reset();
        m_consumer->stop();
    }
    m_consumer.reset();
    m_tractor.reset();
    m_source.reset();
    m_builder.reset();
    m_stillBuilder.reset();
    m_fullBuilder.reset();
    m_fullProfile.reset();
    m_fullScale = 0;
    m_settings = settings;
    const QSize size = previewSize();
    m_profile = std::make_unique<Mlt::Profile>();
    m_profile->set_width(size.width());
    m_profile->set_height(size.height());
    m_profile->set_frame_rate(settings.fpsNum, settings.fpsDen);
    m_profile->set_sample_aspect(1, 1);
    m_profile->set_display_aspect(size.width(), size.height());
    m_profile->set_progressive(1);
    m_profile->set_colorspace(709);
    m_profile->set_explicit(1);
    m_builder = std::make_unique<MltBuilder>(*m_profile);
    m_stillBuilder = std::make_unique<MltBuilder>(*m_profile);
    m_speed = 0;
    m_mode = QStringLiteral("timeline");
    m_sourceMediaId.clear();
    createConsumer();
    Q_EMIT modeChanged();
    Q_EMIT playingChanged();
}

void Player::createConsumer()
{
    m_consumer = std::make_unique<Mlt::FilteredConsumer>(*m_profile, "sdl2_audio");
    if (!m_consumer->is_valid()) {
        qWarning() << "lingmo-studio: no sdl2_audio consumer, playback is silent";
        m_consumer = std::make_unique<Mlt::FilteredConsumer>(*m_profile, "null");
    }
    const QSize size = previewSize();
    m_previewW = size.width();
    m_previewH = size.height();
    m_consumer->set("mlt_image_format", "rgba");
    m_consumer->set("width", size.width());
    m_consumer->set("height", size.height());
    m_consumer->set("progressive", 1);
    m_consumer->set("rescale", "bilinear");
    m_consumer->set("deinterlacer", "onefield");
    m_consumer->set("real_time", 1);
    m_consumer->set("terminate_on_pause", 0);
    m_consumer->set("scrub_audio", 1);
    m_consumer->set("volume", m_volume);
    m_consumer->set("frequency", m_settings.sampleRate);
    m_consumer->set("channels", m_settings.channels);
    m_consumer->set("audio_buffer", 1024);
    m_event.reset(m_consumer->listen("consumer-frame-show", this, reinterpret_cast<mlt_listener>(onFrameShow)));
}

void Player::onFrameShow(mlt_properties, void *self, mlt_event_data data)
{
    auto *player = static_cast<Player *>(self);
    mlt_frame f = mlt_event_data_to_frame(data);
    if (!f)
        return;
    Mlt::Frame frame(f);
    const int position = frame.get_position();
    mlt_image_format fmt = mlt_image_rgba;
    int w = player->m_previewW;
    int h = player->m_previewH;
    const uint8_t *img = frame.get_image(fmt, w, h);
    QImage image;
    if (img && fmt == mlt_image_rgba && w > 0 && h > 0)
        image = QImage(img, w, h, QImage::Format_RGBA8888).copy();
    // Master meter: peak of this frame's audio (already decoded for playback,
    // so this returns MLT's cached buffer), IEC-like scale over 60 dB
    double l = 0, r = 0;
    {
        mlt_audio_format afmt = mlt_audio_s16;
        int freq = player->m_settings.sampleRate;
        int channels = player->m_settings.channels;
        int samples = mlt_audio_calculate_frame_samples(float(player->m_settings.fps()), freq, position);
        // returns the buffer already decoded for playback, in whatever format it has
        const void *buf = frame.get_audio(afmt, freq, channels, samples);
        double peak[2] = {0, 0};
        if (buf && channels > 0 && samples > 0) {
            for (int i = 0; i < samples; ++i) {
                for (int c = 0; c < std::min(channels, 2); ++c) {
                    double v = 0;
                    if (afmt == mlt_audio_s16)
                        v = static_cast<const int16_t *>(buf)[i * channels + c] / 32768.0;
                    else if (afmt == mlt_audio_f32le)
                        v = static_cast<const float *>(buf)[i * channels + c];
                    else if (afmt == mlt_audio_float) // planar
                        v = static_cast<const float *>(buf)[c * samples + i];
                    else if (afmt == mlt_audio_s32le)
                        v = static_cast<const int32_t *>(buf)[i * channels + c] / 2147483648.0;
                    peak[c] = std::max(peak[c], std::abs(v));
                }
            }
            if (channels == 1)
                peak[1] = peak[0];
        }
        auto scale = [](double p) {
            if (p <= 1e-6)
                return 0.0;
            return std::clamp((20.0 * std::log10(p) + 60.0) / 60.0, 0.0, 1.0); // 0 = -60 dBFS
        };
        l = scale(peak[0]);
        r = scale(peak[1]);
    }
    QMetaObject::invokeMethod(player, [player, image, position, l, r] { player->deliver(image, position, l, r); }, Qt::QueuedConnection);
}

void Player::deliver(const QImage &image, int position, double levelL, double levelR)
{
    if (!image.isNull()) {
        QMutexLocker lock(&m_imageLock);
        m_image = image;
    }
    ++m_serial;
    if (m_speed != 0.0 || position != m_position) {
        m_position = position;
        if (m_mode == QLatin1String("timeline"))
            m_timelinePosition = position;
        Q_EMIT positionChanged();
    }
    if (std::abs(levelL - m_levelL) > 0.005 || std::abs(levelR - m_levelR) > 0.005) {
        m_levelL = levelL;
        m_levelR = levelR;
        Q_EMIT levelsChanged();
    }
    Q_EMIT frameReady();
    const int end = (m_markOut >= 0 && m_mode == QLatin1String("timeline")) ? duration() - 1 : duration() - 1;
    if ((m_speed > 0 && position >= end) || (m_speed < 0 && position <= 0))
        pause();
}

QImage Player::currentImage() const
{
    QMutexLocker lock(&m_imageLock);
    return m_image;
}

Mlt::Producer *Player::activeProducer() const
{
    if (m_mode == QLatin1String("source"))
        return m_source.get();
    return m_tractor.get();
}

void Player::connectProducer(Mlt::Producer *producer, int position)
{
    if (!m_consumer || !producer)
        return;
    m_consumer->stop();
    m_consumer->purge();
    producer->set_speed(m_speed);
    producer->seek(std::clamp(position, 0, std::max(0, producer->get_length() - 1)));
    m_consumer->connect(*producer);
    m_consumer->start();
    m_consumer->set("refresh", 1);
}

void Player::setPlan(const RenderPlan &plan)
{
    if (!m_builder)
        return;
    const bool active = m_mode == QLatin1String("timeline");
    if (active && m_consumer)
        m_consumer->stop(); // the builder reuses producers the consumer reads
    std::unique_ptr<Mlt::Tractor> tractor = m_builder->build(plan);
    m_builder->prune();
    m_tractor = std::move(tractor);
    if (m_timelineLength != plan.length) {
        m_timelineLength = plan.length;
        Q_EMIT durationChanged();
    }
    if (active) {
        m_position = std::clamp(m_position, 0, m_timelineLength - 1);
        connectProducer(m_tractor.get(), m_position);
        Q_EMIT positionChanged();
    }
}

QImage Player::renderStill(const RenderPlan &plan, int frame, int width)
{
    if (!m_stillBuilder)
        return {};
    std::unique_ptr<Mlt::Tractor> t = m_stillBuilder->build(plan);
    if (!t)
        return {};
    t->seek(frame);
    std::unique_ptr<Mlt::Frame> f(t->get_frame());
    if (!f)
        return {};
    mlt_image_format fmt = mlt_image_rgba;
    int w = width - width % 2;
    int h = int(std::lround(double(w) * m_settings.height / m_settings.width));
    h -= h % 2;
    f->set("consumer.rescale", "bilinear");
    const uint8_t *img = f->get_image(fmt, w, h);
    if (!img || fmt != mlt_image_rgba)
        return {};
    return QImage(img, w, h, QImage::Format_RGBA8888).copy();
}

QImage Player::renderFullStill(const RenderPlan &plan, int frame, double scale)
{
    scale = std::clamp(scale, 0.05, 1.0);
    if (!m_fullBuilder || std::abs(m_fullScale - scale) > 1e-6) {
        m_fullBuilder.reset();
        const int w = std::max(2, int(std::lround(m_settings.width * scale / 2.0)) * 2);
        const int h = std::max(2, int(std::lround(m_settings.height * scale / 2.0)) * 2);
        m_fullProfile = std::make_unique<Mlt::Profile>();
        m_fullProfile->set_width(w);
        m_fullProfile->set_height(h);
        m_fullProfile->set_frame_rate(m_settings.fpsNum, m_settings.fpsDen);
        m_fullProfile->set_sample_aspect(1, 1);
        m_fullProfile->set_display_aspect(w, h);
        m_fullProfile->set_progressive(1);
        m_fullProfile->set_colorspace(709);
        m_fullProfile->set_explicit(1);
        m_fullBuilder = std::make_unique<MltBuilder>(*m_fullProfile);
        m_fullScale = scale;
    }
    std::unique_ptr<Mlt::Tractor> t = m_fullBuilder->build(plan);
    if (!t)
        return {};
    t->seek(frame);
    std::unique_ptr<Mlt::Frame> f(t->get_frame());
    if (!f)
        return {};
    mlt_image_format fmt = mlt_image_rgba;
    int w = m_fullProfile->width(), h = m_fullProfile->height();
    f->set("consumer.rescale", "bilinear");
    const uint8_t *img = f->get_image(fmt, w, h);
    if (!img || fmt != mlt_image_rgba)
        return {};
    return QImage(img, w, h, QImage::Format_RGBA8888).copy();
}

void Player::setSpeed(double speed)
{
    Mlt::Producer *p = activeProducer();
    if (!p || !m_consumer)
        return;
    if (speed == m_speed)
        return;
    m_speed = speed;
    if (speed == 0.0) {
        p->set_speed(0);
        p->seek(m_position);
        m_consumer->purge();
        m_consumer->set("refresh", 1);
    } else {
        m_consumer->purge();
        p->set_speed(speed);
        if (m_consumer->is_stopped())
            m_consumer->start();
        m_consumer->set("refresh", 1);
    }
    Q_EMIT playingChanged();
}

void Player::play()
{
    Mlt::Producer *p = activeProducer();
    if (!p)
        return;
    if (m_position >= duration() - 1) {
        p->seek(0);
        m_position = 0;
    }
    setSpeed(1.0);
}

void Player::pause()
{
    setSpeed(0.0);
}

void Player::togglePlay()
{
    if (playing())
        pause();
    else
        play();
}

void Player::seek(int frame)
{
    Mlt::Producer *p = activeProducer();
    if (!p || !m_consumer)
        return;
    frame = std::clamp(frame, 0, std::max(0, duration() - 1));
    p->seek(frame);
    if (m_speed != 0.0)
        m_consumer->purge();
    m_consumer->set("refresh", 1);
    if (frame != m_position) {
        m_position = frame;
        if (m_mode == QLatin1String("timeline"))
            m_timelinePosition = frame;
        Q_EMIT positionChanged();
    }
}

void Player::step(int frames)
{
    pause();
    seek(m_position + frames);
}

void Player::shuttle(int direction)
{
    if (direction == 0) {
        pause();
        return;
    }
    double s = direction;
    if ((m_speed > 0 && direction > 0) || (m_speed < 0 && direction < 0))
        s = std::clamp(m_speed * 2, -16.0, 16.0);
    setSpeed(s);
}

void Player::setMode(const QString &mode)
{
    if (mode == m_mode || (mode == QLatin1String("source") && !m_source))
        return;
    pause();
    int pos;
    if (mode == QLatin1String("source")) {
        m_timelinePosition = m_position;
        pos = std::max(0, m_sourceIn);
        m_mode = mode;
        connectProducer(m_source.get(), pos);
    } else {
        m_mode = QStringLiteral("timeline");
        pos = m_timelinePosition;
        connectProducer(m_tractor.get(), pos);
    }
    m_position = pos;
    Q_EMIT modeChanged();
    Q_EMIT durationChanged();
    Q_EMIT positionChanged();
}

void Player::openSource(const QString &mediaId, const QString &path, int length)
{
    if (!m_profile)
        return;
    pause();
    if (m_mode == QLatin1String("source") && m_consumer)
        m_consumer->stop();
    auto p = std::make_unique<Mlt::Producer>(*m_profile, path.toUtf8().constData());
    if (!p->is_valid())
        return;
    m_source = std::move(p);
    m_sourceMediaId = mediaId;
    m_sourcePath = path;
    m_sourceLength = std::max(1, length);
    m_sourceIn = -1;
    m_sourceOut = -1;
    Q_EMIT sourceMarksChanged();
    if (m_mode == QLatin1String("source")) {
        m_position = 0;
        connectProducer(m_source.get(), 0);
        Q_EMIT durationChanged();
        Q_EMIT positionChanged();
        Q_EMIT modeChanged();
    } else {
        setMode(QStringLiteral("source"));
    }
}

void Player::setSourceIn(int f)
{
    if (f == m_sourceIn)
        return;
    m_sourceIn = f;
    if (m_sourceOut >= 0 && m_sourceIn > m_sourceOut)
        m_sourceOut = -1;
    Q_EMIT sourceMarksChanged();
}

void Player::setSourceOut(int f)
{
    if (f == m_sourceOut)
        return;
    m_sourceOut = f;
    if (m_sourceIn >= 0 && m_sourceOut >= 0 && m_sourceOut < m_sourceIn)
        m_sourceIn = -1;
    Q_EMIT sourceMarksChanged();
}

void Player::setMarkIn(int f)
{
    if (f == m_markIn)
        return;
    m_markIn = f;
    Q_EMIT marksChanged();
}

void Player::setMarkOut(int f)
{
    if (f == m_markOut)
        return;
    m_markOut = f;
    Q_EMIT marksChanged();
}

void Player::markInHere()
{
    if (m_mode == QLatin1String("source"))
        setSourceIn(m_position);
    else
        setMarkIn(m_position);
}

void Player::markOutHere()
{
    if (m_mode == QLatin1String("source"))
        setSourceOut(m_position);
    else
        setMarkOut(m_position);
}

void Player::clearMarks()
{
    if (m_mode == QLatin1String("source")) {
        setSourceIn(-1);
        setSourceOut(-1);
    } else {
        setMarkIn(-1);
        setMarkOut(-1);
    }
}

void Player::setPreviewHeight(int h)
{
    if (h == m_previewHeight)
        return;
    const int position = m_position;
    m_previewHeight = h;
    if (m_profile) {
        setProfile(m_settings); // new profile size; the editor rebuilds the timeline
        m_position = position;
    }
    Q_EMIT previewHeightChanged();
}

void Player::setVolume(double v)
{
    v = std::clamp(v, 0.0, 1.0);
    if (v == m_volume)
        return;
    m_volume = v;
    if (m_consumer)
        m_consumer->set("volume", v);
    Q_EMIT volumeChanged();
}

void Player::refreshFrame()
{
    if (m_consumer)
        m_consumer->set("refresh", 1);
}
