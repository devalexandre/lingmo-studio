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

#include "captionoverlay.h"
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
#include <QRegularExpression>
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

// One frame of a video, small, through ffmpeg
static QImage frameAt(const QString &video, double seconds)
{
    const QString png = video + QStringLiteral(".frame.png");
    QFile::remove(png);
    if (!TestMedia::ffmpeg({QStringLiteral("-ss"), QString::number(seconds, 'f', 3), QStringLiteral("-i"), video, QStringLiteral("-frames:v"),
                            QStringLiteral("1"), QStringLiteral("-vf"), QStringLiteral("scale=270:-2"), png}))
        return {};
    return QImage(png).convertToFormat(QImage::Format_RGB32);
}

// Mean difference (0..255) of two frames within rows [top, bottom) (fractions of the height)
static double difference(const QImage &a, const QImage &b, double top, double bottom)
{
    if (a.isNull() || a.size() != b.size())
        return -1;
    double sum = 0;
    int n = 0;
    for (int y = int(a.height() * top); y < int(a.height() * bottom); ++y)
        for (int x = 0; x < a.width(); ++x) {
            const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
            sum += (std::abs(qRed(p) - qRed(q)) + std::abs(qGreen(p) - qGreen(q)) + std::abs(qBlue(p) - qBlue(q))) / 3.0;
            ++n;
        }
    return n ? sum / n : -1;
}

static int painted(const QImage &img)
{
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            n += qAlpha(img.pixel(x, y)) > 128;
    return n;
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

    // The preview plays the source with the cut's captions on top: the same
    // ASS the export renders, shifted to the source's time
    void previewCaptions()
    {
        // the card became a cut: it left the suggestions for "Your cuts"
        QVERIFY(shorts()->candidates().isEmpty());
        QCOMPARE(shorts()->cuts().size(), 1);
        const QVariantMap cut = shorts()->cuts().first().toMap();
        QVERIFY(QStringList({QStringLiteral("ready"), QStringLiteral("reframing")}).contains(cut.value(QStringLiteral("state")).toString()));
        const qint64 startMs = cut.value(QStringLiteral("startMs")).toLongLong();
        shorts()->previewCut(cut.value(QStringLiteral("id")).toString());
        const QString ass = shorts()->previewCaptions();
        QVERIFY(QFileInfo::exists(ass));
        QCOMPARE(shorts()->previewSize(), QSize(1080, 1920));
        // a suggestion's preview (before the cut exists) draws the same captions
        CaptionRenderer fromRange;
        const QString rangeAss = shorts()->previewCaptionsFor(startMs, cut.value(QStringLiteral("endMs")).toLongLong());
        // the source viewer knows the speech of this media too
        QVERIFY(QFileInfo::exists(m_editor->sourceCaptions()));
        QVERIFY(m_editor->sourceAspect() > 1.7);

        const Sequence *seq = nullptr;
        for (const Sequence &s : m_editor->document().sequences)
            if (!s.info.isEmpty())
                seq = &s;
        QVERIFY(seq);
        const Track &captions = seq->timeline.tracks.first();
        QVERIFY(Captions::trackHasCaptions(captions));
        const Captions::Style style = Captions::Style::fromMap(captions.captionStyle);
        const QString exportAss = Editor::cacheCaptions(Captions::toAss(Captions::trackCues(captions, 30, style, 1080, 1920), style, 1080, 1920, 30));
        CaptionRenderer preview, exported;
        int compared = 0;
        // mid-word moments: the karaoke fill is half way through a word
        for (const Clip &c : captions.clips) {
            const qint64 local = qint64((c.position + c.duration() / 2) * 1000.0 / 30);
            const QImage a = preview.render(ass, QSize(270, 480), startMs + local);
            const QImage b = exported.render(exportAss, QSize(270, 480), local);
            QVERIFY(painted(a) > 100);
            QCOMPARE(a, b);
            QCOMPARE(fromRange.render(rangeAss, QSize(270, 480), startMs + local), b);
            if (++compared == 3)
                break;
        }
        QCOMPARE(compared, 3);
        // before the cut: nothing
        QCOMPARE(painted(preview.render(ass, QSize(270, 480), std::max<qint64>(0, startMs - 2000))), 0);
        shorts()->stopPreview();
        QVERIFY(shorts()->previewCaptions().isEmpty());
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
        // as the export dialog does: captions in the video, nothing next to it
        shorts()->exportCuts(QUrl::fromLocalFile(out), QStringLiteral("video"));
        QVERIFY(shorts()->exporting()); // queued, or waiting for the face tracking
        QTRY_COMPARE_WITH_TIMEOUT(queue->count(), 2, 120000);
        QCOMPARE(shorts()->cuts().size(), 1);
        QCOMPARE(shorts()->cuts().first().toMap().value(QStringLiteral("files")).toList().size(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 240000);
        QTRY_VERIFY(shorts()->exportDone());
        QCOMPARE(shorts()->exportFailed(), 0);
        QCOMPARE(shorts()->cuts().first().toMap().value(QStringLiteral("state")).toString(), QStringLiteral("done"));
        QCOMPARE(shorts()->exportFolder(), out);
        const QStringList files = QDir(out).entryList({QStringLiteral("*.mp4")}, QDir::Files, QDir::Name);
        QCOMPARE(files.size(), 2);
        QVERIFY(QDir(out).entryList({QStringLiteral("*.srt")}).isEmpty());
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

    // The captions are in the pixels of the MP4: the same cut exported with
    // "only a .srt" differs where the captions are and nowhere else
    void captionsBurnedIn()
    {
        Document d = m_editor->document();
        for (Sequence &s : d.sequences)
            if (!s.info.isEmpty())
                s.info[QStringLiteral("platforms")] = QStringList{QStringLiteral("tiktok")};
        m_editor->restoreDocument(d);
        auto *queue = static_cast<RenderQueue *>(m_editor->renderQueueObject());
        const QString burned = QDir(TestMedia::dir()).filePath(QStringLiteral("cuts"));
        const QString plain = QDir(TestMedia::dir()).filePath(QStringLiteral("cuts-srt"));
        const QString both = QDir(TestMedia::dir()).filePath(QStringLiteral("cuts-both"));
        QDir(plain).removeRecursively();
        QDir(both).removeRecursively();
        QSignalSpy finished(queue, &RenderQueue::jobFinished);
        shorts()->exportCuts(QUrl::fromLocalFile(plain), QStringLiteral("srt"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 240000);
        shorts()->exportCuts(QUrl::fromLocalFile(both), QStringLiteral("video+srt"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 240000);
        QCOMPARE(shorts()->captionOutput(), QStringLiteral("video")); // back to the default
        QCOMPARE(QDir(plain).entryList({QStringLiteral("*.srt")}).size(), 1);
        QCOMPARE(QDir(both).entryList({QStringLiteral("*.srt")}).size(), 1);
        const QString name = QDir(burned).entryList({QStringLiteral("*tiktok.mp4")}).value(0);
        QVERIFY(!name.isEmpty());
        // a moment with a caption on screen: the middle of the first one
        const Sequence *seq = nullptr;
        for (const Sequence &s : m_editor->document().sequences)
            if (!s.info.isEmpty())
                seq = &s;
        const Clip &first = seq->timeline.tracks.first().clips.first();
        const double t = (first.position + first.duration() / 2.0) / 30.0;
        const QImage withCaptions = frameAt(QDir(burned).filePath(name), t);
        const QImage without = frameAt(QDir(plain).filePath(name), t);
        const QImage again = frameAt(QDir(both).filePath(name), t);
        QVERIFY(!withCaptions.isNull() && !without.isNull() && !again.isNull());
        // TikTok's captions sit above its bottom interface (safe area)
        const double captionBand = difference(withCaptions, without, 0.55, 0.85);
        const double top = difference(withCaptions, without, 0.0, 0.4);
        const double same = difference(withCaptions, again, 0.55, 0.85);
        qInfo() << "caption band difference" << captionBand << "top" << top << "burned vs burned+srt" << same;
        QVERIFY(captionBand > 1.5);
        QVERIFY(captionBand > 4 * top);
        QVERIFY(same < 2);
        withCaptions.save(QDir(burned).filePath(QStringLiteral("burned-frame.png")));
        without.save(QDir(plain).filePath(QStringLiteral("plain-frame.png")));
    }

    // Cuts from a 16:9 project with the video on its timeline (the user's
    // case): a vertical project replaces it, keeping the media, and the media
    // pool shows it, with the cuts as sequences
    void cutsFromAProjectWithWork()
    {
        m_editor->newProject(QStringLiteral("1080p30"));
        const QString id = importOne(QDir(TestMedia::dir()).filePath(QStringLiteral("long.mp4")));
        QVERIFY(!id.isEmpty());
        m_editor->appendMedia(id);
        QVERIFY(m_editor->currentTimeline().duration() > 1);
        QSignalSpy reset(bin(), &QAbstractItemModel::modelReset);
        QList<Captions::Word> words;
        for (qint64 t = 0; t < 29000; t += 500)
            words.append({t, t + 400, QStringLiteral("w%1.").arg(t)});
        const auto made = m_editor->createShorts(*m_editor->mediaItem(id), {{2000, 25000, QStringLiteral("A"), QString(), 50}},
                                                 {QStringLiteral("instagram-reels")}, QStringLiteral("karaoke"), words);
        QCOMPARE(made.size(), 1);
        QCOMPARE(m_editor->projectHeight(), 1920);
        QCOMPARE(bin()->rowCount(), 1);
        QVERIFY(reset.count() >= 1); // views were told
        QCOMPARE(m_editor->shortSequences().size(), 1);
        QVERIFY(m_editor->mediaItem(id));
    }

    // A cut far into a long source (17 min, the cut at 15:48): the captions
    // are timed from the cut's start, in the project, in the ASS melt burns
    // and in the pixels; the picture is the source's at the same moment
    void longSourceCutTiming()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("melt")).isEmpty())
            QSKIP("needs melt");
        const QString path = QDir(TestMedia::dir()).filePath(QStringLiteral("long17min.mp4"));
        if (!QFileInfo::exists(path))
            QVERIFY(TestMedia::ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc2=s=320x180:r=30:d=1020"),
                                       QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=f=300:d=1020"),
                                       QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                                       QStringLiteral("-g"), QStringLiteral("30"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                                       QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-shortest"), path}));
        m_editor->newProject(QStringLiteral("vertical1080"));
        const QString id = importOne(path);
        QVERIFY(!id.isEmpty());
        const MediaItem media = *m_editor->mediaItem(id);
        // speech all along, and one word alone at 15:50.000 (2 s into the cut)
        const qint64 cutStart = 948000, cutEnd = 978000, markAt = 950000;
        QList<Captions::Word> words;
        for (qint64 t = 500; t < 1019000; t += 600) {
            if (std::abs(t - markAt) < 1800)
                continue;
            words.append({t, t + 450, QStringLiteral("p%1%2").arg(t / 600).arg((t / 600) % 7 == 6 ? QStringLiteral(".") : QString())});
        }
        words.append({markAt, markAt + 900, QStringLiteral("MARCADOR.")});
        std::sort(words.begin(), words.end(), [](const Captions::Word &a, const Captions::Word &b) { return a.startMs < b.startMs; });
        const auto made = m_editor->createShorts(media, {{cutStart, cutEnd, QStringLiteral("Longe"), QString(), 50}}, {QStringLiteral("tiktok")},
                                                 QStringLiteral("classic"), words);
        QCOMPARE(made.size(), 1);
        const Sequence *seq = nullptr;
        for (const Sequence &s : m_editor->document().sequences)
            if (s.id == made.first().first)
                seq = &s;
        QVERIFY(seq);
        // the video clip starts at the cut's first frame of the source
        const Clip *video = seq->timeline.clip(made.first().second);
        QVERIFY(video);
        QCOMPARE(video->in, int(cutStart * 30 / 1000));
        QCOMPARE(video->position, 0);
        // the marker's caption: 2 s into the cut
        const Track &captions = seq->timeline.tracks.first();
        const Clip *mark = nullptr;
        for (const Clip &c : captions.clips)
            if (c.name.contains(QLatin1String("MARCADOR")))
                mark = &c;
        QVERIFY(mark);
        QCOMPARE(mark->position, int((markAt - cutStart) * 30 / 1000));

        const QString burned = QDir(TestMedia::dir()).filePath(QStringLiteral("cuts-long"));
        const QString plain = QDir(TestMedia::dir()).filePath(QStringLiteral("cuts-long-srt"));
        QDir(burned).removeRecursively();
        QDir(plain).removeRecursively();
        auto *queue = static_cast<RenderQueue *>(m_editor->renderQueueObject());
        QSignalSpy finished(queue, &RenderQueue::jobFinished);
        const auto outA = m_editor->exportShorts(QUrl::fromLocalFile(burned), QStringLiteral("video"));
        QCOMPARE(outA.size(), 1);
        // the ASS the queued project burns in: the marker at 0:00:02.00
        const QDir renders(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/renders"));
        const QString project = renders.filePath(renders.entryList({QStringLiteral("render-*.mlt")}, QDir::Files, QDir::Time).value(0));
        QFile mlt(project);
        QVERIFY(mlt.open(QIODevice::ReadOnly));
        const QString xml = QString::fromUtf8(mlt.readAll());
        QVERIFY(xml.contains(QLatin1String("avfilter.ass")));
        const QRegularExpressionMatch file = QRegularExpression(QStringLiteral("av.filename\">([^<]+\\.ass)<")).match(xml);
        QVERIFY(file.hasMatch());
        QVERIFY(QFileInfo(file.captured(1)).isAbsolute());
        QFile assFile(file.captured(1));
        QVERIFY(assFile.open(QIODevice::ReadOnly));
        const QString ass = QString::fromUtf8(assFile.readAll());
        QVERIFY2(ass.contains(QRegularExpression(QStringLiteral("Dialogue: 0,0:00:02\\.00,[^\\n]*MARCADOR"))), qPrintable(ass.right(2000)));
        QVERIFY(xml.contains(QStringLiteral("in=\"%1\"").arg(int(cutStart * 30 / 1000))));
        const auto outB = m_editor->exportShorts(QUrl::fromLocalFile(plain), QStringLiteral("srt"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 300000);

        // the marker on the frame 2.3 s into the file, and not 1.2 s before it
        const QImage on = frameAt(outA.first().output, 2.3), off = frameAt(outA.first().output, 1.1);
        const QImage onPlain = frameAt(outB.first().output, 2.3), offPlain = frameAt(outB.first().output, 1.1);
        const double markDiff = difference(on, onPlain, 0.55, 0.85);
        const double beforeDiff = difference(off, offPlain, 0.55, 0.85);
        qInfo() << "marker band difference" << markDiff << "1.2 s earlier" << beforeDiff;
        QVERIFY(markDiff > 1.5);
        QVERIFY(beforeDiff < markDiff / 4);
        on.save(QDir(burned).filePath(QStringLiteral("marker-frame.png")));
        // the picture: the source at cut start + 2.3 s, cropped the same way
        const QString src = QDir(burned).filePath(QStringLiteral("source.png"));
        auto sourceAt = [&](double sec) {
            QFile::remove(src);
            TestMedia::ffmpeg({QStringLiteral("-ss"), QString::number(sec, 'f', 3), QStringLiteral("-i"), path, QStringLiteral("-frames:v"), QStringLiteral("1"),
                               QStringLiteral("-vf"), QStringLiteral("crop=ih*9/16:ih,scale=270:480"), src});
            return QImage(src).convertToFormat(QImage::Format_RGB32);
        };
        const double same = difference(onPlain, sourceAt(cutStart / 1000.0 + 2.3), 0.0, 0.5);
        const double later = difference(onPlain, sourceAt(cutStart / 1000.0 + 3.3), 0.0, 0.5);
        qInfo() << "picture vs source at the same time" << same << "and 1 s later" << later;
        QVERIFY(same < later / 2);
    }

    // One given cut of a real video, as the app makes it: STUDIO_E2E_MEDIA,
    // STUDIO_E2E_CUT=<startMs>-<endMs>, STUDIO_E2E_PLATFORMS (comma list),
    // STUDIO_E2E_OUT. Writes the MP4s, the cut's caption words
    // (captions.json, cut time) and the preview's ASS (preview.ass) there.
    void realCut()
    {
        const QString media = qEnvironmentVariable("STUDIO_E2E_MEDIA");
        const QStringList range = qEnvironmentVariable("STUDIO_E2E_CUT").split(QLatin1Char('-'));
        if (media.isEmpty() || range.size() != 2)
            QSKIP("set STUDIO_E2E_MEDIA and STUDIO_E2E_CUT=<startMs>-<endMs>");
        auto *tr = m_editor->transcriber();
        tr->refresh();
        if (!tr->available())
            QSKIP("no transcription engine");
        const QString out = qEnvironmentVariable("STUDIO_E2E_OUT", QDir(TestMedia::dir()).filePath(QStringLiteral("e2e-cut")));
        QDir(out).removeRecursively();
        QDir().mkpath(out);
        m_editor->newProject(QStringLiteral("vertical1080"));
        const QString id = importOne(media);
        QVERIFY(!id.isEmpty());
        ShortsGenerator *g = shorts();
        g->setTestData({}, {});
        g->setMediaId(id);
        const QStringList platforms = qEnvironmentVariable("STUDIO_E2E_PLATFORMS", QStringLiteral("instagram-reels")).split(QLatin1Char(','));
        g->setSelectedPlatforms(platforms);
        g->setCaptionStyle(qEnvironmentVariable("STUDIO_E2E_STYLE", QStringLiteral("karaoke")));
        QSignalSpy done(g, &ShortsGenerator::analysisFinished);
        QElapsedTimer clock;
        clock.start();
        g->analyze();
        QVERIFY2(done.wait(900000), qPrintable(g->error()));
        const QList<Captions::Word> &words = g->transcript().words;
        qInfo() << "analysis took" << clock.elapsed() / 1000.0 << "s;" << words.size() << "words; repetition" << Captions::repetition(words)
                << "model" << tr->runModel() << "on" << tr->device();
        QVERIFY(Captions::repetition(words) < 0.2);
        const qint64 startMs = range.at(0).toLongLong(), endMs = range.at(1).toLongLong();
        const auto made = m_editor->createShorts(*m_editor->mediaItem(id), {{startMs, endMs, QStringLiteral("Corte real"), QString(), 50}}, platforms,
                                                 g->captionStyle(), words);
        QCOMPARE(made.size(), 1);
        // the caption words, in the cut's time, as the track holds them
        QJsonArray list;
        for (const Sequence &s : m_editor->document().sequences)
            if (s.id == made.first().first)
                for (const Track &t : s.timeline.tracks)
                    if (Captions::trackHasCaptions(t))
                        for (const Captions::Cue &cue : Captions::trackCues(t, 30, Captions::Style::fromMap(t.captionStyle), 1080, 1920))
                            for (const Captions::Word &w : cue.words)
                                list.append(QJsonArray{w.startMs, w.endMs, w.text});
        QFile json(QDir(out).filePath(QStringLiteral("captions.json")));
        QVERIFY(json.open(QIODevice::WriteOnly));
        json.write(QJsonDocument(list).toJson());
        json.close();
        const QString preview = g->previewCaptionsFor(startMs, endMs);
        QVERIFY(QFile::copy(preview, QDir(out).filePath(QStringLiteral("preview.ass"))));
        auto *queue = static_cast<RenderQueue *>(m_editor->renderQueueObject());
        QSignalSpy finished(queue, &RenderQueue::jobFinished);
        auto files = m_editor->exportShorts(QUrl::fromLocalFile(out));
        // the same files without captions, to find them in the pixels
        if (!qEnvironmentVariable("STUDIO_E2E_PLAIN").isEmpty())
            files += m_editor->exportShorts(QUrl::fromLocalFile(QDir(out).filePath(QStringLiteral("plain"))), QStringLiteral("srt"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), int(files.size()), 900000);
        for (const auto &f : files)
            QVERIFY(QFileInfo::exists(f.output));
        qInfo() << "exported" << QDir(out).entryList(QDir::Files);
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
