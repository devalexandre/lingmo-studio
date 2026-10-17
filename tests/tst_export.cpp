/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Export: presets, hardware encoder detection (with a fake ffmpeg), the melt
// command line, and one real render checked with ffprobe.

#include "exporter.h"
#include "mediaprobe.h"
#include "projectio.h"
#include "testmedia.h"
#include "timelineops.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Studio;

class TestExport : public QObject
{
    Q_OBJECT

    static QString value(const QStringList &args, const QString &key)
    {
        for (const QString &a : args)
            if (a.startsWith(key + QLatin1Char('=')))
                return a.mid(key.size() + 1);
        return {};
    }

    // ffmpeg that lists NVENC and VA-API but whose test encodes succeed only for `working`
    static CommandRunner fakeFfmpeg(const QStringList &working, QStringList *calls = nullptr)
    {
        return [working, calls](const QString &, const QStringList &args, QString *out) {
            if (args.contains(QStringLiteral("-encoders"))) {
                if (out)
                    *out = QStringLiteral(" V....D libx264  H.264\n V....D h264_nvenc NVIDIA\n V....D hevc_nvenc NVIDIA\n"
                                          " V....D av1_nvenc NVIDIA\n V....D h264_vaapi VAAPI\n V....D hevc_vaapi VAAPI\n");
                return 0;
            }
            const int i = args.indexOf(QStringLiteral("-c:v"));
            const QString enc = i >= 0 ? args.value(i + 1) : QString();
            if (calls)
                calls->append(enc);
            return working.contains(enc) ? 0 : 1;
        };
    }

private Q_SLOTS:
    void presets()
    {
        const QList<ExportPreset> list = Exporter::presets();
        QVERIFY(list.size() >= 10);
        QCOMPARE(Exporter::defaultPresetId(), QStringLiteral("mp4-h264"));
        const ExportPreset mp4 = Exporter::preset(QStringLiteral("mp4-h264"));
        QCOMPARE(mp4.extension, QStringLiteral("mp4"));
        QVERIFY(mp4.args.contains(QStringLiteral("vcodec=libx264")));
        QVERIFY(mp4.args.contains(QStringLiteral("acodec=aac")));
        for (const char *id : {"youtube-1080p", "youtube-4k-hevc", "webm-vp9", "gif", "audio-mp3", "audio-wav", "prores-hq", "dnxhr-hq", "shorts-9x16", "av1"})
            QCOMPARE(Exporter::preset(QString::fromLatin1(id)).id, QString::fromLatin1(id));
    }

    void hardwareDetectionFallsBack()
    {
        QStringList calls;
        // nothing works: listed encoders are not trusted
        HwEncoders none = Exporter::detectHardware(QStringLiteral("ffmpeg"), fakeFfmpeg({}, &calls));
        QVERIFY(none.available.isEmpty());
        QVERIFY(calls.contains(QStringLiteral("h264_nvenc")));
        // NVENC H.264/HEVC work, AV1 does not (e.g. RTX 20xx)
        HwEncoders nv = Exporter::detectHardware(QStringLiteral("ffmpeg"), fakeFfmpeg({QStringLiteral("h264_nvenc"), QStringLiteral("hevc_nvenc")}));
        QVERIFY(nv.has(QStringLiteral("h264_nvenc")));
        QVERIFY(!nv.has(QStringLiteral("av1_nvenc")));
        // ffmpeg missing
        HwEncoders missing = Exporter::detectHardware(QStringLiteral("ffmpeg"), [](const QString &, const QStringList &, QString *) { return -1; });
        QVERIFY(missing.available.isEmpty());
    }

    void meltArguments()
    {
        ProjectSettings ps;
        Exporter::Settings s;
        s.output = QStringLiteral("/tmp/out.mp4");
        s.threads = 3;
        QString enc;

        // software fallback
        QStringList args = Exporter::meltArguments(QStringLiteral("/p.mlt"), s, HwEncoders{}, ps, &enc);
        QCOMPARE(enc, QStringLiteral("libx264"));
        QCOMPARE(args.at(0), QStringLiteral("-progress2"));
        QCOMPARE(args.at(1), QStringLiteral("/p.mlt"));
        QVERIFY(args.contains(QStringLiteral("avformat:/tmp/out.mp4")));
        QCOMPARE(value(args, QStringLiteral("crf")), QStringLiteral("20"));
        QCOMPARE(value(args, QStringLiteral("real_time")), QStringLiteral("-3"));

        // NVENC preferred when detected
        HwEncoders hw;
        hw.available = {QStringLiteral("h264_nvenc"), QStringLiteral("h264_vaapi")};
        args = Exporter::meltArguments(QStringLiteral("/p.mlt"), s, hw, ps, &enc);
        QCOMPARE(enc, QStringLiteral("h264_nvenc"));
        QCOMPARE(value(args, QStringLiteral("vcodec")), QStringLiteral("h264_nvenc"));
        QVERIFY(value(args, QStringLiteral("crf")).isEmpty());
        QCOMPARE(value(args, QStringLiteral("cq")), QStringLiteral("21"));

        // VA-API: device and qp
        hw.available = {QStringLiteral("hevc_vaapi")};
        s.presetId = QStringLiteral("youtube-4k-hevc");
        s.quality = 25;
        args = Exporter::meltArguments(QStringLiteral("/p.mlt"), s, hw, ps, &enc);
        QCOMPARE(enc, QStringLiteral("hevc_vaapi"));
        QCOMPARE(value(args, QStringLiteral("vaapi_device")), QStringLiteral("/dev/dri/renderD128"));
        QCOMPARE(value(args, QStringLiteral("qp")), QStringLiteral("25"));
        QCOMPARE(value(args, QStringLiteral("width")), QStringLiteral("3840"));

        // hardware off
        s.hardware = false;
        args = Exporter::meltArguments(QStringLiteral("/p.mlt"), s, hw, ps, &enc);
        QCOMPARE(enc, QStringLiteral("libx265"));
        QCOMPARE(value(args, QStringLiteral("crf")), QStringLiteral("25"));

        // GIF keeps aspect at 480 wide, 15 fps; range
        s.presetId = QStringLiteral("gif");
        s.in = 10;
        s.out = 40;
        args = Exporter::meltArguments(QStringLiteral("/p.mlt"), s, hw, ps, &enc);
        QCOMPARE(value(args, QStringLiteral("width")), QStringLiteral("480"));
        QCOMPARE(value(args, QStringLiteral("height")), QStringLiteral("270"));
        QCOMPARE(value(args, QStringLiteral("frame_rate_num")), QStringLiteral("15"));
        QVERIFY(args.contains(QStringLiteral("in=10")));
        QVERIFY(args.contains(QStringLiteral("out=40")));

        // audio only: no video encoder
        s.presetId = QStringLiteral("audio-mp3");
        args = Exporter::meltArguments(QStringLiteral("/p.mlt"), s, hw, ps, &enc);
        QVERIFY(enc.isEmpty());
        QCOMPARE(value(args, QStringLiteral("video_off")), QStringLiteral("1"));
    }

    void progress()
    {
        QCOMPARE(Exporter::parseProgress(QStringLiteral("Current Frame:         45, percentage:         37")), 37);
        QCOMPARE(Exporter::parseProgress(QStringLiteral("garbage")), -1);
    }

    void realRender()
    {
        if (!TestMedia::haveFfmpeg() || QStandardPaths::findExecutable(QStringLiteral("melt")).isEmpty())
            QSKIP("needs ffmpeg and melt");
        ProjectData data;
        data.settings.width = 640;
        data.settings.height = 360;
        MediaItem v;
        v.id = QStringLiteral("m1");
        QVERIFY(MediaProbe::probe(TestMedia::video(), data.settings, &v));
        data.media << v;
        data.doc.timeline = Timeline::defaultTimeline();
        Clip a;
        a.id = QStringLiteral("a");
        a.mediaId = v.id;
        a.out = 44;
        Clip b = a;
        b.id = QStringLiteral("b");
        b.position = 45;
        b.in = 60;
        b.out = 104;
        QVERIFY(Ops::insertClip(data.doc.timeline, 1, a, false));
        QVERIFY(Ops::insertClip(data.doc.timeline, 1, b, false));
        QVERIFY(Ops::setTransition(data.doc.timeline, QStringLiteral("b"), 10, QStringLiteral("dissolve")));
        const int frames = data.doc.timeline.duration();
        QCOMPARE(frames, 80);

        const QString project = QDir(TestMedia::dir()).filePath(QStringLiteral("export.mlt"));
        const QString out = QDir(TestMedia::dir()).filePath(QStringLiteral("export.mp4"));
        QFile::remove(out);
        QVERIFY(ProjectIO::save(project, data));
        Exporter::Settings s;
        s.output = out;
        s.hardware = false;
        QProcess melt;
        melt.setProcessChannelMode(QProcess::MergedChannels);
        melt.start(QStringLiteral("melt"), Exporter::meltArguments(project, s, HwEncoders{}, data.settings));
        QVERIFY(melt.waitForFinished(120000));
        const QString log = QString::fromUtf8(melt.readAll());
        QCOMPARE(melt.exitCode(), 0);
        QVERIFY(log.contains(QStringLiteral("percentage:")));

        QProcess probe;
        probe.start(QStringLiteral("ffprobe"), {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-count_frames"),
                                                QStringLiteral("-show_entries"), QStringLiteral("stream=codec_name,width,height,nb_read_frames"),
                                                QStringLiteral("-of"), QStringLiteral("json"), out});
        QVERIFY(probe.waitForFinished(30000));
        const QJsonArray streams = QJsonDocument::fromJson(probe.readAllStandardOutput()).object().value(QStringLiteral("streams")).toArray();
        QCOMPARE(streams.size(), 2);
        const QJsonObject vs = streams.at(0).toObject();
        QCOMPARE(vs.value(QStringLiteral("codec_name")).toString(), QStringLiteral("h264"));
        QCOMPARE(vs.value(QStringLiteral("width")).toInt(), 640);
        QCOMPARE(vs.value(QStringLiteral("nb_read_frames")).toString().toInt(), frames);
        QCOMPARE(streams.at(1).toObject().value(QStringLiteral("codec_name")).toString(), QStringLiteral("aac"));

        // other families, a 20-frame range each
        const struct { const char *preset; const char *vcodec; const char *acodec; } families[] = {
            {"webm-vp9", "vp9", "opus"}, {"prores-hq", "prores", "pcm_s16le"}, {"dnxhr-hq", "dnxhd", "pcm_s16le"},
            {"gif", "gif", nullptr}, {"audio-mp3", nullptr, "mp3"}, {"av1", "av1", "aac"}};
        for (const auto &fam : families) {
            Exporter::Settings fs;
            fs.presetId = QString::fromLatin1(fam.preset);
            fs.output = QDir(TestMedia::dir()).filePath(QStringLiteral("family-%1.%2").arg(fs.presetId, Exporter::preset(fs.presetId).extension));
            fs.hardware = false;
            fs.in = 0;
            fs.out = 19;
            QFile::remove(fs.output);
            QProcess m;
            m.setProcessChannelMode(QProcess::MergedChannels);
            m.start(QStringLiteral("melt"), Exporter::meltArguments(project, fs, HwEncoders{}, data.settings));
            QVERIFY(m.waitForFinished(120000));
            QVERIFY2(m.exitCode() == 0 && QFileInfo(fs.output).size() > 0, fam.preset);
            QProcess p2;
            p2.start(QStringLiteral("ffprobe"), {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
                                                 QStringLiteral("stream=codec_type,codec_name"), QStringLiteral("-of"), QStringLiteral("json"), fs.output});
            QVERIFY(p2.waitForFinished(30000));
            QString v, a;
            for (const QJsonValue &st : QJsonDocument::fromJson(p2.readAllStandardOutput()).object().value(QStringLiteral("streams")).toArray()) {
                const QJsonObject o = st.toObject();
                (o.value(QStringLiteral("codec_type")).toString() == QLatin1String("video") ? v : a) = o.value(QStringLiteral("codec_name")).toString();
            }
            QCOMPARE(v, fam.vcodec ? QString::fromLatin1(fam.vcodec) : QString());
            QCOMPARE(a, fam.acodec ? QString::fromLatin1(fam.acodec) : QString());
        }
    }
};

QTEST_MAIN(TestExport)
#include "tst_export.moc"
