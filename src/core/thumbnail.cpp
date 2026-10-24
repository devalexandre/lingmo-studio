/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "thumbnail.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

namespace Studio::Thumbnail {

QStringList styleIds()
{
    return {QStringLiteral("bold"), QStringLiteral("banner"), QStringLiteral("center")};
}

double frameScore(const QImage &frame)
{
    if (frame.isNull())
        return 0;
    const QImage s = frame.scaledToWidth(160, Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
    const int w = s.width(), h = s.height();
    if (w < 2 || h < 2)
        return 0;
    QList<float> lum(size_t(w) * h);
    double sum = 0, sumSq = 0, sat = 0;
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(s.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const int r = qRed(line[x]), g = qGreen(line[x]), b = qBlue(line[x]);
            const float l = float((0.299 * r + 0.587 * g + 0.114 * b) / 255.0);
            lum[y * w + x] = l;
            sum += l;
            sumSq += double(l) * l;
            sat += (std::max({r, g, b}) - std::min({r, g, b})) / 255.0;
        }
    }
    const double n = double(w) * h;
    const double mean = sum / n;
    const double stddev = std::sqrt(std::max(0.0, sumSq / n - mean * mean));
    double edges = 0;
    for (int y = 0; y < h - 1; ++y)
        for (int x = 0; x < w - 1; ++x) {
            const float l = lum[y * w + x];
            edges += std::abs(l - lum[y * w + x + 1]) + std::abs(l - lum[(y + 1) * w + x]);
        }
    edges /= n;

    const double sharp = std::min(1.0, edges / 0.12);
    const double contrast = std::min(1.0, stddev / 0.25);
    const double colour = std::min(1.0, sat / n / 0.45);
    const double light = 1.0 - std::min(1.0, std::abs(mean - 0.45) / 0.45);
    double score = 0.35 * sharp + 0.25 * contrast + 0.2 * colour + 0.2 * light;
    if (stddev < 0.04)
        score *= 0.25; // flat: a fade or a blank
    if (mean < 0.08 || mean > 0.93)
        score *= 0.2; // black or white
    return std::clamp(score, 0.0, 1.0);
}

QList<int> pickDiverse(const QList<double> &scores, int count, int minGap)
{
    QList<int> order;
    for (int i = 0; i < scores.size(); ++i)
        order << i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return scores[a] > scores[b]; });
    QList<int> out;
    for (int i : order) {
        bool far = true;
        for (int j : out)
            far &= std::abs(i - j) >= minGap;
        if (far)
            out << i;
        if (out.size() >= count)
            break;
    }
    return out;
}

QStringList wrapText(const QString &text, const QFont &font, int maxWidth, int maxLines)
{
    const QFontMetrics fm(font);
    QStringList lines;
    QString line;
    for (const QString &word : text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const QString tryLine = line.isEmpty() ? word : line + QLatin1Char(' ') + word;
        if (line.isEmpty() || fm.horizontalAdvance(tryLine) <= maxWidth) {
            line = tryLine;
        } else {
            lines << line;
            line = word;
        }
    }
    if (!line.isEmpty())
        lines << line;
    return lines.size() > maxLines ? QStringList() : lines;
}

static QImage cover(const QImage &frame, const QSize &size)
{
    QImage out(size, QImage::Format_RGB32);
    if (frame.isNull()) {
        QPainter p(&out);
        QLinearGradient g(0, 0, 0, size.height());
        g.setColorAt(0, QColor(0x2a, 0x2f, 0x45));
        g.setColorAt(1, QColor(0x0d, 0x0f, 0x1a));
        p.fillRect(out.rect(), g);
        return out;
    }
    const QImage scaled = frame.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPainter p(&out);
    p.drawImage((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2, scaled);
    return out;
}

QImage compose(const QImage &frame, const Options &o)
{
    const QSize size = o.size.isValid() ? o.size : QSize(1280, 720);
    QImage out = cover(frame, size);
    const int W = size.width(), H = size.height();
    const bool vertical = H > W;
    const QString text = o.text.simplified().toUpper();

    QPainter p(&out);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);

    // ---- shading and the box the headline lives in
    QRectF box;
    Qt::Alignment align = Qt::AlignCenter;
    if (o.style == QLatin1String("banner")) {
        const double bandH = H * (vertical ? 0.24 : 0.34);
        const QRectF band(0, H - bandH, W, bandH);
        QLinearGradient fade(0, band.top() - H * 0.12, 0, band.top());
        fade.setColorAt(0, QColor(0, 0, 0, 0));
        fade.setColorAt(1, QColor(0, 0, 0, 120));
        p.fillRect(QRectF(0, band.top() - H * 0.12, W, H * 0.12), fade);
        p.fillRect(band, QColor(8, 8, 12, 215));
        p.fillRect(QRectF(0, band.top(), W, std::max(6.0, H * 0.012)), o.accent);
        box = band.adjusted(W * 0.05, bandH * 0.1, -W * 0.05, -bandH * 0.08);
    } else if (o.style == QLatin1String("center")) {
        QRadialGradient v(W / 2.0, H / 2.0, std::hypot(W, H) * 0.55);
        v.setColorAt(0, QColor(0, 0, 0, 110));
        v.setColorAt(1, QColor(0, 0, 0, 200));
        p.fillRect(out.rect(), v);
        box = QRectF(W * 0.08, H * 0.18, W * 0.84, H * 0.64);
    } else { // bold
        if (vertical) {
            QLinearGradient g(0, H, 0, H * 0.35);
            g.setColorAt(0, QColor(0, 0, 0, 225));
            g.setColorAt(1, QColor(0, 0, 0, 0));
            p.fillRect(out.rect(), g);
            box = QRectF(W * 0.07, H * 0.55, W * 0.86, H * 0.36);
        } else {
            QLinearGradient g(0, 0, W * 0.78, 0);
            g.setColorAt(0, QColor(0, 0, 0, 225));
            g.setColorAt(0.55, QColor(0, 0, 0, 150));
            g.setColorAt(1, QColor(0, 0, 0, 0));
            p.fillRect(out.rect(), g);
            box = QRectF(W * 0.05, H * 0.12, W * 0.60, H * 0.76);
        }
        align = Qt::AlignLeft | Qt::AlignVCenter;
    }
    if (text.isEmpty())
        return out;

    // ---- the largest size at which the headline fits the box
    QFont font(QStringLiteral("Sans Serif"));
    font.setWeight(QFont::Black);
    font.setStyleStrategy(QFont::PreferAntialias);
    const int maxLines = o.style == QLatin1String("banner") ? 2 : 4;
    QStringList lines;
    int px = int(std::min(box.height() * 0.62, W * 0.2));
    for (; px >= 18; px -= 3) {
        font.setPixelSize(px);
        lines = wrapText(text, font, int(box.width()), maxLines);
        if (lines.isEmpty())
            continue;
        const QFontMetrics fm(font);
        int widest = 0;
        for (const QString &l : lines)
            widest = std::max(widest, fm.horizontalAdvance(l));
        if (widest <= box.width() && fm.lineSpacing() * lines.size() * 0.92 <= box.height())
            break;
    }
    if (lines.isEmpty()) {
        font.setPixelSize(18);
        lines = wrapText(text, font, int(box.width()), 99);
    }

    const QFontMetrics fm(font);
    const double lineH = fm.lineSpacing() * 0.92;
    const double top = box.top() + (box.height() - lineH * lines.size()) / 2.0;
    for (int i = 0; i < lines.size(); ++i) {
        const int w = fm.horizontalAdvance(lines.at(i));
        const double x = (align & Qt::AlignLeft) ? box.left() : box.left() + (box.width() - w) / 2.0;
        const double baseline = top + lineH * i + fm.ascent();
        QPainterPath path;
        path.addText(x, baseline, font, lines.at(i));
        p.setPen(QPen(QColor(0, 0, 0, 230), std::max(3.0, px * 0.14), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        // the last line carries the accent when there is more than one
        p.setPen(Qt::NoPen);
        p.setBrush(lines.size() > 1 && i == lines.size() - 1 ? o.accent : QColor(Qt::white));
        p.drawPath(path);
    }
    return out;
}

} // namespace Studio::Thumbnail
