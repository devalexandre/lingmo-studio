/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "captionoverlay.h"

#include <Mlt.h>
#include <QFileInfo>
#include <QPainter>
#include <QQuickWindow>
#include <cmath>

// Centiseconds, the resolution of ASS times
static constexpr int Rate = 100;

CaptionRenderer::CaptionRenderer() = default;
CaptionRenderer::~CaptionRenderer() = default;

QImage CaptionRenderer::render(const QString &assFile, const QSize &size, double ms)
{
    const int w = std::max(16, size.width() / 2 * 2), h = std::max(16, size.height() / 2 * 2);
    if (assFile.isEmpty() || !QFileInfo::exists(assFile))
        return {};
    if (assFile != m_file || QSize(w, h) != m_size || !m_producer) {
        m_filter.reset();
        m_producer.reset();
        m_profile = std::make_unique<Mlt::Profile>();
        m_profile->set_width(w);
        m_profile->set_height(h);
        m_profile->set_frame_rate(Rate, 1);
        m_profile->set_sample_aspect(1, 1);
        m_profile->set_display_aspect(w, h);
        m_profile->set_progressive(1);
        m_profile->set_explicit(1);
        m_producer = std::make_unique<Mlt::Producer>(*m_profile, "color:#00000000");
        m_filter = std::make_unique<Mlt::Filter>(*m_profile, "avfilter.ass");
        if (!m_producer->is_valid() || !m_filter->is_valid()) {
            m_filter.reset();
            m_producer.reset();
            return {};
        }
        m_producer->set("length", 0x7fffffff);
        m_producer->set("out", 0x7ffffffe);
        m_filter->set("av.filename", assFile.toUtf8().constData());
        m_filter->set("av.alpha", 1);
        m_producer->attach(*m_filter);
        m_file = assFile;
        m_size = QSize(w, h);
    }
    m_producer->seek(int(std::lround(std::max(0.0, ms) * Rate / 1000.0)));
    std::unique_ptr<Mlt::Frame> frame(m_producer->get_frame());
    if (!frame)
        return {};
    mlt_image_format fmt = mlt_image_rgba;
    int fw = w, fh = h;
    const uint8_t *img = frame->get_image(fmt, fw, fh);
    if (!img || fw != w || fh != h)
        return {};
    return QImage(img, fw, fh, fw * 4, QImage::Format_RGBA8888).convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

CaptionOverlay::CaptionOverlay(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

CaptionOverlay::~CaptionOverlay() = default;

void CaptionOverlay::setSource(const QString &file)
{
    if (file == m_source)
        return;
    m_source = file;
    Q_EMIT sourceChanged();
    refresh();
}

void CaptionOverlay::setTime(double ms)
{
    if (std::abs(ms - m_time) < 0.5)
        return;
    m_time = ms;
    Q_EMIT timeChanged();
    refresh();
}

void CaptionOverlay::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        refresh();
}

void CaptionOverlay::refresh()
{
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const QSize size(int(width() * dpr), int(height() * dpr));
    m_image = isVisible() && size.width() > 16 && size.height() > 16 ? m_renderer.render(m_source, size, m_time) : QImage();
    update();
}

void CaptionOverlay::paint(QPainter *painter)
{
    if (m_image.isNull())
        return;
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawImage(QRectF(0, 0, width(), height()), m_image);
}
