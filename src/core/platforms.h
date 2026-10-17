/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Social platform presets for automatic Shorts/Reels ("Gerar cortes"):
// frame size, length limits, the area the platform's interface covers and
// the export preset. They live in data/platforms.json so they are easy to
// update without rebuilding (see the notes in that file).

#pragma once

#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <algorithm>

namespace Studio {

struct SafeArea {
    double top = 0;    // share of the frame height covered at the top (0..1)
    double bottom = 0;
    double left = 0;   // share of the frame width
    double right = 0;
    // The usable rectangle in pixels of a w×h frame
    QRectF rect(int w, int h) const { return QRectF(left * w, top * h, w * (1 - left - right), h * (1 - top - bottom)); }
};

struct Platform {
    QString id;
    QString name;
    QString aspect; // "9:16"
    int width = 1080;
    int height = 1920;
    int fps = 30;
    int maxSeconds = 60;
    int minRecommended = 15; // editable default range used to pick highlights
    int maxRecommended = 60;
    SafeArea safeArea;
    QString exportPreset = QStringLiteral("shorts-9x16");
    QString source;

    bool valid() const { return !id.isEmpty() && width > 0 && height > 0; }
    // Recommended range clamped to the platform maximum
    int idealMin() const { return std::min(minRecommended, maxSeconds); }
    int idealMax() const { return std::min(maxRecommended, maxSeconds); }
    QVariantMap toMap() const;
};

namespace Platforms {

// Parses the JSON document; invalid entries are skipped (and reported in *errors)
QList<Platform> fromJson(const QByteArray &json, QStringList *errors = nullptr);
QList<Platform> load(const QString &path, QStringList *errors = nullptr);
// First readable file of: $LINGMO_STUDIO_PLATFORMS, ~/.local/share/lingmo-studio/platforms.json,
// $XDG_DATA_DIRS/lingmo-studio/platforms.json, then `fallback` (e.g. a qrc path)
QString locate(const QString &fallback = QStringLiteral(":/data/platforms.json"));
// Cached list from locate()
const QList<Platform> &all();
void setAll(const QList<Platform> &list); // tests
Platform find(const QString &id);

// The common frame size for a set of platforms: the size of the first one;
// *sameAspect is false when they ask for different aspect ratios.
Platform primary(const QStringList &ids, bool *sameAspect = nullptr);

} // namespace Platforms
} // namespace Studio
