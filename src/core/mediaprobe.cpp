/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "mediaprobe.h"
#include "mltbuilder.h"

#include <Mlt.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <cmath>

namespace Studio::MediaProbe {

static const QStringList VideoExt = {QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mov"), QStringLiteral("mkv"),
                                     QStringLiteral("webm"), QStringLiteral("avi"), QStringLiteral("mts"), QStringLiteral("m2ts"),
                                     QStringLiteral("ts"), QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("ogv"),
                                     QStringLiteral("flv"), QStringLiteral("wmv"), QStringLiteral("3gp"), QStringLiteral("mxf")};
static const QStringList AudioExt = {QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"),
                                     QStringLiteral("oga"), QStringLiteral("opus"), QStringLiteral("m4a"), QStringLiteral("aac"),
                                     QStringLiteral("aiff"), QStringLiteral("aif"), QStringLiteral("wma")};
static const QStringList ImageExt = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("webp"),
                                     QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("svg"), QStringLiteral("tif"),
                                     QStringLiteral("tiff"), QStringLiteral("avif")};

bool isSupported(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return VideoExt.contains(s) || AudioExt.contains(s) || ImageExt.contains(s);
}

QStringList nameFilters()
{
    auto globs = [](const QStringList &exts) {
        QStringList g;
        for (const QString &e : exts)
            g << QStringLiteral("*.") + e;
        return g.join(QLatin1Char(' '));
    };
    const QString tr1 = QCoreApplication::translate("MediaProbe", "Media files");
    return {QStringLiteral("%1 (%2 %3 %4)").arg(tr1, globs(VideoExt), globs(AudioExt), globs(ImageExt)),
            QCoreApplication::translate("MediaProbe", "Video (%1)").arg(globs(VideoExt)),
            QCoreApplication::translate("MediaProbe", "Audio (%1)").arg(globs(AudioExt)),
            QCoreApplication::translate("MediaProbe", "Images (%1)").arg(globs(ImageExt)),
            QCoreApplication::translate("MediaProbe", "All files (*)")};
}

static void setupProfile(Mlt::Profile &profile, const ProjectSettings &s)
{
    profile.set_width(s.width);
    profile.set_height(s.height);
    profile.set_frame_rate(s.fpsNum, s.fpsDen);
    profile.set_sample_aspect(1, 1);
    profile.set_display_aspect(s.width, s.height);
    profile.set_progressive(1);
    profile.set_colorspace(709);
    profile.set_explicit(1);
}

bool probe(const QString &path, const ProjectSettings &project, MediaItem *item, QString *error)
{
    initMlt();
    if (!QFileInfo(path).isFile()) {
        if (error)
            *error = QCoreApplication::translate("MediaProbe", "File not found");
        return false;
    }
    Mlt::Profile profile;
    setupProfile(profile, project);
    Mlt::Producer p(profile, path.toUtf8().constData());
    if (!p.is_valid()) {
        if (error)
            *error = QCoreApplication::translate("MediaProbe", "Unsupported or damaged file");
        return false;
    }
    // Some properties (size, frame rate) only appear once a frame was decoded
    p.probe();
    if (!p.get_int("meta.media.width")) {
        std::unique_ptr<Mlt::Frame> f(p.get_frame());
    }
    MediaItem &m = *item;
    m.path = QFileInfo(path).absoluteFilePath();
    m.name = QFileInfo(path).fileName();
    const QString service = QString::fromUtf8(p.get("mlt_service"));
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (service.startsWith(QLatin1String("qimage")) || service.startsWith(QLatin1String("pixbuf")) || ImageExt.contains(suffix)) {
        m.kind = QStringLiteral("image");
        m.hasVideo = true;
        m.hasAudio = false;
        m.length = int(std::lround(project.fps() * 5));
        m.width = p.get_int("meta.media.width");
        m.height = p.get_int("meta.media.height");
        return true;
    }
    const int streams = p.get_int("meta.media.nb_streams");
    for (int i = 0; i < streams; ++i) {
        const QByteArray prefix = QByteArray("meta.media.") + QByteArray::number(i) + '.';
        const QByteArray type = p.get((prefix + "stream.type").constData());
        if (type == "video" && !m.hasVideo) {
            const QByteArray codec = p.get((prefix + "codec.name").constData());
            // Cover art in audio files shows up as a one-frame mjpeg/png stream
            if ((codec == "mjpeg" || codec == "png") && AudioExt.contains(suffix))
                continue;
            m.hasVideo = true;
            m.videoCodec = QString::fromUtf8(codec);
        } else if (type == "audio" && !m.hasAudio) {
            m.hasAudio = true;
            m.audioCodec = QString::fromUtf8(p.get((prefix + "codec.name").constData()));
            m.sampleRate = p.get_int((prefix + "codec.sample_rate").constData());
            m.audioChannels = p.get_int((prefix + "codec.channels").constData());
        }
    }
    m.width = p.get_int("meta.media.width");
    m.height = p.get_int("meta.media.height");
    const int num = p.get_int("meta.media.frame_rate_num");
    const int den = p.get_int("meta.media.frame_rate_den");
    m.fps = den > 0 ? double(num) / den : 0;
    m.length = p.get_length();
    m.kind = m.hasVideo ? QStringLiteral("video") : QStringLiteral("audio");
    if (!m.hasVideo && !m.hasAudio) {
        if (error)
            *error = QCoreApplication::translate("MediaProbe", "No audio or video in this file");
        return false;
    }
    return m.length > 0;
}

QImage thumbnail(const QString &path, int frame, const QSize &size, const ProjectSettings &project)
{
    initMlt();
    Mlt::Profile profile;
    setupProfile(profile, project);
    Mlt::Producer p(profile, path.toUtf8().constData());
    if (!p.is_valid())
        return {};
    p.seek(std::clamp(frame, 0, std::max(0, p.get_length() - 1)));
    std::unique_ptr<Mlt::Frame> f(p.get_frame());
    if (!f)
        return {};
    // Fit the project aspect inside the requested box
    int w = size.width();
    int h = int(std::lround(double(w) * project.height / project.width));
    if (h > size.height()) {
        h = size.height();
        w = int(std::lround(double(h) * project.width / project.height));
    }
    w += w % 2;
    h += h % 2;
    mlt_image_format fmt = mlt_image_rgba;
    f->set("consumer.rescale", "bilinear");
    f->set("consumer.deinterlacer", "onefield");
    const uint8_t *img = f->get_image(fmt, w, h);
    if (!img || fmt != mlt_image_rgba)
        return {};
    return QImage(img, w, h, QImage::Format_RGBA8888).copy();
}

QList<float> audioPeaks(const QString &path, int buckets, const QString &ffmpeg)
{
    QList<float> peaks;
    if (buckets <= 0)
        return peaks;
    QProcess p;
    p.start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), path, QStringLiteral("-vn"),
                     QStringLiteral("-ac"), QStringLiteral("1"), QStringLiteral("-ar"), QStringLiteral("4000"),
                     QStringLiteral("-f"), QStringLiteral("s16le"), QStringLiteral("-")});
    if (!p.waitForFinished(120000))
        return peaks;
    const QByteArray pcm = p.readAllStandardOutput();
    const auto *s = reinterpret_cast<const qint16 *>(pcm.constData());
    const qsizetype n = pcm.size() / 2;
    if (n == 0)
        return peaks;
    peaks.resize(buckets);
    for (int b = 0; b < buckets; ++b) {
        const qsizetype from = n * b / buckets;
        const qsizetype to = std::max(from + 1, n * (b + 1) / buckets);
        int peak = 0;
        for (qsizetype i = from; i < to && i < n; ++i)
            peak = std::max(peak, std::abs(int(s[i])));
        peaks[b] = float(peak) / 32768.0f;
    }
    return peaks;
}

QStringList proxyArguments(const QString &input, const QString &output, int height)
{
    return {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
            QStringLiteral("-i"), input,
            QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-map"), QStringLiteral("0:a?"),
            QStringLiteral("-vf"), QStringLiteral("scale=-2:%1").arg(height),
            QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("veryfast"),
            QStringLiteral("-crf"), QStringLiteral("23"), QStringLiteral("-g"), QStringLiteral("1"), QStringLiteral("-bf"), QStringLiteral("0"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-fps_mode"), QStringLiteral("passthrough"),
            QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"), QStringLiteral("160k"),
            QStringLiteral("-progress"), QStringLiteral("pipe:1"), QStringLiteral("-nostats"),
            output};
}

QString proxyPathFor(const QString &input, const QString &cacheDir)
{
    const QFileInfo fi(input);
    const QByteArray key = (fi.absoluteFilePath() + QString::number(fi.size()) + QString::number(fi.lastModified().toSecsSinceEpoch())).toUtf8();
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(16));
    return QDir(cacheDir).filePath(fi.completeBaseName() + QLatin1Char('-') + hash + QStringLiteral(".proxy.mp4"));
}

} // namespace Studio::MediaProbe
