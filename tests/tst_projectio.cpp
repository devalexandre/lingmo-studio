/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// MLT XML save/load round trip, the live MLT build (frames really render),
// foreign MLT files, and media probing.

#include "effects.h"
#include "mediaprobe.h"
#include "mltbuilder.h"
#include "mltxml.h"
#include "projectio.h"
#include "testmedia.h"
#include "timelineops.h"

#include <Mlt.h>
#include <QFile>
#include <QJsonDocument>
#include <QTest>
#include <clocale>

using namespace Studio;

class TestProjectIO : public QObject
{
    Q_OBJECT

    ProjectData m_data;
    QString m_video;

    static Clip clipOf(const MediaItem &m, int pos, int in, int out)
    {
        Clip c;
        c.id = newId(QStringLiteral("c"));
        c.mediaId = m.id;
        c.position = pos;
        c.in = in;
        c.out = out;
        return c;
    }

private Q_SLOTS:
    void initTestCase()
    {
        setlocale(LC_NUMERIC, "C"); // as the app does: MLT/ffmpeg parse numbers with strtod
        if (!TestMedia::haveFfmpeg())
            QSKIP("ffmpeg is needed to make test media");
        m_video = TestMedia::video();
        QVERIFY(QFileInfo::exists(m_video));

        m_data.settings = ProjectSettings::preset(QStringLiteral("720p30"));
        m_data.settings.width = 640;
        m_data.settings.height = 360;
        MediaItem v;
        v.id = QStringLiteral("m-video");
        QString error;
        QVERIFY2(MediaProbe::probe(m_video, m_data.settings, &v, &error), qPrintable(error));
        QCOMPARE(v.kind, QStringLiteral("video"));
        QCOMPARE(v.length, 120);
        QCOMPARE(v.width, 640);
        QCOMPARE(v.videoCodec, QStringLiteral("h264"));
        QCOMPARE(v.audioCodec, QStringLiteral("aac"));
        QVERIFY(v.hasAudio);
        m_data.media << v;

        Timeline &tl = m_data.doc.timeline;
        tl = Timeline::defaultTimeline();
        Clip a = clipOf(v, 0, 0, 59);
        Effect color = EffectRegistry::create(QStringLiteral("color"));
        color.params[QStringLiteral("saturation")] = 1.5;
        a.effects << color;
        Effect tf = EffectRegistry::create(QStringLiteral("transform"));
        Ops::setKeyframe(tf.keyframes, 0, {{QStringLiteral("scale"), 100.0}, {QStringLiteral("opacity"), 100.0}, {QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}, {QStringLiteral("rotation"), 0.0}});
        Ops::setKeyframe(tf.keyframes, 59, {{QStringLiteral("scale"), 50.0}, {QStringLiteral("opacity"), 100.0}, {QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}, {QStringLiteral("rotation"), 0.0}});
        a.effects << tf;
        a.fadeIn = 10;
        QVERIFY(Ops::insertClip(tl, 1, a, false));
        Clip b = clipOf(v, 60, 30, 89);
        QVERIFY(Ops::insertClip(tl, 1, b, false));
        QVERIFY(Ops::setTransition(tl, b.id, 15, QStringLiteral("wipe-left")));
        Clip fast = clipOf(v, 105, 0, 29);
        fast.speed = 2.0;
        QVERIFY(Ops::insertClip(tl, 1, fast, false));

        Clip title;
        title.id = newId(QStringLiteral("c"));
        title.kind = QStringLiteral("title");
        title.position = 20;
        title.in = 0;
        title.out = 49;
        title.fadeIn = 10;
        title.fadeOut = 10;
        Effect text = EffectRegistry::create(QStringLiteral("text"));
        text.params[QStringLiteral("text")] = QStringLiteral("Olá <Studio> & \"amigos\"");
        title.effects << text;
        QVERIFY(Ops::insertClip(tl, 0, title, false));

        Clip music = clipOf(v, 10, 0, 99);
        Effect vol = EffectRegistry::create(QStringLiteral("volume"));
        Ops::setKeyframe(vol.keyframes, 0, {{QStringLiteral("gain"), -10.0}});
        Ops::setKeyframe(vol.keyframes, 99, {{QStringLiteral("gain"), 0.0}});
        music.effects << vol;
        QVERIFY(Ops::insertClip(tl, 2, music, false));
        tl.tracks[2].volumeDb = -3;
        tl.tracks[2].pan = 0.25;
        tl.tracks[3].muted = true;
        tl.tracks[0].locked = true;
        tl.markers << Marker{30, QStringLiteral("Intro"), QStringLiteral("green")} << Marker{90, QStringLiteral("Cut"), QStringLiteral("red")};

        // a nested sequence used as a compound clip
        Sequence seq;
        seq.id = QStringLiteral("s1");
        seq.name = QStringLiteral("Compound 1");
        seq.timeline = Timeline::defaultTimeline();
        QVERIFY(Ops::insertClip(seq.timeline, 1, clipOf(v, 0, 0, 29), false));
        m_data.doc.sequences << seq;
        Clip compound;
        compound.id = newId(QStringLiteral("c"));
        compound.kind = QStringLiteral("compound");
        compound.mediaId = seq.id;
        compound.position = 80;
        compound.in = 0;
        compound.out = 29;
        tl.tracks[0].locked = false;
        QVERIFY(Ops::insertClip(tl, 0, compound, false));
        tl.tracks[0].locked = true;
    }

    void roundTrip()
    {
        const QString path = QDir(TestMedia::dir()).filePath(QStringLiteral("roundtrip.mlt"));
        QString error;
        QVERIFY2(ProjectIO::save(path, m_data, &error), qPrintable(error));
        ProjectData loaded;
        QVERIFY2(ProjectIO::load(path, &loaded, &error), qPrintable(error));
        QVERIFY(loaded.settings == m_data.settings);
        QCOMPARE(loaded.media.size(), 1);
        QCOMPARE(loaded.media.first().toJson(), m_data.media.first().toJson());
        QCOMPARE(loaded.doc.timeline.markers.size(), 2);
        QCOMPARE(loaded.doc.timeline.markers.at(1).name, QStringLiteral("Cut"));
        QCOMPARE(loaded.doc.timeline.tracks.size(), m_data.doc.timeline.tracks.size());
        for (int t = 0; t < m_data.doc.timeline.tracks.size(); ++t) {
            const Track &a = m_data.doc.timeline.tracks.at(t);
            const Track &b = loaded.doc.timeline.tracks.at(t);
            QCOMPARE(b.id, a.id);
            QCOMPARE(b.name, a.name);
            QCOMPARE(b.clips.size(), a.clips.size());
            for (int c = 0; c < a.clips.size(); ++c)
                QCOMPARE(QJsonDocument(b.clips.at(c).toFullJson()).toJson(), QJsonDocument(a.clips.at(c).toFullJson()).toJson());
            QVERIFY(a == b);
        }
        QVERIFY(loaded.doc == m_data.doc);
        // Saving again gives the same file
        QCOMPARE(ProjectIO::toXml(loaded, QStringLiteral("roundtrip")), ProjectIO::toXml(m_data, QStringLiteral("roundtrip")));
    }

    void liveBuildRendersFrames()
    {
        initMlt();
        Mlt::Profile profile;
        profile.set_width(640);
        profile.set_height(360);
        profile.set_frame_rate(30, 1);
        profile.set_sample_aspect(1, 1);
        profile.set_display_aspect(16, 9);
        profile.set_progressive(1);
        profile.set_explicit(1);
        MltBuilder builder(profile);
        Planner planner(m_data.settings, m_data.media, m_data.doc.sequences, {});
        const RenderPlan plan = planner.plan(m_data.doc.timeline);
        std::unique_ptr<Mlt::Tractor> tractor = builder.build(plan);
        QVERIFY(tractor && tractor->is_valid());
        QVERIFY2(builder.errors().isEmpty(), qPrintable(builder.errors().join(QLatin1Char('\n'))));
        QCOMPARE(tractor->get_length(), m_data.doc.timeline.duration());
        for (int pos : {0, 30, 50, 100, 110, 125}) {
            tractor->seek(pos);
            std::unique_ptr<Mlt::Frame> frame(tractor->get_frame());
            QVERIFY(frame);
            mlt_image_format fmt = mlt_image_rgba;
            int w = 320, h = 180;
            const uint8_t *img = frame->get_image(fmt, w, h);
            QVERIFY2(img, qPrintable(QStringLiteral("no image at %1").arg(pos)));
            QCOMPARE(w, 320);
            mlt_audio_format afmt = mlt_audio_s16;
            int freq = 48000, ch = 2, samples = mlt_audio_calculate_frame_samples(30, 48000, pos);
            QVERIFY(frame->get_audio(afmt, freq, ch, samples));
        }
        // Frame 0 fades in from black; frame 30 is the testsrc pattern
        auto brightness = [&](int pos) {
            tractor->seek(pos);
            std::unique_ptr<Mlt::Frame> frame(tractor->get_frame());
            mlt_image_format fmt = mlt_image_rgba;
            int w = 64, h = 36;
            const uint8_t *img = frame->get_image(fmt, w, h);
            qint64 sum = 0;
            for (int i = 0; i < w * h * 4; i += 4)
                sum += img[i] + img[i + 1] + img[i + 2];
            return sum / (w * h);
        };
        QVERIFY(brightness(0) < 30);
        QVERIFY(brightness(30) > 100);
    }

    void everyEffectBuilds()
    {
        // one clip per registered effect, plus a speed ramp and a freeze frame:
        // every MLT service must exist and every frame must render
        initMlt();
        EffectRegistry::setAvailableServices(availableMltServices());
        ProjectData d = m_data;
        Timeline &tl = d.doc.timeline;
        tl = Timeline::defaultTimeline();
        int pos = 0;
        for (const EffectDef &def : EffectRegistry::all()) {
            QVERIFY2(EffectRegistry::available(def), qPrintable(def.id));
            Clip c = clipOf(m_data.media.first(), pos, 0, 9);
            Effect e = EffectRegistry::create(def.id);
            if (def.id == QLatin1String("lut")) {
                const QString cube = QDir(TestMedia::dir()).filePath(QStringLiteral("identity.cube"));
                QFile f(cube);
                QVERIFY(f.open(QIODevice::WriteOnly));
                f.write("LUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n");
                f.close();
                e.params[QStringLiteral("file")] = cube;
            }
            if (def.id == QLatin1String("curves"))
                e.params[QStringLiteral("master")] = QStringLiteral("0/0 0.5/0.6 1/1");
            c.effects << e;
            QVERIFY(Ops::insertClip(tl, def.video ? 1 : 2, c, false));
            pos += 10;
        }
        Clip ramp = clipOf(m_data.media.first(), pos, 0, 29);
        Ops::setKeyframe(ramp.speedRamp, 0, {{QStringLiteral("speed"), 0.5}});
        Ops::setKeyframe(ramp.speedRamp, 29, {{QStringLiteral("speed"), 2.0}});
        QVERIFY(Ops::insertClip(tl, 1, ramp, false));
        Clip still = clipOf(m_data.media.first(), pos + 30, 0, 59);
        QVERIFY(Ops::insertClip(tl, 1, still, false));
        const QString freeze = Ops::freezeFrame(tl, still.id, pos + 40, 20);
        QVERIFY(!freeze.isEmpty());

        Mlt::Profile profile;
        profile.set_width(640);
        profile.set_height(360);
        profile.set_frame_rate(30, 1);
        profile.set_sample_aspect(1, 1);
        profile.set_display_aspect(16, 9);
        profile.set_progressive(1);
        profile.set_explicit(1);
        MltBuilder builder(profile);
        std::unique_ptr<Mlt::Tractor> tractor = builder.build(Planner(d.settings, d.media, d.doc.sequences, {}).plan(tl));
        QVERIFY2(builder.errors().isEmpty(), qPrintable(builder.errors().join(QLatin1Char('\n'))));
        auto frameAt = [&](int f) {
            tractor->seek(f);
            std::unique_ptr<Mlt::Frame> frame(tractor->get_frame());
            mlt_image_format fmt = mlt_image_rgba;
            int w = 160, h = 90;
            const uint8_t *img = frame->get_image(fmt, w, h);
            return img ? QImage(img, w, h, QImage::Format_RGBA8888).copy() : QImage();
        };
        for (int f = 0; f < tl.duration(); f += 5)
            QVERIFY2(!frameAt(f).isNull(), qPrintable(QString::number(f)));
        // the freeze frame holds one picture (testsrc changes every frame)
        const int fp = tl.clip(freeze)->position;
        QCOMPARE(frameAt(fp + 2), frameAt(fp + 15));
        QVERIFY(frameAt(fp - 5) != frameAt(fp - 1));

        // the plan survives the XML round trip with the ramp
        ProjectData loaded;
        QString error;
        QVERIFY2(ProjectIO::loadXml(ProjectIO::toXml(d), &loaded, &error), qPrintable(error));
        QVERIFY(loaded.doc == d.doc);
    }

    void foreignProject()
    {
        // A plain MLT file without studio:* properties (as other tools write)
        PlanOptions opts;
        opts.storeModel = false;
        Planner planner(m_data.settings, m_data.media, m_data.doc.sequences, opts);
        const QByteArray xml = writeMltXml(planner.plan(m_data.doc.timeline));
        QVERIFY(!xml.contains("studio:"));
        ProjectData loaded;
        QString error;
        QVERIFY2(ProjectIO::loadXml(xml, &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.media.size(), 1);
        QCOMPARE(loaded.media.first().path, m_video);
        int clips = 0, orig = 0;
        for (const Track &t : loaded.doc.timeline.tracks)
            clips += t.clips.size();
        for (const Track &t : m_data.doc.timeline.tracks)
            for (const Clip &c : t.clips)
                orig += c.kind == QLatin1String("compound") ? 0 : 1; // nested timelines need studio data
        QCOMPARE(clips, orig);
        QCOMPARE(loaded.doc.timeline.tracks.size(), m_data.doc.timeline.tracks.size());
        // the dissolve survives as a transition
        bool sawTransition = false;
        for (const Track &t : loaded.doc.timeline.tracks)
            for (const Clip &c : t.clips)
                sawTransition |= c.transitionIn == 15;
        QVERIFY(sawTransition);
    }

    void thumbnails()
    {
        const QImage img = MediaProbe::thumbnail(m_video, 10, QSize(160, 160), m_data.settings);
        QVERIFY(!img.isNull());
        QCOMPARE(img.width(), 160);
        QCOMPARE(img.height(), 90);
        const QList<float> peaks = MediaProbe::audioPeaks(m_video, 50);
        QCOMPARE(peaks.size(), 50);
        QVERIFY(peaks.at(25) > 0.05f);
    }

    void unsupported()
    {
        MediaItem m;
        QVERIFY(!MediaProbe::probe(QStringLiteral("/nonexistent/file.mp4"), m_data.settings, &m));
        QVERIFY(MediaProbe::isSupported(QStringLiteral("a.MP4")));
        QVERIFY(MediaProbe::isSupported(QStringLiteral("a.mkv")));
        QVERIFY(!MediaProbe::isSupported(QStringLiteral("a.txt")));
    }
};

QTEST_MAIN(TestProjectIO)
#include "tst_projectio.moc"
