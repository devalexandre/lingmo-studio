/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Automatic Shorts/Reels: platform presets, sentence segmentation, candidate
// segments fitted to the platform length, heuristic scoring and selection,
// the Ollama request/answer, and the reframe path smoothing.

#include "highlights.h"
#include "platforms.h"
#include "reframe.h"

#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QProcess>
#include <QTcpSocket>

using namespace Studio;
using Captions::Word;

// ~2.4 words/s talk with sentence ends, a long pause in the middle
static QList<Word> talk(int sentencesCount, int wordsPerSentence = 9)
{
    QList<Word> out;
    qint64 t = 1000;
    for (int s = 0; s < sentencesCount; ++s) {
        for (int w = 0; w < wordsPerSentence; ++w) {
            QString text = QStringLiteral("palavra%1").arg(w);
            if (s == 3 && w == 0)
                text = QStringLiteral("Como");
            if (w + 1 == wordsPerSentence)
                text += (s == 3 ? QStringLiteral("?") : QStringLiteral("."));
            out.append({t, t + 330, text});
            t += 420;
        }
        t += (s == 5) ? 6000 : 500;
    }
    return out;
}

class TestShorts : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void platformsLoad()
    {
        QStringList errors;
        const QList<Platform> list = Platforms::load(QStringLiteral(TEST_DATA "/platforms.json"), &errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        QStringList ids;
        for (const Platform &p : list) {
            ids << p.id;
            QVERIFY(p.valid());
            QVERIFY2(!p.source.isEmpty(), qPrintable(p.id)); // every number says where it came from
            QVERIFY(p.idealMin() <= p.idealMax());
            QVERIFY(p.idealMax() <= p.maxSeconds);
            QVERIFY(p.safeArea.top + p.safeArea.bottom < 0.8);
            QCOMPARE(qint64(p.width) * p.aspect.section(QLatin1Char(':'), 1).toInt(), qint64(p.height) * p.aspect.section(QLatin1Char(':'), 0, 0).toInt());
        }
        for (const char *id : {"tiktok", "instagram-reels", "youtube-shorts", "facebook-reels", "kwai", "linkedin-square", "linkedin-portrait", "x", "stories"})
            QVERIFY2(ids.contains(QLatin1String(id)), id);
        Platforms::setAll(list);
        const Platform stories = Platforms::find(QStringLiteral("stories"));
        QCOMPARE(stories.maxSeconds, 60);
        QCOMPARE(stories.idealMin(), 15);
        QCOMPARE(stories.aspect, QStringLiteral("9:16"));
        QCOMPARE(Platforms::find(QStringLiteral("linkedin-portrait")).height, 1350);
        // safe area rectangle
        const QRectF r = Platforms::find(QStringLiteral("instagram-reels")).safeArea.rect(1080, 1920);
        QVERIFY(r.bottom() < 1920 * 0.7);

        bool same = false;
        QCOMPARE(Platforms::primary({QStringLiteral("tiktok"), QStringLiteral("youtube-shorts")}, &same).id, QStringLiteral("tiktok"));
        QVERIFY(same);
        Platforms::primary({QStringLiteral("tiktok"), QStringLiteral("linkedin-square")}, &same);
        QVERIFY(!same);

        // bad entries are skipped, not fatal
        errors.clear();
        const QList<Platform> partial = Platforms::fromJson(
            "{\"platforms\":[{\"id\":\"ok\",\"width\":1080,\"height\":1920,\"maxSeconds\":60,\"recommended\":[10,30]},"
            "{\"id\":\"bad\",\"width\":0,\"height\":0}]}", &errors);
        QCOMPARE(partial.size(), 1);
        QCOMPARE(partial.first().aspect, QStringLiteral("9:16"));
        QCOMPARE(errors.size(), 1);
    }

    void sentenceSegmentation()
    {
        const QList<Word> w = talk(8);
        const QList<Highlights::Sentence> s = Highlights::sentences(w);
        QCOMPARE(s.size(), 8);
        QCOMPARE(s.at(3).text.left(4), QStringLiteral("Como"));
        QVERIFY(s.at(3).text.endsWith(QLatin1Char('?')));
        // a long pause also ends a sentence
        QList<Word> run = {{0, 300, QStringLiteral("sem")}, {300, 600, QStringLiteral("ponto")}, {4000, 4300, QStringLiteral("depois")}};
        QCOMPARE(Highlights::sentences(run).size(), 2);
    }

    void candidatesFitTheLength_data()
    {
        QTest::addColumn<int>("minSec");
        QTest::addColumn<int>("maxSec");
        QTest::newRow("stories 15-60") << 15 << 60;
        QTest::newRow("x 15-45") << 15 << 45;
        QTest::newRow("tight 8-12") << 8 << 12;
    }
    void candidatesFitTheLength()
    {
        QFETCH(int, minSec);
        QFETCH(int, maxSec);
        const QList<Word> w = talk(12);
        const QList<Highlights::Sentence> s = Highlights::sentences(w);
        const QList<Highlights::Candidate> cands = Highlights::candidates(s, w, minSec, maxSec, w.last().endMs + 2000);
        QVERIFY(!cands.isEmpty());
        for (const Highlights::Candidate &c : cands) {
            QVERIFY2(c.seconds() >= minSec - 0.01 && c.seconds() <= maxSec + 0.01, qPrintable(QString::number(c.seconds())));
            // starts just before a sentence and ends just after one: no cut words
            bool startsAtSentence = false, endsAtSentence = false;
            for (const Highlights::Sentence &x : s) {
                startsAtSentence |= std::abs(c.startMs - std::max<qint64>(0, x.startMs - 250)) <= 1;
                endsAtSentence |= std::abs(c.endMs - (x.endMs + 400)) <= 1;
            }
            QVERIFY(startsAtSentence);
            QVERIFY(endsAtSentence);
        }
    }

    void longSentencesAreSplit()
    {
        // one 40-word sentence without punctuation: still yields fitting candidates
        QList<Word> w;
        for (int i = 0; i < 40; ++i)
            w.append({i * 500, i * 500 + 400, QStringLiteral("w%1").arg(i)});
        w.last().text += QLatin1Char('.');
        const auto cands = Highlights::candidates(Highlights::sentences(w), w, 5, 10);
        QVERIFY(!cands.isEmpty());
        for (const auto &c : cands)
            QVERIFY(c.seconds() <= 10.01);
    }

    void heuristicPrefersLivelySegments()
    {
        const QList<Word> w = talk(12);
        const auto s = Highlights::sentences(w);
        auto cands = Highlights::candidates(s, w, 10, 30, w.last().endMs + 2000);
        Highlights::Features f;
        // loud in sentences 3-4, a scene cut there; quiet elsewhere
        for (double t = 0; t < w.last().endMs / 1000.0; t += 0.1) {
            const bool hot = t * 1000 >= s.at(3).startMs && t * 1000 <= s.at(4).endMs;
            f.loudness.append({t, hot ? -14.0 : -26.0});
        }
        f.scenes = {s.at(3).startMs / 1000.0 + 1, s.at(4).startMs / 1000.0 + 0.5};
        f.silences = {{s.at(5).endMs / 1000.0 + 0.2, s.at(5).endMs / 1000.0 + 5.8}};
        for (auto &c : cands) {
            c.heuristic = Highlights::heuristicScore(c, w, f, 20);
            c.score = c.heuristic;
            QVERIFY(c.score >= 0 && c.score <= 100);
        }
        const auto best = Highlights::pickBest(cands, 3);
        QVERIFY(!best.isEmpty());
        QVERIFY(best.size() <= 3);
        // the winner opens with the question and contains the loud part
        QCOMPARE(best.first().firstSentence, 3);
        // no two picks overlap much
        for (int i = 0; i < best.size(); ++i)
            for (int j = i + 1; j < best.size(); ++j) {
                const qint64 ov = std::min(best.at(i).endMs, best.at(j).endMs) - std::max(best.at(i).startMs, best.at(j).startMs);
                QVERIFY(ov <= 0.25 * std::min(best.at(i).endMs - best.at(i).startMs, best.at(j).endMs - best.at(j).startMs));
            }
        // the long silence costs points
        Highlights::Candidate withGap;
        for (const auto &c : cands)
            if (c.firstSentence == 5 && c.lastSentence == 6)
                withGap = c;
        Highlights::Candidate tight;
        for (const auto &c : cands)
            if (c.firstSentence == 7 && c.lastSentence == 8)
                tight = c;
        if (!withGap.id.isEmpty() && !tight.id.isEmpty())
            QVERIFY(Highlights::scoreParts(withGap, w, f, 20).pauses > Highlights::scoreParts(tight, w, f, 20).pauses);
    }

    void llmRequestAndReply()
    {
        Highlights::Candidate c;
        c.startMs = 0;
        c.endMs = 30000;
        c.text = QStringLiteral("Você sabia que dá pra editar vídeo sem pagar nada?");
        const QJsonObject body = QJsonDocument::fromJson(Highlights::llmRequest(QStringLiteral("qwen3.5:4b"), c, QStringLiteral("TikTok"), QStringLiteral("pt"))).object();
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("qwen3.5:4b"));
        QCOMPARE(body.value(QStringLiteral("think")).toBool(true), false);
        QCOMPARE(body.value(QStringLiteral("stream")).toBool(true), false);
        QVERIFY(body.value(QStringLiteral("format")).isObject());
        QVERIFY(QJsonDocument(body).toJson().contains("Brazilian Portuguese"));

        auto r = Highlights::parseLlmReply(QJsonDocument(QJsonObject{{QStringLiteral("message"), QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
            {QStringLiteral("content"), QStringLiteral("{\"score\": 82, \"title\": \"Edite de graça\", \"reason\": \"Gancho forte\"}")}}}}).toJson());
        QVERIFY(r.ok);
        QCOMPARE(r.score, 82.0);
        QCOMPARE(r.title, QStringLiteral("Edite de graça"));
        // reasoning text, code fences and a 0..10 scale
        r = Highlights::parseLlmReply("<think>hmm</think>\n```json\n{\"score\": 7, \"title\": \"T\", \"reason\": \"R\"}\n```");
        QVERIFY(r.ok);
        QCOMPARE(r.score, 70.0);
        r = Highlights::parseLlmReply("{\"score\": 0.9, \"title\": \"x\", \"reason\": \"y\"}");
        QCOMPARE(r.score, 90.0);
        QVERIFY(!Highlights::parseLlmReply("sorry, I can't").ok);
        QCOMPARE(Highlights::combine(40, -1), 40.0);
        QCOMPARE(Highlights::combine(40, 80), 0.65 * 80 + 0.35 * 40);
    }

    // Real call to the local Ollama when the test is asked to (read-only: /api/chat)
    void ollamaScores()
    {
        const QString model = qEnvironmentVariable("STUDIO_TEST_OLLAMA_MODEL");
        if (model.isEmpty())
            QSKIP("set STUDIO_TEST_OLLAMA_MODEL to score with the local Ollama");
        Highlights::Candidate c;
        c.endMs = 25000;
        c.text = QStringLiteral("Você sabia que a maioria das pessoas assiste vídeo sem som? Por isso legenda grande muda tudo. "
                                "Hoje eu vou te mostrar como fazer isso em dois minutos.");
        QTcpSocket probe;
        probe.connectToHost(QStringLiteral("127.0.0.1"), 11434);
        if (!probe.waitForConnected(1000))
            QSKIP("Ollama is not running");
        probe.close();
        QProcess curl;
        curl.start(QStringLiteral("curl"), {QStringLiteral("-s"), QStringLiteral("-m"), QStringLiteral("120"), QStringLiteral("http://127.0.0.1:11434/api/chat"),
                                            QStringLiteral("-d"), QString::fromUtf8(Highlights::llmRequest(model, c, QStringLiteral("TikTok"), QStringLiteral("pt")))});
        QVERIFY(curl.waitForFinished(130000));
        const QByteArray reply = curl.readAllStandardOutput();
        const auto r = Highlights::parseLlmReply(reply);
        QVERIFY2(r.ok, reply.left(400).constData());
        qInfo() << model << "score" << r.score << "title" << r.title << "reason" << r.reason;
        QVERIFY(r.score >= 0 && r.score <= 100);
        QVERIFY(!r.title.isEmpty());
    }

    void loudnessCurve()
    {
        const QString out = QStringLiteral("[Parsed_ebur128_1 @ 0x1] t: 0.1  TARGET:-23 LUFS    M: -120.7 S:-120.7     I: -70.0 LUFS\n"
                                           "[Parsed_ebur128_1 @ 0x1] t: 0.2  TARGET:-23 LUFS    M: -18.2 S:-120.7     I: -18.2 LUFS\n"
                                           "[Parsed_ebur128_1 @ 0x1] t: 0.3  TARGET:-23 LUFS    M:  -inf S:-120.7     I: -18.2 LUFS\n");
        const auto pts = Highlights::parseLoudnessCurve(out);
        QCOMPARE(pts.size(), 3);
        QCOMPARE(pts.at(1).t, 0.2);
        QCOMPARE(pts.at(1).lufs, -18.2);
        QCOMPARE(pts.at(2).lufs, -120.0);
    }

    void reframePath()
    {
        // a face that sits left, jitters, has a false detection and a gap, then walks right
        QByteArray lines;
        for (int i = 0; i < 40; ++i) {
            const double t = i * 0.25;
            QJsonObject o{{QStringLiteral("t"), t}};
            if (i == 7)
                o[QStringLiteral("x")] = 0.95; // false positive
            else if (i >= 10 && i < 14)
                o[QStringLiteral("x")] = QJsonValue::Null; // looked away
            else
                o[QStringLiteral("x")] = (i < 20 ? 0.3 : 0.7) + ((i % 2) ? 0.01 : -0.01);
            lines += QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
        }
        const QList<Reframe::Sample> raw = Reframe::parseDetections(lines);
        QCOMPARE(raw.size(), 40);
        QCOMPARE(raw.at(10).x, -1.0);
        const QList<Reframe::Sample> path = Reframe::smooth(raw);
        QCOMPARE(path.size(), 40);
        for (int i = 1; i < path.size(); ++i) {
            QVERIFY(path.at(i).x >= 0 && path.at(i).x <= 1);
            // speed limit: 0.35 of the width per second
            QVERIFY(std::abs(path.at(i).x - path.at(i - 1).x) <= 0.35 * 0.25 + 1e-9);
        }
        QVERIFY(std::abs(path.at(7).x - 0.3) < 0.05);  // the outlier is ignored
        QVERIFY(std::abs(path.at(12).x - 0.3) < 0.05); // the gap holds position
        QVERIFY(std::abs(path.at(5).x - path.at(6).x) < 1e-9); // jitter inside the dead zone doesn't move the camera
        QVERIFY(path.last().x > 0.6);                   // follows the walk

        // 1920x1080 source in a 1080x1920 frame
        const double scale = Reframe::fillScale(1920, 1080, 1080, 1920);
        QVERIFY(std::abs(scale - 316.05) < 0.1);
        QCOMPARE(Reframe::positionFor(0.5, 1920, 1080, 1080, 1920), 0.0);
        const double limit = (3413.33 - 1080) / 2;
        QVERIFY(std::abs(Reframe::positionFor(0.0, 1920, 1080, 1080, 1920) - limit) < 1);   // clamped: frame stays covered
        QVERIFY(std::abs(Reframe::positionFor(0.3, 1920, 1080, 1080, 1920) - 682.7) < 1);   // (0.5 - 0.3) * 3413
        const QVariantMap base{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}, {QStringLiteral("scale"), scale}, {QStringLiteral("rotation"), 0.0}, {QStringLiteral("opacity"), 100.0}};
        const QList<Keyframe> kfs = Reframe::keyframes(path, 0, 300, 30, 1920, 1080, 1080, 1920, base);
        QVERIFY(kfs.size() >= 3);
        QVERIFY(kfs.size() < 30); // simplified, editable
        QCOMPARE(kfs.first().frame, 0);
        QCOMPARE(kfs.last().frame, 299);
        QVERIFY(kfs.first().values.value(QStringLiteral("x")).toDouble() > 600);  // subject left → image moves right
        QVERIFY(kfs.last().values.value(QStringLiteral("x")).toDouble() < -400);
        QCOMPARE(kfs.first().values.value(QStringLiteral("scale")).toDouble(), scale);
        // a still subject gives one keyframe
        QList<Reframe::Sample> still;
        for (int i = 0; i < 20; ++i)
            still.append({i * 0.25, 0.5, 1});
        QCOMPARE(Reframe::keyframes(Reframe::smooth(still), 0, 150, 30, 1920, 1080, 1080, 1920, base).size(), 1);
    }
};

QTEST_MAIN(TestShorts)
#include "tst_shorts.moc"
