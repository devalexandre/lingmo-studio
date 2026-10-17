/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Synthetic test media made with ffmpeg in the build folder (never user files).

#pragma once

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace TestMedia {

inline QString dir()
{
    QDir().mkpath(QStringLiteral(TEST_SCRATCH));
    return QStringLiteral(TEST_SCRATCH);
}

inline bool haveFfmpeg()
{
    return !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
}

inline bool ffmpeg(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral("ffmpeg"), QStringList{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y")} + args);
    return p.waitForFinished(60000) && p.exitCode() == 0;
}

// 4 s, 640x360, 30 fps, H.264 + AAC sine
inline QString video()
{
    const QString path = QDir(dir()).filePath(QStringLiteral("testsrc.mp4"));
    if (!QFileInfo::exists(path))
        ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc2=s=640x360:r=30:d=4"),
                QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=f=440:d=4"),
                QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-shortest"), path});
    return path;
}

// 6 s: 2 s tone, 2 s silence, 2 s tone
inline QString speech()
{
    const QString path = QDir(dir()).filePath(QStringLiteral("gaps.wav"));
    if (!QFileInfo::exists(path))
        ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                QStringLiteral("aevalsrc='if(between(t,2,4),0,0.5*sin(2*PI*330*t))':s=48000:d=6"), path});
    return path;
}

// 3 s of red then 3 s of blue: one scene cut at 3 s
inline QString scenes()
{
    const QString path = QDir(dir()).filePath(QStringLiteral("scenes.mp4"));
    if (!QFileInfo::exists(path))
        ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("color=red:s=320x180:r=30:d=3"),
                QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("color=blue:s=320x180:r=30:d=3"),
                QStringLiteral("-filter_complex"), QStringLiteral("[0:v][1:v]concat=n=2:v=1[v]"), QStringLiteral("-map"), QStringLiteral("[v]"),
                QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), path});
    return path;
}

} // namespace TestMedia
