/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Captions drawn over a viewer by libass (MLT's avfilter.ass), the renderer
// the export burns them in with, so preview and file look the same: the
// Gerar cortes preview (a cut's captions over the source) and the source
// viewer (the media's known speech). The ASS script's PlayRes is the frame
// the captions belong to; libass scales it to the item, so the item must have
// that shape.

#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <memory>

namespace Mlt {
class Profile;
class Producer;
class Filter;
}

// Renders an ASS file on a transparent frame at a time; keeps the MLT
// objects while the file and size stay the same
class CaptionRenderer
{
public:
    CaptionRenderer();
    ~CaptionRenderer();
    QImage render(const QString &assFile, const QSize &size, double ms);

private:
    std::unique_ptr<Mlt::Profile> m_profile;
    std::unique_ptr<Mlt::Producer> m_producer;
    std::unique_ptr<Mlt::Filter> m_filter;
    QString m_file;
    QSize m_size;
};

class CaptionOverlay : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(double time READ time WRITE setTime NOTIFY timeChanged) // ms in the script's time

public:
    explicit CaptionOverlay(QQuickItem *parent = nullptr);
    ~CaptionOverlay() override;

    QString source() const { return m_source; }
    void setSource(const QString &file);
    double time() const { return m_time; }
    void setTime(double ms);
    void paint(QPainter *painter) override;

Q_SIGNALS:
    void sourceChanged();
    void timeChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void refresh();

    QString m_source;
    double m_time = 0;
    QImage m_image;
    CaptionRenderer m_renderer;
};
