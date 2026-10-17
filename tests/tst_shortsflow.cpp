/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// "Gerar cortes" through the editor controller, as the QML page drives it:
// candidates → sequences with reframe and captions → subject keyframes →
// batch export. With STUDIO_E2E_MEDIA set to a video with speech, the whole
// chain runs for real (whisper, ffmpeg analysis, Ollama when configured,
// OpenCV tracking, melt renders). Runs with a scratch HOME.

#include "captions.h"
#include "editor.h"
#include "imageproviders.h"
#include "mediabin.h"
#include "platforms.h"
#include "player.h"
#include "reframe.h"
#include "renderqueue.h"
#include "shorts.h"
#include "testmedia.h"
#include "transcriber.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>
#include <clocale>

using namespace Studio;

static QJsonObject probe(const QString &file)
{
    QProcess p;
    p.start(QStringLiteral("ffprobe"), {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
                                        QStringLiteral("stream=codec_type,width,height:format=duration"), QStringLiteral("-of"), QStringLiteral("json"), file});
    p.waitForFinished(30000);
    return QJsonDocument::fromJson(p.readAllStandardOutput()).object();
}

class TestShortsFlow : public QObject
{
    Q_OBJECT
    Editor *m_editor = nullptr;

    MediaBin *bin() const { return static_cast<MediaBin *>(m_editor->mediaBinObject()); }
    ShortsGenerator *shorts() const { return static_cast<ShortsGenerator *>(m_editor->shortsObject()); }
    QString importOne(const QString &path)
    {
        const int before = bin()->count();
        m_editor->importPaths({path});
        if (!QTest::qWaitFor([&] { return bin()->count() == before + 1; }, 30000))
            return {};
        return m_editor->media().last().id;
    }

private Q_SLOTS:
    void initTestCase()
    {
        if (!TestMedia::haveFfmpeg())
            QSKIP("needs ffmpeg");
        Platforms::setAll(Platforms::load(QStringLiteral(TEST_DATA "/platforms.json")));
        QVERIFY(!Platforms::all().isEmpty());
        m_editor = new Editor;
    }
    void cleanupTestCase()
    {
        delete m_editor;
    }

    // The gallery previews: every preset renders text through libass
    void stylePreviews()
    {
        for (const QVariant &v : m_editor->captionStyles()) {
            const QVariantMap style = v.toMap();
            int painted = 0;
            for (qint64 ms : {300, 1500}) {
                const QImage img = CaptionPreviewProvider::render(style, ms, QSize(252, 448), true);
                QCOMPARE(img.size(), QSize(252, 448));
                for (int y = 0; y < img.height(); ++y)
                    for (int x = 0; x < img.width(); ++x)
                        painted += qAlpha(img.pixel(x, y)) > 128;
            }
            QVERIFY2(painted > 200, qPrintable(style.value(QStringLiteral("id")).toString()));
        }
    }

    void cutsFromAnInjectedTranscript()
    {
        // 30 s 16:9 video with sound
        const QString path = QDir(TestMedia::dir()).filePath(QStringLiteral("long.mp4"));
        if (!QFileInfo::exists(path))
            QVERIFY(TestMedia::ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc2=s=640x360:r=30:d=30"),
                                       QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=f=300:d=30"),
                                       QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                                       QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-shortest"), path}));
        const QString id = importOne(path);
        QVERIFY(!id.isEmpty());
        Captions::Transcript t;
        t.language = QStringLiteral("pt");
        qint64 ms = 500;
        for (int s = 0; s < 8; ++s) {
            for (int w = 0; w < 8; ++w) {
                QString word = s == 2 && w == 0 ? QStringLiteral("Você") : QStringLiteral("palavra%1").arg(w);
                if (w == 7)
                    word += s == 2 ? QStringLiteral("?") : QStringLiteral(".");
                t.words.append({ms, ms + 300, word});
                ms += 380;
            }
            ms += 450;
        }
        shorts()->setTestData(t, {});
        shorts()->setMediaId(id);
        shorts()->setSelectedPlatforms({QStringLiteral("tiktok"), QStringLiteral("linkedin-square")});
        QVERIFY(shorts()->mixedAspects());
        shorts()->setCount(1);
        shorts()->setCaptionStyle(QStringLiteral("karaoke"));
        QCOMPARE(shorts()->minSeconds(), 30); // both platforms: LinkedIn wants 30-90, TikTok 21-60
        shorts()->setSelectedPlatforms({QStringLiteral("tiktok")});
        QSignalSpy done(shorts(), &ShortsGenerator::analysisFinished);
        shorts()->analyze();
        QVERIFY(done.count() == 1 || done.wait(30000));
        QCOMPARE(shorts()->candidates().size(), 1);
        const QVariantMap cand = shorts()->candidates().first().toMap();
        QVERIFY(cand.value(QStringLiteral("seconds")).toDouble() >= 21);
        QVERIFY(!cand.value(QStringLiteral("title")).toString().isEmpty());
        QVERIFY(cand.value(QStringLiteral("thumbnail")).toString().startsWith(QLatin1String("image://thumb/")));

        // trimming snaps to word edges
        const qint64 end0 = cand.value(QStringLiteral("endMs")).toLongLong();
        shorts()->trim(cand.value(QStringLiteral("id")).toString(), false, -1000);
        const qint64 end1 = shorts()->candidates().first().toMap().value(QStringLiteral("endMs")).toLongLong();
        QVERIFY(end1 < end0);
        bool onWordEdge = false;
        for (const Captions::Word &w : t.words)
            onWordEdge |= w.endMs + 400 == end1;
        QVERIFY(onWordEdge);

        QCOMPARE(shorts()->createCuts(), 1);
        // the project took TikTok's size
        QCOMPARE(m_editor->projectWidth(), 1080);
        QCOMPARE(m_editor->projectHeight(), 1920);
        const QVariantList seqs = m_editor->shortSequences();
        QCOMPARE(seqs.size(), 1);
        const QString seqId = seqs.first().toMap().value(QStringLiteral("id")).toString();
        const Sequence *seq = nullptr;
        for (const Sequence &s : m_editor->document().sequences)
            if (s.id == seqId)
                seq = &s;
        QVERIFY(seq);
        // a caption track with the karaoke style and TikTok's safe area, and the reframed video
        QCOMPARE(seq->timeline.tracks.first().kind, TrackKind::Subtitle);
        const Captions::Style st = Captions::Style::fromMap(seq->timeline.tracks.first().captionStyle);
        QCOMPARE(st.effect, QStringLiteral("karaoke"));
        QCOMPARE(st.safeArea.bottom, Platforms::find(QStringLiteral("tiktok")).safeArea.bottom);
        QVERIFY(!seq->timeline.tracks.first().clips.isEmpty());
        const Clip *video = nullptr;
        for (const Track &tr : seq->timeline.tracks)
            for (const Clip &c : tr.clips)
                if (c.kind == QLatin1String("media"))
                    video = &c;
        QVERIFY(video);
        QCOMPARE(video->effects.first().type, QStringLiteral("transform"));
        QVERIFY(std::abs(video->effects.first().params.value(QStringLiteral("scale")).toDouble() - 316.0) < 1);
        // the main timeline shows the cut as a compound clip with a marker
        bool compound = false;
        for (const Track &tr : m_editor->currentTimeline().tracks)
            for (const Clip &c : tr.clips)
                compound |= c.kind == QLatin1String("compound") && c.mediaId == seqId;
        QVERIFY(compound);
        QCOMPARE(m_editor->currentTimeline().markers.size(), 1);

        // subject path → Position X keyframes (left, then moving right)
        QList<Reframe::Sample> samples;
        for (int i = 0; i < 80; ++i)
            samples.append({i * 0.25, i < 40 ? 0.3 : 0.7, 1});
        m_editor->applyReframe(seqId, video->id, samples);
        for (const Sequence &s : m_editor->document().sequences)
            if (s.id == seqId)
                seq = &s;
        for (const Track &tr : seq->timeline.tracks)
            for (const Clip &c : tr.clips)
                if (c.kind == QLatin1String("media"))
                    video = &c;
        const QList<Keyframe> &kfs = video->effects.first().keyframes;
        QVERIFY(kfs.size() >= 2);
        QVERIFY(kfs.first().values.value(QStringLiteral("x")).toDouble() > 300);
        QVERIFY(kfs.last().values.value(QStringLiteral("x")).toDouble() < -300);
        QVERIFY(!video->genParams.value(QStringLiteral("reframePath")).toList().isEmpty());
        // undo removes the keyframes in one step
        m_editor->undo();
        for (const Sequence &s : m_editor->document().sequences)
            if (s.id == seqId)
                for (const Track &tr : s.timeline.tracks)
                    for (const Clip &c : tr.clips)
                        if (c.kind == QLatin1String("media"))
                            QVERIFY(c.effects.first().keyframes.isEmpty());
        m_editor->redo();
    }

    void batchExport()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("melt")).isEmpty())
            QSKIP("needs melt");
        // add LinkedIn 1:1 to the cut: a second file, reframed for a square
        Document d = m_editor->document();
        for (Sequence &s : d.sequences)
            if (!s.info.isEmpty())
                s.info[QStringLiteral("platforms")] = QStringList{QStringLiteral("tiktok"), QStringLiteral("linkedin-square")};
        m_editor->restoreDocument(d);
        const QString out = QDir(TestMedia::dir()).filePath(QStringLiteral("cuts"));
        QDir(out).removeRecursively();
        auto *queue = static_cast<RenderQueue *>(m_editor->renderQueueObject());
        QSignalSpy finished(queue, &RenderQueue::jobFinished);
        m_editor->exportShorts(QUrl::fromLocalFile(out));
        QCOMPARE(queue->count(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 240000);
        const QStringList files = QDir(out).entryList({QStringLiteral("*.mp4")}, QDir::Files, QDir::Name);
        QCOMPARE(files.size(), 2);
        QVERIFY(QDir(out).entryList({QStringLiteral("*.srt")}).size() == 2);
        for (const QString &f : files) {
            const QJsonObject info = probe(QDir(out).filePath(f));
            const QJsonObject v = info.value(QStringLiteral("streams")).toArray().first().toObject();
            if (f.contains(QLatin1String("tiktok"))) {
                QCOMPARE(v.value(QStringLiteral("width")).toInt(), 1080);
                QCOMPARE(v.value(QStringLiteral("height")).toInt(), 1920);
            } else {
                QCOMPARE(v.value(QStringLiteral("width")).toInt(), 1080);
                QCOMPARE(v.value(QStringLiteral("height")).toInt(), 1080);
            }
            QVERIFY(info.value(QStringLiteral("format")).toObject().value(QStringLiteral("duration")).toString().toDouble() > 15);
        }
    }

    // The real chain: STUDIO_E2E_MEDIA=<video with speech>; whisper-cli and a
    // model in ~/.local/share/lingmo/whisper-models; Ollama optional.
    void realVideo()
    {
        const QString media = qEnvironmentVariable("STUDIO_E2E_MEDIA");
        if (media.isEmpty())
            QSKIP("set STUDIO_E2E_MEDIA to a video with speech");
        auto *tr = m_editor->transcriber();
        tr->refresh();
        if (!tr->available())
            QSKIP("no transcription engine");
        m_editor->newProject(QStringLiteral("1080p30"));
        const QString id = importOne(media);
        QVERIFY(!id.isEmpty());
        shorts()->setTestData({}, {}); // real tools this time
        ShortsGenerator *g = shorts();
        g->setMediaId(id);
        g->setSelectedPlatforms({QStringLiteral("tiktok")});
        g->setCount(2);
        g->setCaptionStyle(qEnvironmentVariable("STUDIO_E2E_STYLE", QStringLiteral("highlight")));
        QSignalSpy done(g, &ShortsGenerator::analysisFinished);
        QElapsedTimer clock;
        clock.start();
        g->analyze();
        QVERIFY2(done.wait(900000), qPrintable(g->error()));
        qInfo() << "analysis took" << clock.elapsed() / 1000.0 << "s, LLM:" << g->usedLlm() << g->llmModel();
        QVERIFY(!g->candidates().isEmpty());
        for (const QVariant &v : g->candidates()) {
            const QVariantMap c = v.toMap();
            qInfo().noquote() << c.value(QStringLiteral("range")).toString() << int(c.value(QStringLiteral("score")).toDouble())
                              << c.value(QStringLiteral("title")).toString() << "|" << c.value(QStringLiteral("reason")).toString();
        }
        const int made = g->createCuts();
        QVERIFY(made > 0);
        QTRY_VERIFY_WITH_TIMEOUT(!g->tracking(), 300000);
        int moving = 0;
        for (const Sequence &s : m_editor->document().sequences)
            for (const Track &t : s.timeline.tracks)
                for (const Clip &c : t.clips)
                    if (c.kind == QLatin1String("media") && !c.effects.isEmpty())
                        moving += c.effects.first().keyframes.size() > 1 || std::abs(c.effects.first().params.value(QStringLiteral("x")).toDouble()) > 1;
        qInfo() << "cuts" << made << "reframed off-centre" << moving << "opencv" << g->opencvAvailable();
        const QString out = qEnvironmentVariable("STUDIO_E2E_OUT", QDir(TestMedia::dir()).filePath(QStringLiteral("e2e")));
        QDir(out).removeRecursively();
        auto *queue = static_cast<RenderQueue *>(m_editor->renderQueueObject());
        QSignalSpy finished(queue, &RenderQueue::jobFinished);
        const int before = queue->count();
        m_editor->exportShorts(QUrl::fromLocalFile(out));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), queue->count() - before, 900000);
        if (!qEnvironmentVariable("STUDIO_E2E_SAVE").isEmpty())
            QVERIFY(m_editor->saveProjectAs(QUrl::fromLocalFile(qEnvironmentVariable("STUDIO_E2E_SAVE"))));
        qInfo() << "exported to" << out << QDir(out).entryList(QDir::Files);

        // captions for a clip in an ordinary project
        m_editor->newProject(QStringLiteral("1080p30"));
        const QString id2 = importOne(media);
        const QString clip = m_editor->addMediaToTimeline(id2, -1, 0, 30 * 30, 45 * 30);
        QVERIFY(!clip.isEmpty());
        m_editor->generateCaptions(clip, QStringLiteral("karaoke"));
        QVERIFY(m_editor->captionsBusy());
        QTRY_VERIFY_WITH_TIMEOUT(!m_editor->captionsBusy(), 600000);
        QVERIFY(m_editor->hasCaptions());
        int cues = 0;
        for (const Track &t : m_editor->currentTimeline().tracks)
            for (const Clip &c : t.clips)
                if (Captions::isCaption(c)) {
                    ++cues;
                    QVERIFY(c.position >= 0 && c.end() <= m_editor->currentTimeline().clip(clip)->end());
                }
        QVERIFY(cues > 3);
        QCOMPARE(m_editor->captionTrackStyle().value(QStringLiteral("effect")).toString(), QStringLiteral("karaoke"));
        const QString vtt = QDir(out).filePath(QStringLiteral("clip.vtt"));
        QVERIFY(m_editor->exportCaptions(QUrl::fromLocalFile(vtt), QStringLiteral("vtt")));
        QFile f(vtt);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().startsWith("WEBVTT"));
        qInfo() << cues << "captions on the clip";
    }
};

int main(int argc, char **argv)
{
    // never the user's settings: a scratch HOME (STUDIO_TEST_HOME may point to a
    // prepared scratch home with ai.conf and a whisper model for the real run)
    QByteArray home = qgetenv("STUDIO_TEST_HOME");
    if (home.isEmpty()) {
        home = QByteArray(TEST_SCRATCH) + "/shorts-home";
        QDir(QString::fromUtf8(home)).removeRecursively();
    }
    QDir().mkpath(QString::fromUtf8(home));
    qputenv("HOME", home);
    qputenv("XDG_CONFIG_HOME", home + "/.config");
    qputenv("XDG_DATA_HOME", home + "/.local/share");
    qputenv("XDG_CACHE_HOME", home + "/.cache");
    qputenv("SDL_AUDIODRIVER", "dummy");
    QGuiApplication app(argc, argv);
    std::setlocale(LC_NUMERIC, "C");
    app.setOrganizationName(QStringLiteral("lingmoos"));
    app.setApplicationName(QStringLiteral("lingmo-studio-test"));
    TestShortsFlow t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_shortsflow.moc"
