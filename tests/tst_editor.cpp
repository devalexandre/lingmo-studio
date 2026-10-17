/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// The editor as the UI drives it: import, edit through undo commands,
// keyframes, compound clips, save/open, autosave recovery, playback,
// analysis tools and a real export through the render queue.
// Runs with a scratch HOME (XDG dirs in the build folder) and no audio device.

#include "editor.h"
#include "mediabin.h"
#include "player.h"
#include "renderqueue.h"
#include "projectio.h"
#include "testmedia.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <clocale>

using namespace Studio;

class TestEditor : public QObject
{
    Q_OBJECT

    Editor *m_editor = nullptr;
    QString m_video;

    int clipCount(const Timeline &tl) const
    {
        int n = 0;
        for (const Track &t : tl.tracks)
            n += t.clips.size();
        return n;
    }
    MediaBin *bin() const { return static_cast<MediaBin *>(m_editor->mediaBinObject()); }
    QString importOne(const QString &path)
    {
        const int before = bin()->count();
        m_editor->importPaths({path});
        if (!QTest::qWaitFor([&] { return bin()->count() == before + 1; }, 20000))
            return {};
        return m_editor->media().last().id;
    }

private Q_SLOTS:
    void initTestCase()
    {
        if (!TestMedia::haveFfmpeg())
            QSKIP("needs ffmpeg for test media");
        m_video = TestMedia::video();
        m_editor = new Editor;
        QVERIFY(m_editor->recoveryPath().isEmpty());
    }
    void cleanupTestCase()
    {
        delete m_editor;
    }

    void importEditUndo()
    {
        const QString id = importOne(m_video);
        QVERIFY(!id.isEmpty());
        QCOMPARE(m_editor->mediaItem(id)->kind, QStringLiteral("video"));
        // 4 s at 30 fps in a 30 fps project
        const QString clip = m_editor->addMediaToTimeline(id, -1, 0);
        QVERIFY(!clip.isEmpty());
        QCOMPARE(m_editor->currentTimeline().duration(), 120);
        QCOMPARE(m_editor->selection(), QStringList{clip});
        // the preview graph is rebuilt on the next event loop pass
        QTRY_COMPARE(m_editor->player()->duration(), 120);

        m_editor->player()->seek(40);
        QCOMPARE(m_editor->splitAtPlayhead(), 1);
        QCOMPARE(clipCount(m_editor->currentTimeline()), 2);
        QVERIFY(m_editor->canUndo());
        m_editor->undo();
        QCOMPARE(clipCount(m_editor->currentTimeline()), 1);
        m_editor->redo();
        QCOMPARE(clipCount(m_editor->currentTimeline()), 2);
        // ripple delete the right half, then take it back from the history list
        const QString right = m_editor->clipAt(1, 60); // V1
        QVERIFY(!right.isEmpty());
        m_editor->select(right);
        QVERIFY(m_editor->deleteSelected(true));
        QCOMPARE(m_editor->currentTimeline().duration(), 40);
        const int steps = m_editor->history().size();
        m_editor->setHistoryIndex(steps - 2);
        QCOMPARE(m_editor->currentTimeline().duration(), 120);
        m_editor->setHistoryIndex(steps - 1);
        QCOMPARE(m_editor->currentTimeline().duration(), 40);
        m_editor->undo();
        QCOMPARE(m_editor->currentTimeline().duration(), 120);
        QVERIFY(m_editor->dirty());
    }

    void effectsAndKeyframes()
    {
        const QString clip = m_editor->currentTimeline().tracks.at(1).clips.first().id;
        m_editor->addEffect(clip, QStringLiteral("transform"));
        m_editor->player()->seek(0);
        m_editor->toggleKeyframe(clip, 0, 0);
        QVERIFY(m_editor->hasKeyframeAt(clip, 0, 0));
        // with keyframes, changing a value at the playhead adds a keyframe there
        m_editor->player()->seek(30);
        QCOMPARE(m_editor->player()->timelinePosition(), 30);
        m_editor->setEffectParam(clip, 0, QStringLiteral("opacity"), 0.0);
        QVERIFY(m_editor->hasKeyframeAt(clip, 0, 30));
        QCOMPARE(m_editor->effectValuesAt(clip, 0, 15).value(QStringLiteral("opacity")).toDouble(), 50.0);
        // slider drags merge into one undo step
        const int before = m_editor->history().size();
        for (int i = 0; i < 5; ++i)
            m_editor->setEffectParam(clip, 0, QStringLiteral("scale"), 100.0 + i);
        QCOMPARE(m_editor->history().size(), before + 1);
        // a grade is copied and pasted between clips
        m_editor->addEffect(clip, QStringLiteral("wheels"));
        m_editor->setWheel(clip, QStringLiteral("gain"), 1.2, 1.0, 0.9);
        m_editor->copyGrade(clip);
        QVERIFY(m_editor->hasGrade());
        const QVariantMap info = m_editor->clipInfo(clip);
        QCOMPARE(info.value(QStringLiteral("effects")).toList().size(), 2);
        // before/after still renders
        const QImage before2 = m_editor->beforeAfterFrame(true, 320);
        QVERIFY(!before2.isNull());
    }

    void compoundClip()
    {
        const Timeline tl = m_editor->currentTimeline();
        m_editor->selectAll();
        QVERIFY(m_editor->createCompound());
        QCOMPARE(m_editor->document().sequences.size(), 1);
        QCOMPARE(m_editor->currentTimeline().tracks.at(1).clips.size() + m_editor->currentTimeline().tracks.at(0).clips.size(), 1);
        const QString comp = m_editor->selection().first();
        QVERIFY(m_editor->openCompound(comp));
        QCOMPARE(m_editor->sequencePath().size(), 2);
        QCOMPARE(m_editor->currentTimeline().duration(), tl.duration());
        m_editor->leaveCompound(0);
        QCOMPARE(m_editor->sequencePath().size(), 1);
        m_editor->undo();
        QCOMPARE(clipCount(m_editor->currentTimeline()), clipCount(tl));
    }

    void saveOpenAutosave()
    {
        const QString path = QDir(TestMedia::dir()).filePath(QStringLiteral("editor-test.mlt"));
        QFile::remove(path);
        QVERIFY(m_editor->saveProjectAs(QUrl::fromLocalFile(path)));
        QVERIFY(!m_editor->dirty());
        const Document saved = m_editor->document();
        QVERIFY(m_editor->openPath(path));
        QVERIFY(m_editor->document() == saved);
        QCOMPARE(m_editor->recentProjects().first(), path);

        // unsaved change + autosave, then a "crash" (no clean exit): the next
        // editor offers to recover it. (One player per process, like the app.)
        m_editor->addMarker(10, QStringLiteral("unsaved"));
        QVERIFY(m_editor->dirty());
        m_editor->autosave();
        delete m_editor;
        m_editor = new Editor;
        QVERIFY(!m_editor->recoveryPath().isEmpty());
        m_editor->recover();
        QCOMPARE(m_editor->projectPath(), path);
        QCOMPARE(m_editor->currentTimeline().markers.size(), 1);
        QVERIFY(m_editor->dirty());
        QVERIFY(m_editor->recoveryPath().isEmpty());
        QTRY_VERIFY(m_editor->player()->duration() > 1);
    }

    void playback()
    {
        m_editor->player()->seek(0);
        m_editor->player()->play();
        QTRY_VERIFY_WITH_TIMEOUT(m_editor->player()->position() > 10, 10000);
        // the master meter follows the sound (testsrc has a sine tone)
        QTRY_VERIFY_WITH_TIMEOUT(m_editor->player()->levelLeft() > 0.05, 5000);
        m_editor->player()->pause();
        QVERIFY(!m_editor->player()->playing());
        QVERIFY(!m_editor->player()->currentImage().isNull());
        // J/K/L
        m_editor->player()->shuttle(1);
        m_editor->player()->shuttle(1);
        QCOMPARE(m_editor->player()->speed(), 2.0);
        m_editor->player()->shuttle(0);
        QVERIFY(!m_editor->player()->playing());
    }

    void analysisTools()
    {
        const QString gaps = importOne(TestMedia::speech());
        QVERIFY(!gaps.isEmpty());
        int audioTrack = -1;
        for (int i = 0; i < m_editor->currentTimeline().tracks.size(); ++i)
            if (m_editor->currentTimeline().tracks.at(i).isAudio())
                audioTrack = i;
        const QString clip = m_editor->addMediaToTimeline(gaps, audioTrack, 0);
        QVERIFY(!clip.isEmpty());
        const int before = m_editor->currentTimeline().tracks.at(audioTrack).clips.size();
        m_editor->splitOnSilence(clip, -35, 0.6, true);
        QTRY_VERIFY_WITH_TIMEOUT(m_editor->busyTask().isEmpty() && m_editor->currentTimeline().tracks.at(audioTrack).clips.size() > before, 20000);
        // 6 s with 2 s of silence removed
        int end = 0;
        for (const Clip &c : m_editor->currentTimeline().tracks.at(audioTrack).clips)
            end = std::max(end, c.end());
        QVERIFY2(std::abs(end - 120) <= 3, qPrintable(QString::number(end)));

        const QString first = m_editor->currentTimeline().tracks.at(audioTrack).clips.first().id;
        m_editor->normalizeLoudness(first, -14);
        QTRY_VERIFY_WITH_TIMEOUT(!m_editor->clipInfo(first).value(QStringLiteral("effects")).toList().isEmpty(), 20000);
        const QVariantMap vol = m_editor->clipInfo(first).value(QStringLiteral("effects")).toList().last().toMap();
        QCOMPARE(vol.value(QStringLiteral("type")).toString(), QStringLiteral("volume"));
        QVERIFY(vol.value(QStringLiteral("params")).toMap().value(QStringLiteral("gain")).toDouble() != 0.0);

        // subtitles in and out
        const QString srt = QDir(TestMedia::dir()).filePath(QStringLiteral("in.srt"));
        QFile f(srt);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("1\n00:00:00,500 --> 00:00:01,500\nOlá\n\n2\n00:00:02,000 --> 00:00:03,000\nmundo\n");
        f.close();
        m_editor->importSubtitles(QUrl::fromLocalFile(srt));
        QCOMPARE(m_editor->currentTimeline().tracks.first().kind, TrackKind::Subtitle);
        QCOMPARE(m_editor->currentTimeline().tracks.first().clips.size(), 2);
        const QString out = QDir(TestMedia::dir()).filePath(QStringLiteral("out.srt"));
        QVERIFY(m_editor->exportSubtitles(QUrl::fromLocalFile(out)));
        QFile o(out);
        QVERIFY(o.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(o.readAll());
        QVERIFY(text.contains(QStringLiteral("00:00:00,500 --> 00:00:01,500")));
        QVERIFY(text.contains(QStringLiteral("mundo")));
        m_editor->searchTranscripts(QStringLiteral("MUNDO"));
        QCOMPARE(m_editor->searchResults().size(), 1);
    }

    void exportThroughQueue()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("melt")).isEmpty())
            QSKIP("needs melt");
        auto *queue = static_cast<RenderQueue *>(m_editor->renderQueueObject());
        QTRY_VERIFY_WITH_TIMEOUT(m_editor->hwDetected(), 60000);
        const QString out = QDir(TestMedia::dir()).filePath(QStringLiteral("queue-export.mp4"));
        QFile::remove(out);
        m_editor->queueExport(QStringLiteral("mp4-h264"), QUrl::fromLocalFile(out), true, -1, false);
        QCOMPARE(queue->count(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!queue->running(), 180000);
        const QModelIndex idx = queue->index(0);
        QCOMPARE(idx.data(RenderQueue::StateRole).toString(), QStringLiteral("done"));
        QProcess probe;
        probe.start(QStringLiteral("ffprobe"), {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
                                                QStringLiteral("stream=codec_name,width,height:format=duration"), QStringLiteral("-of"), QStringLiteral("json"), out});
        QVERIFY(probe.waitForFinished(30000));
        const QJsonObject json = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
        const QJsonArray streams = json.value(QStringLiteral("streams")).toArray();
        QCOMPARE(streams.size(), 2);
        QCOMPARE(streams.at(0).toObject().value(QStringLiteral("codec_name")).toString(), QStringLiteral("h264"));
        QCOMPARE(streams.at(0).toObject().value(QStringLiteral("width")).toInt(), 1920);
        const double duration = json.value(QStringLiteral("format")).toObject().value(QStringLiteral("duration")).toString().toDouble();
        QVERIFY2(std::abs(duration - m_editor->currentTimeline().duration() / 30.0) < 0.2, qPrintable(QString::number(duration)));
        qInfo() << "exported with" << idx.data(RenderQueue::EncoderRole).toString();
    }

    void gpuFailureFallsBackToCpu()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("melt")).isEmpty())
            QSKIP("needs melt");
        // pretend VA-API works; on a machine without it melt fails and the
        // queue retries with libx264 (on one with it, the GPU render succeeds)
        const QString project = QDir(TestMedia::dir()).filePath(QStringLiteral("fallback.mlt"));
        QVERIFY(ProjectIO::save(project, m_editor->projectData()));
        HwEncoders fake;
        fake.available = {QStringLiteral("h264_vaapi")};
        fake.vaapiDevice = QStringLiteral("/nonexistent/renderD128");
        Exporter::Settings s;
        s.output = QDir(TestMedia::dir()).filePath(QStringLiteral("fallback.mp4"));
        s.in = 0;
        s.out = 14;
        QFile::remove(s.output);
        RenderQueue queue;
        RenderQueue::Job job;
        job.name = QStringLiteral("fallback");
        job.output = s.output;
        job.projectFile = QDir(TestMedia::dir()).filePath(QStringLiteral("fallback-copy.mlt"));
        QFile::copy(project, job.projectFile);
        job.args = Exporter::meltArguments(project, s, fake, m_editor->settings(), &job.encoder);
        QCOMPARE(job.encoder, QStringLiteral("h264_vaapi"));
        s.hardware = false;
        job.fallbackArgs = Exporter::meltArguments(project, s, fake, m_editor->settings(), &job.fallbackEncoder);
        queue.add(job);
        QTRY_VERIFY_WITH_TIMEOUT(!queue.running() && queue.index(0).data(RenderQueue::StateRole).toString() != QLatin1String("queued"), 120000);
        QCOMPARE(queue.index(0).data(RenderQueue::StateRole).toString(), QStringLiteral("done"));
        QCOMPARE(queue.index(0).data(RenderQueue::EncoderRole).toString(), QStringLiteral("libx264"));
        QVERIFY(QFileInfo(s.output).size() > 0);
    }

    void proxies()
    {
        // a short 4K clip gets a 540p proxy automatically
        const QString uhd = QDir(TestMedia::dir()).filePath(QStringLiteral("uhd.mp4"));
        if (!QFileInfo::exists(uhd))
            QVERIFY(TestMedia::ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc2=s=3840x2160:r=30:d=1"),
                                       QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                                       QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), uhd}));
        QVERIFY(m_editor->autoProxy());
        const QString id = importOne(uhd);
        QVERIFY(!id.isEmpty());
        QVERIFY(m_editor->mediaItem(id)->needsProxy());
        QTRY_VERIFY_WITH_TIMEOUT(!m_editor->mediaItem(id)->proxyPath.isEmpty(), 60000);
        QProcess probe;
        probe.start(QStringLiteral("ffprobe"), {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                                                QStringLiteral("-show_entries"), QStringLiteral("stream=height"), QStringLiteral("-of"), QStringLiteral("csv=p=0"),
                                                m_editor->mediaItem(id)->proxyPath});
        QVERIFY(probe.waitForFinished(20000));
        QCOMPARE(probe.readAllStandardOutput().trimmed(), QByteArray("540"));
        // project files keep the original
        QVERIFY(!m_editor->appendMedia(id).isEmpty());
        const QByteArray xml = ProjectIO::toXml(m_editor->projectData());
        QVERIFY(!xml.contains("name=\"resource\">" + m_editor->mediaItem(id)->proxyPath.toUtf8()));
        QVERIFY(xml.contains("name=\"resource\">" + uhd.toUtf8()));
    }

    void reframeVertical()
    {
        m_editor->smartReframe(QStringLiteral("9:16"));
        QCOMPARE(m_editor->projectWidth(), 1080);
        QCOMPARE(m_editor->projectHeight(), 1920);
        bool found = false;
        for (const Track &t : m_editor->currentTimeline().tracks)
            for (const Clip &c : t.clips)
                for (const Effect &e : c.effects)
                    if (e.type == QLatin1String("transform") && c.kind == QLatin1String("media")) {
                        // 16:9 into 9:16: fill = (16/9)^2 = 316 %
                        QVERIFY(std::abs(e.params.value(QStringLiteral("scale")).toDouble() - 316.0) < 1.0);
                        found = true;
                    }
        QVERIFY(found);
    }

    void templates()
    {
        m_editor->newFromTemplate(QStringLiteral("shorts"));
        QCOMPARE(m_editor->projectWidth(), 1080);
        QCOMPARE(m_editor->projectHeight(), 1920);
        QCOMPARE(m_editor->currentTimeline().tracks.first().kind, TrackKind::Subtitle);
        QVERIFY(!m_editor->canUndo());
        m_editor->newFromTemplate(QStringLiteral("youtube"));
        QCOMPARE(m_editor->projectHeight(), 1080);
        QCOMPARE(clipCount(m_editor->currentTimeline()), 1); // the intro title
    }
};

int main(int argc, char **argv)
{
    // never touch the user's settings, autosaves or caches
    const QByteArray scratch = QByteArray(TEST_SCRATCH) + "/editor-home";
    QDir(QString::fromUtf8(scratch)).removeRecursively();
    QDir().mkpath(QString::fromUtf8(scratch));
    qputenv("HOME", scratch);
    qputenv("XDG_CONFIG_HOME", scratch + "/config");
    qputenv("XDG_DATA_HOME", scratch + "/data");
    qputenv("XDG_CACHE_HOME", scratch + "/cache");
    qputenv("SDL_AUDIODRIVER", "dummy");
    QGuiApplication app(argc, argv);
    std::setlocale(LC_NUMERIC, "C");
    app.setOrganizationName(QStringLiteral("lingmoos"));
    app.setApplicationName(QStringLiteral("lingmo-studio-test"));
    TestEditor t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_editor.moc"
