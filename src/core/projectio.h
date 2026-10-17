/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Project files are plain MLT XML (.mlt). Studio-specific data rides along as
// "studio:*" properties (clip JSON on each cut, media bin, markers, nested
// sequences), which other MLT tools ignore. Files from other MLT tools load
// best-effort: tracks, cuts, blanks and dissolves; their filters are dropped.

#pragma once

#include "model.h"
#include "plan.h"

namespace Studio {

struct ProjectData {
    ProjectSettings settings;
    Document doc;
    QList<MediaItem> media;
    QStringList warnings;
};

namespace ProjectIO {

QByteArray toXml(const ProjectData &data, const QString &title = {}, bool useProxies = false);
bool save(const QString &path, const ProjectData &data, QString *error = nullptr);
bool load(const QString &path, ProjectData *data, QString *error = nullptr);
bool loadXml(const QByteArray &xml, ProjectData *data, QString *error = nullptr);

QJsonObject timelineToJson(const Timeline &tl);
Timeline timelineFromJson(const QJsonObject &o);

} // namespace ProjectIO
} // namespace Studio
