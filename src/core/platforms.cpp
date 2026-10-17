/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "platforms.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>
#include <numeric>

namespace Studio {

QVariantMap Platform::toMap() const
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("aspect"), aspect},
            {QStringLiteral("width"), width},
            {QStringLiteral("height"), height},
            {QStringLiteral("fps"), fps},
            {QStringLiteral("maxSeconds"), maxSeconds},
            {QStringLiteral("minRecommended"), idealMin()},
            {QStringLiteral("maxRecommended"), idealMax()},
            {QStringLiteral("safeTop"), safeArea.top},
            {QStringLiteral("safeBottom"), safeArea.bottom},
            {QStringLiteral("safeLeft"), safeArea.left},
            {QStringLiteral("safeRight"), safeArea.right},
            {QStringLiteral("exportPreset"), exportPreset},
            {QStringLiteral("source"), source}};
}

namespace Platforms {

static double share(const QJsonObject &o, const char *key)
{
    return std::clamp(o.value(QLatin1String(key)).toDouble(), 0.0, 0.45);
}

QList<Platform> fromJson(const QByteArray &json, QStringList *errors)
{
    QList<Platform> list;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (!doc.isObject()) {
        if (errors)
            errors->append(err.errorString());
        return list;
    }
    for (const QJsonValue &v : doc.object().value(QStringLiteral("platforms")).toArray()) {
        const QJsonObject o = v.toObject();
        Platform p;
        p.id = o.value(QStringLiteral("id")).toString();
        p.name = o.value(QStringLiteral("name")).toString(p.id);
        p.width = o.value(QStringLiteral("width")).toInt();
        p.height = o.value(QStringLiteral("height")).toInt();
        p.aspect = o.value(QStringLiteral("aspect")).toString();
        if (p.aspect.isEmpty() && p.width > 0 && p.height > 0) {
            const int g = std::gcd(p.width, p.height);
            p.aspect = QStringLiteral("%1:%2").arg(p.width / g).arg(p.height / g);
        }
        p.fps = o.value(QStringLiteral("fps")).toInt(30);
        p.maxSeconds = o.value(QStringLiteral("maxSeconds")).toInt(60);
        const QJsonArray rec = o.value(QStringLiteral("recommended")).toArray();
        p.minRecommended = rec.size() == 2 ? rec.at(0).toInt() : std::min(15, p.maxSeconds);
        p.maxRecommended = rec.size() == 2 ? rec.at(1).toInt() : p.maxSeconds;
        const QJsonObject safe = o.value(QStringLiteral("safeArea")).toObject();
        p.safeArea = {share(safe, "top"), share(safe, "bottom"), share(safe, "left"), share(safe, "right")};
        p.exportPreset = o.value(QStringLiteral("exportPreset")).toString(QStringLiteral("mp4-h264"));
        p.source = o.value(QStringLiteral("source")).toString();
        if (!p.valid() || p.maxSeconds <= 0 || p.minRecommended <= 0 || p.minRecommended > p.maxRecommended || p.fps <= 0) {
            if (errors)
                errors->append(QStringLiteral("invalid platform entry: %1").arg(p.id));
            continue;
        }
        list.append(p);
    }
    return list;
}

QList<Platform> load(const QString &path, QStringList *errors)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errors)
            errors->append(f.errorString());
        return {};
    }
    return fromJson(f.readAll(), errors);
}

QString locate(const QString &fallback)
{
    QStringList candidates = {qEnvironmentVariable("LINGMO_STUDIO_PLATFORMS")};
    candidates += QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("lingmo-studio/platforms.json"));
    candidates << fallback;
    for (const QString &c : std::as_const(candidates))
        if (!c.isEmpty() && QFile::exists(c))
            return c;
    return {};
}

static QList<Platform> &cache()
{
    static QList<Platform> list;
    return list;
}

const QList<Platform> &all()
{
    if (cache().isEmpty()) {
        const QString path = locate();
        if (!path.isEmpty())
            cache() = load(path);
    }
    return cache();
}

void setAll(const QList<Platform> &list)
{
    cache() = list;
}

Platform find(const QString &id)
{
    for (const Platform &p : all())
        if (p.id == id)
            return p;
    return {};
}

Platform primary(const QStringList &ids, bool *sameAspect)
{
    Platform first;
    bool same = true;
    for (const QString &id : ids) {
        const Platform p = find(id);
        if (!p.valid())
            continue;
        if (!first.valid())
            first = p;
        else if (qint64(p.width) * first.height != qint64(first.width) * p.height)
            same = false;
    }
    if (sameAspect)
        *sameAspect = same;
    return first;
}

} // namespace Platforms
} // namespace Studio
