/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "reframe.h"
#include "timelineops.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <functional>

namespace Studio::Reframe {

QList<Sample> parseDetections(const QByteArray &output)
{
    QList<Sample> list;
    auto add = [&](const QJsonObject &o) {
        if (!o.contains(QStringLiteral("t")))
            return;
        Sample s;
        s.t = o.value(QStringLiteral("t")).toDouble();
        const QJsonValue x = o.value(QStringLiteral("x"));
        s.x = x.isDouble() ? std::clamp(x.toDouble(), 0.0, 1.0) : -1;
        s.confidence = o.value(QStringLiteral("c")).toDouble(s.x >= 0 ? 1.0 : 0.0);
        list.append(s);
    };
    const QJsonDocument doc = QJsonDocument::fromJson(output);
    if (doc.isArray()) {
        for (const QJsonValue &v : doc.array())
            add(v.toObject());
    } else {
        for (const QByteArray &line : output.split('\n')) {
            const QJsonDocument d = QJsonDocument::fromJson(line.trimmed());
            if (d.isObject())
                add(d.object());
        }
    }
    std::sort(list.begin(), list.end(), [](const Sample &a, const Sample &b) { return a.t < b.t; });
    return list;
}

QList<Sample> smooth(const QList<Sample> &raw, const SmoothOptions &o)
{
    QList<Sample> s = raw;
    if (s.isEmpty())
        return s;
    // 1. gaps: hold the last known position (centre before the first detection)
    double first = 0.5;
    for (const Sample &x : s)
        if (x.x >= 0) {
            first = x.x;
            break;
        }
    double last = first;
    for (Sample &x : s) {
        if (x.x < 0)
            x.x = last;
        last = x.x;
    }
    // 2. median: one-off false detections disappear
    const int half = std::max(0, o.medianWindow / 2);
    QList<Sample> med = s;
    for (int i = 0; i < s.size(); ++i) {
        QList<double> win;
        for (int k = std::max(0, i - half); k <= std::min<int>(s.size() - 1, i + half); ++k)
            win << s.at(k).x;
        std::sort(win.begin(), win.end());
        med[i].x = win.at(win.size() / 2);
    }
    // 3. virtual camera: dead zone, speed limit, easing
    QList<Sample> out = med;
    double cam = med.first().x;
    double target = cam;
    out[0].x = cam;
    for (int i = 1; i < med.size(); ++i) {
        const double dt = std::max(1e-3, med.at(i).t - med.at(i - 1).t);
        if (std::abs(med.at(i).x - target) > o.deadZone)
            target = med.at(i).x;
        double step = (target - cam) * o.ease;
        const double maxStep = o.maxSpeed * dt;
        step = std::clamp(step, -maxStep, maxStep);
        cam += step;
        out[i].x = cam;
    }
    return out;
}

double fillScale(int srcW, int srcH, int projW, int projH)
{
    if (srcW <= 0 || srcH <= 0 || projW <= 0 || projH <= 0)
        return 100;
    const double fit = std::min(double(projW) / srcW, double(projH) / srcH);
    const double fill = std::max(double(projW) / srcW, double(projH) / srcH);
    return fill / fit * 100.0;
}

double positionFor(double x, int srcW, int srcH, int projW, int projH)
{
    // Transform: the project-sized rect is scaled by s and the source fits inside it
    const double s = fillScale(srcW, srcH, projW, projH) / 100.0;
    const double fit = std::min(double(projW) / srcW, double(projH) / srcH);
    const double shownW = srcW * fit * s; // displayed width of the source
    const double limit = std::max(0.0, (shownW - projW) / 2.0);
    return std::clamp((0.5 - x) * shownW, -limit, limit);
}

QList<Keyframe> keyframes(const QList<Sample> &path, double inSec, int clipFrames, double fps, int srcW, int srcH, int projW, int projH,
                          const QVariantMap &base, double tolerancePx)
{
    QList<Keyframe> kfs;
    if (path.isEmpty() || clipFrames <= 0 || fps <= 0)
        return kfs;
    auto xAt = [&](double t) {
        if (t <= path.first().t)
            return path.first().x;
        if (t >= path.last().t)
            return path.last().x;
        for (int i = 1; i < path.size(); ++i) {
            if (path.at(i).t >= t) {
                const Sample &a = path.at(i - 1), &b = path.at(i);
                const double f = (t - a.t) / std::max(1e-6, b.t - a.t);
                return a.x + (b.x - a.x) * f;
            }
        }
        return path.last().x;
    };
    // Dense values every ~1/4 s, then keep only the points a straight line can't stand in for
    const int step = std::max(1, int(std::lround(fps / 4)));
    QList<QPair<int, double>> dense;
    for (int f = 0; f < clipFrames; f += step)
        dense.append({f, positionFor(xAt(inSec + f / fps), srcW, srcH, projW, projH)});
    if (dense.last().first != clipFrames - 1)
        dense.append({clipFrames - 1, positionFor(xAt(inSec + (clipFrames - 1) / fps), srcW, srcH, projW, projH)});
    // Douglas–Peucker on (frame, x)
    QList<bool> keep(dense.size(), false);
    keep.first() = keep.last() = true;
    std::function<void(int, int)> simplify = [&](int a, int b) {
        if (b <= a + 1)
            return;
        double worst = 0;
        int idx = -1;
        for (int i = a + 1; i < b; ++i) {
            const double f = double(dense.at(i).first - dense.at(a).first) / std::max(1, dense.at(b).first - dense.at(a).first);
            const double expect = dense.at(a).second + (dense.at(b).second - dense.at(a).second) * f;
            const double d = std::abs(dense.at(i).second - expect);
            if (d > worst) {
                worst = d;
                idx = i;
            }
        }
        if (idx >= 0 && worst > tolerancePx) {
            keep[idx] = true;
            simplify(a, idx);
            simplify(idx, b);
        }
    };
    simplify(0, dense.size() - 1);
    bool moves = false;
    for (int i = 1; i < dense.size(); ++i)
        moves |= std::abs(dense.at(i).second - dense.first().second) > tolerancePx;
    for (int i = 0; i < dense.size(); ++i) {
        if (!keep.at(i) || (!moves && i > 0))
            continue;
        QVariantMap v = base;
        v[QStringLiteral("x")] = std::round(dense.at(i).second * 10) / 10;
        kfs.append({dense.at(i).first, v});
    }
    return kfs;
}

QString detectorScript()
{
    const QString env = qEnvironmentVariable("LINGMO_STUDIO_FACETRACK");
    if (!env.isEmpty() && QFile::exists(env))
        return env;
    const QString installed = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("lingmo-studio/facetrack.py"));
    if (!installed.isEmpty())
        return installed;
    const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/../share/lingmo-studio/facetrack.py");
    if (QFile::exists(beside))
        return beside;
    // the copy built into the app (uninstalled builds): run it from the cache
    QFile builtin(QStringLiteral(":/data/facetrack.py"));
    if (!builtin.open(QIODevice::ReadOnly))
        return {};
    const QByteArray code = builtin.readAll();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/facetrack.py");
    QFile out(path);
    if (out.open(QIODevice::ReadOnly) && out.readAll() == code)
        return path;
    out.close();
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    out.write(code);
    return path;
}

QStringList detectorArguments(const QString &script, const QString &input, double startSec, double durationSec, double sampleFps)
{
    return {script, QStringLiteral("--start"), QString::number(startSec, 'f', 3), QStringLiteral("--duration"), QString::number(durationSec, 'f', 3),
            QStringLiteral("--fps"), QString::number(sampleFps, 'f', 2), input};
}

} // namespace Studio::Reframe
