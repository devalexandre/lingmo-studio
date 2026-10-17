/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Async image providers (they run in Qt's image thread pool):
//   image://thumb/<mediaId>/<frame>     video frame thumbnails (memory cache)
//   image://wave/<mediaId>/<in>/<out>   audio waveform of a source range
//   image://scope/<kind>/<serial>       latest scope image (see scopes.h)

#pragma once

#include "model.h"

#include <QQuickAsyncImageProvider>

// Thread-safe copy of what the providers need from the editor.
namespace MediaRegistry {
void set(const QList<Studio::MediaItem> &media, const Studio::ProjectSettings &settings);
bool find(const QString &id, Studio::MediaItem *out, Studio::ProjectSettings *settings);
}

class ThumbnailProvider : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};

class WaveformProvider : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};

class ScopeProvider : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
