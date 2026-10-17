/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Export presets, hardware encoder detection and the melt command line.
// Renders run in a separate `melt` process (see app/renderqueue): a crash or
// a cancel never touches the editor, and progress comes from melt -progress2.

#pragma once

#include "model.h"

#include <QStringList>
#include <functional>

namespace Studio {

struct ExportPreset {
    QString id;
    QString name;
    QString category;  // web | social | master | audio | animation
    QString extension;
    QString codecFamily; // h264 | hevc | av1 | vp9 | prores | dnxhr | gif | audio
    QStringList args;    // avformat consumer properties (software path)
    int width = 0;       // 0 = project size
    int height = 0;
    int fps = 0;         // 0 = project rate
    bool audioOnly = false;
    bool vertical = false; // made for 9:16 projects
};

struct HwEncoders {
    QStringList available;          // e.g. "h264_nvenc", "hevc_vaapi"
    QString vaapiDevice = QStringLiteral("/dev/dri/renderD128");
    bool detected = false;
    bool has(const QString &encoder) const { return available.contains(encoder); }
};

// Runs a program and returns its exit code (stdout+stderr in *output). Tests replace it.
using CommandRunner = std::function<int(const QString &program, const QStringList &args, QString *output)>;

class Exporter
{
public:
    static QList<ExportPreset> presets();
    static ExportPreset preset(const QString &id);
    static QString defaultPresetId() { return QStringLiteral("mp4-h264"); }

    // Lists encoders with `ffmpeg -encoders`, then proves each hardware one
    // with a tiny test encode (a listed encoder often has no device behind it).
    static HwEncoders detectHardware(const QString &ffmpeg = QStringLiteral("ffmpeg"), const CommandRunner &run = {});
    static CommandRunner defaultRunner();

    struct Settings {
        QString presetId = QStringLiteral("mp4-h264");
        QString output;
        bool hardware = true; // use a detected hardware encoder when the preset allows
        int quality = -1;     // preset default when < 0 (crf/cq/qp scale 0..51)
        int in = -1;          // range (frames); -1 = whole timeline
        int out = -1;
        int threads = 4;
    };
    // `melt <project> [in= out=] -consumer avformat:<out> k=v ...`
    // *encoder receives the video encoder chosen (e.g. "h264_nvenc" or "libx264").
    static QStringList meltArguments(const QString &projectFile, const Settings &settings, const HwEncoders &hw,
                                     const ProjectSettings &project, QString *encoder = nullptr);
    // Parses a melt -progress2 line; returns percent or -1
    static int parseProgress(const QString &line);
};

} // namespace Studio
