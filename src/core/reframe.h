/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Subject-aware reframing (16:9 → 9:16 and friends). A helper script
// (data/facetrack.py, OpenCV through Python) samples frames and reports where
// the face/subject is; OpenCV is optional and checked at run time. The path
// is smoothed like a camera operator would (dead zone, limited speed, ease)
// and baked into Position X keyframes of the clip's Transform effect, so the
// user can adjust it. Without OpenCV the clip is centred.

#pragma once

#include "model.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace Studio::Reframe {

struct Sample {
    double t = 0;     // seconds in the source
    double x = -1;    // subject centre, 0..1 of the source width; < 0 = nothing found
    double confidence = 0;
};

// JSON lines / array from facetrack.py: {"t":1.2,"x":0.43,"w":0.1,"c":0.9} or {"t":1.4,"x":null}
QList<Sample> parseDetections(const QByteArray &output);

struct SmoothOptions {
    double deadZone = 0.06;   // ignore moves smaller than this (share of the source width)
    double maxSpeed = 0.35;   // share of the source width per second
    double ease = 0.35;       // exponential smoothing factor per sample (0..1, lower = smoother)
    int medianWindow = 5;
};
// Fills gaps (holds the last position, centre at the start), removes
// outliers (median), then follows with a dead zone, speed limit and easing.
QList<Sample> smooth(const QList<Sample> &raw, const SmoothOptions &o = {});

// Scale (%) of the Transform effect that makes a src frame fill the project frame
double fillScale(int srcW, int srcH, int projW, int projH);
// Position X (project px) that puts source x (0..1) in the middle, clamped so the frame stays covered
double positionFor(double x, int srcW, int srcH, int projW, int projH);

// Keyframes (frames relative to the clip start) for a clip that plays the
// source from `inSec`; only where the path moves more than `tolerancePx`.
QList<Keyframe> keyframes(const QList<Sample> &path, double inSec, int clipFrames, double fps, int srcW, int srcH, int projW, int projH,
                          const QVariantMap &base, double tolerancePx = 6);

// The helper: python3 + cv2, and the script location
QString detectorScript();
QStringList detectorArguments(const QString &script, const QString &input, double startSec, double durationSec, double sampleFps = 4);

} // namespace Studio::Reframe
