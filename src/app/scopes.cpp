/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "scopes.h"
#include "player.h"

#include <QMutex>
#include <QPainter>
#include <QtConcurrent>
#include <cmath>

static QMutex s_lock;
static Scopes::Result s_latest;

Scopes::Scopes(Player *player, QObject *parent)
    : QObject(parent)
    , m_player(player)
{
    m_throttle.setInterval(100);
    m_throttle.setSingleShot(true);
    connect(&m_throttle, &QTimer::timeout, this, &Scopes::run);
    connect(player, &Player::frameReady, this, &Scopes::frameArrived);
    connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this] {
        {
            QMutexLocker l(&s_lock);
            s_latest = m_watcher.result();
        }
        ++m_serial;
        Q_EMIT updated();
        if (m_pending)
            m_throttle.start();
    });
}

void Scopes::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    Q_EMIT enabledChanged();
    if (on)
        frameArrived();
}

void Scopes::frameArrived()
{
    if (!m_enabled)
        return;
    m_pending = true;
    if (!m_throttle.isActive() && !m_watcher.isRunning())
        m_throttle.start();
}

void Scopes::run()
{
    if (m_watcher.isRunning())
        return;
    m_pending = false;
    const QImage frame = m_player->currentImage();
    if (frame.isNull())
        return;
    m_watcher.setFuture(QtConcurrent::run(&Scopes::analyze, frame));
}

QImage Scopes::latest(const QString &kind)
{
    QMutexLocker l(&s_lock);
    if (kind == QLatin1String("vectorscope"))
        return s_latest.vectorscope;
    if (kind == QLatin1String("histogram"))
        return s_latest.histogram;
    if (kind == QLatin1String("parade"))
        return s_latest.parade;
    return s_latest.waveform;
}

// Accumulation buffer -> glowing image (log scale, like a phosphor trace)
static QImage toImage(const QList<quint32> &acc, int w, int h, const QColor &tint, quint32 maxv)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    const double norm = maxv > 0 ? 1.0 / std::log1p(double(maxv)) : 0;
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const quint32 v = acc.at(y * w + x);
            if (!v)
                continue;
            const double a = std::clamp(0.25 + std::log1p(double(v)) * norm * 0.75, 0.0, 1.0);
            line[x] = qPremultiply(qRgba(tint.red(), tint.green(), tint.blue(), int(a * 255)));
        }
    }
    return img;
}

static void graticule(QImage &img, bool vertical)
{
    QPainter p(&img);
    p.setPen(QColor(255, 255, 255, 40));
    for (int i = 0; i <= 4; ++i) {
        if (vertical) {
            const int y = int((img.height() - 1) * i / 4.0);
            p.drawLine(0, y, img.width(), y);
        } else {
            const int x = int((img.width() - 1) * i / 4.0);
            p.drawLine(x, 0, x, img.height());
        }
    }
}

Scopes::Result Scopes::analyze(const QImage &input)
{
    Result r;
    const QImage frame = input.width() > 360 ? input.scaledToWidth(360, Qt::FastTransformation).convertToFormat(QImage::Format_RGBA8888)
                                             : input.convertToFormat(QImage::Format_RGBA8888);
    const int fw = frame.width(), fh = frame.height();

    // Luma waveform: x = image column, y = Y' (BT.709)
    const int ww = fw, wh = 256;
    QList<quint32> wave(ww * wh, 0);
    // RGB parade: three columns side by side
    const int pw = fw * 3, ph = 256;
    QList<quint32> pr(pw * ph, 0), pg(pw * ph, 0), pb(pw * ph, 0);
    // Vectorscope: Cb/Cr plane
    const int vs = 256;
    QList<quint32> vec(vs * vs, 0);
    quint32 hist[3][256] = {};
    quint32 waveMax = 0, vecMax = 0, paradeMax = 0;

    for (int y = 0; y < fh; ++y) {
        const uchar *line = frame.constScanLine(y);
        for (int x = 0; x < fw; ++x) {
            const int R = line[x * 4], G = line[x * 4 + 1], B = line[x * 4 + 2];
            const double Y = 0.2126 * R + 0.7152 * G + 0.0722 * B;
            const double Cb = (B - Y) / 1.8556;
            const double Cr = (R - Y) / 1.5748;
            const int yi = 255 - std::clamp(int(Y), 0, 255);
            waveMax = std::max(waveMax, ++wave[yi * ww + x]);
            const int vx = std::clamp(int(128 + Cb), 0, 255);
            const int vy = std::clamp(int(128 - Cr), 0, 255);
            vecMax = std::max(vecMax, ++vec[vy * vs + vx]);
            ++hist[0][R];
            ++hist[1][G];
            ++hist[2][B];
            paradeMax = std::max(paradeMax, ++pr[(255 - R) * pw + x]);
            ++pg[(255 - G) * pw + fw + x];
            ++pb[(255 - B) * pw + 2 * fw + x];
        }
    }
    r.waveform = toImage(wave, ww, wh, QColor(140, 255, 160), waveMax);
    graticule(r.waveform, true);

    QImage par(pw, ph, QImage::Format_ARGB32_Premultiplied);
    par.fill(Qt::transparent);
    {
        QPainter p(&par);
        p.drawImage(0, 0, toImage(pr, pw, ph, QColor(255, 80, 80), paradeMax));
        p.drawImage(0, 0, toImage(pg, pw, ph, QColor(80, 255, 80), paradeMax));
        p.drawImage(0, 0, toImage(pb, pw, ph, QColor(90, 140, 255), paradeMax));
    }
    graticule(par, true);
    r.parade = par;

    r.vectorscope = toImage(vec, vs, vs, QColor(170, 255, 200), vecMax);
    {
        QPainter p(&r.vectorscope);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QColor(255, 255, 255, 50));
        p.drawEllipse(QPointF(128, 128), 120, 120);
        p.drawLine(128, 8, 128, 248);
        p.drawLine(8, 128, 248, 128);
        // 75% colour bar targets (R, Y, G, C, B, M)
        const QColor targets[] = {QColor(191, 0, 0), QColor(191, 191, 0), QColor(0, 191, 0), QColor(0, 191, 191), QColor(0, 0, 191), QColor(191, 0, 191)};
        for (const QColor &c : targets) {
            const double Y = 0.2126 * c.red() + 0.7152 * c.green() + 0.0722 * c.blue();
            const double cx = 128 + (c.blue() - Y) / 1.8556;
            const double cy = 128 - (c.red() - Y) / 1.5748;
            p.setPen(QColor(c.red(), c.green(), c.blue(), 200).lighter(150));
            p.drawRect(QRectF(cx - 4, cy - 4, 8, 8));
        }
        // skin tone line
        p.setPen(QColor(255, 200, 150, 70));
        p.drawLine(QPointF(128, 128), QPointF(128 - 70, 128 - 105));
    }

    const int hw = 256, hh = 128;
    QImage hist2(hw, hh, QImage::Format_ARGB32_Premultiplied);
    hist2.fill(Qt::transparent);
    quint32 hmax = 1;
    for (int c = 0; c < 3; ++c)
        for (int i = 1; i < 255; ++i)
            hmax = std::max(hmax, hist[c][i]);
    {
        QPainter p(&hist2);
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        const QColor colors[] = {QColor(255, 60, 60, 150), QColor(60, 255, 60, 150), QColor(70, 120, 255, 150)};
        for (int c = 0; c < 3; ++c) {
            QPolygonF poly;
            poly << QPointF(0, hh);
            for (int i = 0; i < 256; ++i)
                poly << QPointF(i, hh - std::min(1.0, double(hist[c][i]) / hmax) * (hh - 2));
            poly << QPointF(255, hh);
            p.setPen(Qt::NoPen);
            p.setBrush(colors[c]);
            p.drawPolygon(poly);
        }
    }
    graticule(hist2, false);
    r.histogram = hist2;
    return r;
}
