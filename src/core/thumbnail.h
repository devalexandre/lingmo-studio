/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Thumbnails: scoring candidate frames (sharp, contrasty, well lit, not black)
// and composing the final image, a frame with a headline on top. Pure image
// code; the app picks the frames from the timeline (src/app/metadata.cpp).

#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>

namespace Studio::Thumbnail {

// bold: text on the left over a dark gradient; banner: a band along the
// bottom; center: big text in the middle over a vignette
QStringList styleIds();

struct Options {
    QString text;
    QString style = QStringLiteral("bold");
    QSize size = QSize(1280, 720);
    QColor accent = QColor(0xFF, 0xD2, 0x1F);
};

// 0..1, higher is a better thumbnail: detail, contrast, colour and a middling brightness.
// Black, white and flat frames score near zero.
double frameScore(const QImage &frame);

// Indices of the best `count` scores, best first, no two closer than `minGap` positions
QList<int> pickDiverse(const QList<double> &scores, int count, int minGap);

// The headline broken into lines for a box `maxWidth` wide; empty when it needs more
// than `maxLines`. A word wider than the box stays alone on its line. Exposed for tests.
QStringList wrapText(const QString &text, const QFont &font, int maxWidth, int maxLines);

// `frame` cropped to fill options.size, darkened where the text sits, headline on top
QImage compose(const QImage &frame, const Options &options);

} // namespace Studio::Thumbnail
