/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Media inspection through MLT (and ffmpeg for audio peaks). Every function
// is blocking and safe to call from worker threads.

#pragma once

#include "model.h"

#include <QImage>

namespace Studio::MediaProbe {

bool isSupported(const QString &path);
QStringList nameFilters();
// Fills kind, length (project frames), size, fps, codecs. Returns false if unreadable.
bool probe(const QString &path, const ProjectSettings &project, MediaItem *item, QString *error = nullptr);
// Frame `frame` (project frames) scaled to fit `size`
QImage thumbnail(const QString &path, int frame, const QSize &size, const ProjectSettings &project);
// Peak levels 0..1, `buckets` values over the whole file (ffmpeg decode)
QList<float> audioPeaks(const QString &path, int buckets, const QString &ffmpeg = QStringLiteral("ffmpeg"));
// ffmpeg arguments for an edit-friendly proxy (all-intra H.264, 540p, same rate)
QStringList proxyArguments(const QString &input, const QString &output, int height = 540);
QString proxyPathFor(const QString &input, const QString &cacheDir);

} // namespace Studio::MediaProbe
