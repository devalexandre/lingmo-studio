/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "exporter.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <cmath>
#include <numeric>

namespace Studio {

QList<ExportPreset> Exporter::presets()
{
    const QStringList aac = {QStringLiteral("acodec=aac"), QStringLiteral("ab=192k"), QStringLiteral("frequency=48000"), QStringLiteral("channels=2")};
    QList<ExportPreset> p;
    p.append({QStringLiteral("mp4-h264"), QCoreApplication::translate("Exporter", "MP4 (H.264 + AAC)"), QStringLiteral("web"), QStringLiteral("mp4"), QStringLiteral("h264"),
              QStringList{QStringLiteral("f=mp4"), QStringLiteral("vcodec=libx264"), QStringLiteral("preset=medium"), QStringLiteral("crf=20"),
                          QStringLiteral("pix_fmt=yuv420p"), QStringLiteral("movflags=+faststart"), QStringLiteral("bf=2")} + aac});
    p.append({QStringLiteral("youtube-1080p"), QCoreApplication::translate("Exporter", "YouTube 1080p (H.264)"), QStringLiteral("web"), QStringLiteral("mp4"), QStringLiteral("h264"),
              QStringList{QStringLiteral("f=mp4"), QStringLiteral("vcodec=libx264"), QStringLiteral("preset=slow"), QStringLiteral("crf=18"),
                          QStringLiteral("pix_fmt=yuv420p"), QStringLiteral("movflags=+faststart"), QStringLiteral("bf=2")} + aac,
              1920, 1080});
    p.append({QStringLiteral("youtube-4k-hevc"), QCoreApplication::translate("Exporter", "4K H.265 (HEVC)"), QStringLiteral("web"), QStringLiteral("mp4"), QStringLiteral("hevc"),
              QStringList{QStringLiteral("f=mp4"), QStringLiteral("vcodec=libx265"), QStringLiteral("preset=medium"), QStringLiteral("crf=24"),
                          QStringLiteral("pix_fmt=yuv420p"), QStringLiteral("vtag=hvc1"), QStringLiteral("movflags=+faststart")} + aac,
              3840, 2160});
    p.append({QStringLiteral("av1"), QCoreApplication::translate("Exporter", "AV1 (MP4)"), QStringLiteral("web"), QStringLiteral("mp4"), QStringLiteral("av1"),
              QStringList{QStringLiteral("f=mp4"), QStringLiteral("vcodec=libsvtav1"), QStringLiteral("preset=8"), QStringLiteral("crf=32"),
                          QStringLiteral("pix_fmt=yuv420p"), QStringLiteral("movflags=+faststart")} + aac});
    p.append({QStringLiteral("webm-vp9"), QCoreApplication::translate("Exporter", "WebM (VP9 + Opus)"), QStringLiteral("web"), QStringLiteral("webm"), QStringLiteral("vp9"),
              {QStringLiteral("f=webm"), QStringLiteral("vcodec=libvpx-vp9"), QStringLiteral("crf=31"), QStringLiteral("vb=0"),
               QStringLiteral("deadline=good"), QStringLiteral("cpu-used=4"), QStringLiteral("row-mt=1"), QStringLiteral("pix_fmt=yuv420p"),
               QStringLiteral("acodec=libopus"), QStringLiteral("ab=128k"), QStringLiteral("frequency=48000"), QStringLiteral("channels=2")}});
    p.append({QStringLiteral("shorts-9x16"), QCoreApplication::translate("Exporter", "Shorts / Reels / TikTok (9:16)"), QStringLiteral("social"), QStringLiteral("mp4"), QStringLiteral("h264"),
              QStringList{QStringLiteral("f=mp4"), QStringLiteral("vcodec=libx264"), QStringLiteral("preset=medium"), QStringLiteral("crf=20"),
                          QStringLiteral("pix_fmt=yuv420p"), QStringLiteral("movflags=+faststart")} + aac,
              1080, 1920, 0, false, true});
    p.append({QStringLiteral("prores-hq"), QCoreApplication::translate("Exporter", "ProRes 422 HQ (intermediate)"), QStringLiteral("master"), QStringLiteral("mov"), QStringLiteral("prores"),
              {QStringLiteral("f=mov"), QStringLiteral("vcodec=prores_ks"), QStringLiteral("vprofile=3"), QStringLiteral("pix_fmt=yuv422p10le"),
               QStringLiteral("acodec=pcm_s16le"), QStringLiteral("frequency=48000"), QStringLiteral("channels=2")}});
    p.append({QStringLiteral("dnxhr-hq"), QCoreApplication::translate("Exporter", "DNxHR HQ (intermediate)"), QStringLiteral("master"), QStringLiteral("mov"), QStringLiteral("dnxhr"),
              {QStringLiteral("f=mov"), QStringLiteral("vcodec=dnxhd"), QStringLiteral("vprofile=dnxhr_hq"), QStringLiteral("pix_fmt=yuv422p"),
               QStringLiteral("acodec=pcm_s16le"), QStringLiteral("frequency=48000"), QStringLiteral("channels=2")}});
    p.append({QStringLiteral("gif"), QCoreApplication::translate("Exporter", "Animated GIF"), QStringLiteral("animation"), QStringLiteral("gif"), QStringLiteral("gif"),
              {QStringLiteral("f=gif"), QStringLiteral("vcodec=gif"), QStringLiteral("an=1"), QStringLiteral("audio_off=1"),
               QStringLiteral("g=1"), QStringLiteral("bf=0")},
              480, 0, 15});
    p.append({QStringLiteral("audio-mp3"), QCoreApplication::translate("Exporter", "Audio only (MP3)"), QStringLiteral("audio"), QStringLiteral("mp3"), QStringLiteral("audio"),
              {QStringLiteral("f=mp3"), QStringLiteral("acodec=libmp3lame"), QStringLiteral("ab=320k"), QStringLiteral("frequency=48000"),
               QStringLiteral("channels=2"), QStringLiteral("vn=1"), QStringLiteral("video_off=1")},
              0, 0, 0, true});
    p.append({QStringLiteral("audio-wav"), QCoreApplication::translate("Exporter", "Audio only (WAV)"), QStringLiteral("audio"), QStringLiteral("wav"), QStringLiteral("audio"),
              {QStringLiteral("f=wav"), QStringLiteral("acodec=pcm_s16le"), QStringLiteral("frequency=48000"), QStringLiteral("channels=2"),
               QStringLiteral("vn=1"), QStringLiteral("video_off=1")},
              0, 0, 0, true});
    return p;
}

ExportPreset Exporter::preset(const QString &id)
{
    for (const ExportPreset &p : presets())
        if (p.id == id)
            return p;
    return presets().first();
}

CommandRunner Exporter::defaultRunner()
{
    return [](const QString &program, const QStringList &args, QString *output) {
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(program, args);
        if (!p.waitForStarted(5000))
            return -1;
        if (!p.waitForFinished(20000)) {
            p.kill();
            p.waitForFinished(1000);
            return -1;
        }
        if (output)
            *output = QString::fromUtf8(p.readAll());
        return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    };
}

HwEncoders Exporter::detectHardware(const QString &ffmpeg, const CommandRunner &runner)
{
    const CommandRunner run = runner ? runner : defaultRunner();
    HwEncoders hw;
    hw.detected = true;
    QString list;
    if (run(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")}, &list) != 0)
        return hw;
    const QStringList candidates = {QStringLiteral("h264_nvenc"), QStringLiteral("hevc_nvenc"), QStringLiteral("av1_nvenc"),
                                    QStringLiteral("h264_vaapi"), QStringLiteral("hevc_vaapi"), QStringLiteral("av1_vaapi")};
    const QStringList base = {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y")};
    const QStringList input = {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("color=black:s=256x144:d=0.1")};
    for (const QString &enc : candidates) {
        if (!list.contains(QRegularExpression(QStringLiteral("\\s%1\\s").arg(enc))))
            continue;
        QStringList args = base;
        if (enc.endsWith(QLatin1String("_vaapi"))) {
            if (!QFileInfo::exists(hw.vaapiDevice))
                continue;
            args << QStringLiteral("-vaapi_device") << hw.vaapiDevice << input << QStringLiteral("-vf") << QStringLiteral("format=nv12,hwupload");
        } else {
            args << input;
        }
        args << QStringLiteral("-c:v") << enc << QStringLiteral("-frames:v") << QStringLiteral("2") << QStringLiteral("-f") << QStringLiteral("null") << QStringLiteral("-");
        if (run(ffmpeg, args, nullptr) == 0)
            hw.available << enc;
    }
    return hw;
}

static void setArg(QStringList &args, const QString &key, const QString &value)
{
    for (QString &a : args) {
        if (a.startsWith(key + QLatin1Char('='))) {
            a = key + QLatin1Char('=') + value;
            return;
        }
    }
    args << key + QLatin1Char('=') + value;
}

static void removeArg(QStringList &args, const QString &key)
{
    args.erase(std::remove_if(args.begin(), args.end(), [&](const QString &a) { return a.startsWith(key + QLatin1Char('=')); }), args.end());
}

QStringList Exporter::meltArguments(const QString &projectFile, const Settings &s, const HwEncoders &hw,
                                    const ProjectSettings &project, QString *encoder)
{
    const ExportPreset p = preset(s.presetId);
    QStringList consumer = p.args;

    // Hardware encoder: NVENC first (no extra upload step), then VA-API
    QString chosen;
    if (s.hardware && (p.codecFamily == QLatin1String("h264") || p.codecFamily == QLatin1String("hevc") || p.codecFamily == QLatin1String("av1"))) {
        for (const QString &suffix : {QStringLiteral("_nvenc"), QStringLiteral("_vaapi")}) {
            const QString enc = p.codecFamily + suffix;
            if (hw.has(enc)) {
                chosen = enc;
                break;
            }
        }
    }
    const int q = s.quality;
    if (!chosen.isEmpty()) {
        setArg(consumer, QStringLiteral("vcodec"), chosen);
        removeArg(consumer, QStringLiteral("crf"));
        removeArg(consumer, QStringLiteral("preset"));
        removeArg(consumer, QStringLiteral("bf"));
        if (chosen.endsWith(QLatin1String("_nvenc"))) {
            setArg(consumer, QStringLiteral("preset"), QStringLiteral("p5"));
            setArg(consumer, QStringLiteral("rc"), QStringLiteral("vbr"));
            setArg(consumer, QStringLiteral("cq"), QString::number(q >= 0 ? q : 21));
            setArg(consumer, QStringLiteral("vb"), QStringLiteral("0"));
        } else {
            setArg(consumer, QStringLiteral("vaapi_device"), hw.vaapiDevice);
            setArg(consumer, QStringLiteral("qp"), QString::number(q >= 0 ? q : 22));
            removeArg(consumer, QStringLiteral("pix_fmt"));
        }
    } else {
        for (const QString &a : std::as_const(consumer))
            if (a.startsWith(QLatin1String("vcodec=")))
                chosen = a.mid(7);
        const bool hasCrf = std::any_of(consumer.cbegin(), consumer.cend(), [](const QString &a) { return a.startsWith(QLatin1String("crf=")); });
        if (q >= 0 && hasCrf)
            setArg(consumer, QStringLiteral("crf"), QString::number(q));
    }
    if (p.audioOnly)
        chosen.clear();
    if (encoder)
        *encoder = chosen;

    // Output geometry: preset size wins; keep the project's aspect for "width only"
    if (!p.audioOnly) {
        int w = p.width, h = p.height;
        if (w > 0 && h <= 0) {
            h = int(std::lround(double(w) * project.height / project.width / 2.0)) * 2;
        }
        if (w > 0 && h > 0) {
            setArg(consumer, QStringLiteral("width"), QString::number(w));
            setArg(consumer, QStringLiteral("height"), QString::number(h));
            const int g = std::gcd(w, h);
            setArg(consumer, QStringLiteral("display_aspect_num"), QString::number(w / g));
            setArg(consumer, QStringLiteral("display_aspect_den"), QString::number(h / g));
            setArg(consumer, QStringLiteral("sample_aspect_num"), QStringLiteral("1"));
            setArg(consumer, QStringLiteral("sample_aspect_den"), QStringLiteral("1"));
        }
        if (p.fps > 0) {
            setArg(consumer, QStringLiteral("frame_rate_num"), QString::number(p.fps));
            setArg(consumer, QStringLiteral("frame_rate_den"), QStringLiteral("1"));
        }
        setArg(consumer, QStringLiteral("progressive"), QStringLiteral("1"));
        setArg(consumer, QStringLiteral("rescale"), QStringLiteral("bicubic"));
    }
    setArg(consumer, QStringLiteral("real_time"), QString::number(-std::max(1, s.threads)));
    setArg(consumer, QStringLiteral("threads"), QString::number(std::max(1, s.threads)));

    QStringList args;
    args << QStringLiteral("-progress2") << projectFile;
    if (s.in >= 0)
        args << QStringLiteral("in=%1").arg(s.in);
    if (s.out >= 0)
        args << QStringLiteral("out=%1").arg(s.out);
    args << QStringLiteral("-consumer") << QStringLiteral("avformat:") + s.output;
    args << consumer;
    return args;
}

int Exporter::parseProgress(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("percentage:\\s*(\\d+)"));
    const auto m = re.match(line);
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

} // namespace Studio
