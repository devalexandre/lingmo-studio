/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Dubbing, the pure parts: sentences and chunks, the translation request and
// its answer, the voice reference, and fitting speech onto the timeline.

#include "dubbing.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Studio;
using namespace Studio::Dubbing;

// Words of `text` spread evenly from `startMs` at `wordsPerSecond`
static QList<Captions::Word> say(const QString &text, qint64 startMs, double wordsPerSecond = 2.6)
{
    QList<Captions::Word> out;
    const qint64 step = qint64(1000.0 / wordsPerSecond);
    qint64 t = startMs;
    for (const QString &w : text.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        out.append({t, t + step - 40, w});
        t += step;
    }
    return out;
}

static QByteArray ollama(const QString &content)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("message"), QJsonObject{{QStringLiteral("content"), content}}}}).toJson(QJsonDocument::Compact);
}

class TestDubbing : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void languages()
    {
        QCOMPARE(Dubbing::languages().size(), 23);
        QCOMPARE(find(QStringLiteral("pt-BR"))->code, QStringLiteral("pt"));
        QCOMPARE(find(QStringLiteral("EN"))->name, QStringLiteral("English"));
        QCOMPARE(normalizeCode(QStringLiteral("iw")), QStringLiteral("he"));
        QCOMPARE(normalizeCode(QStringLiteral("nb")), QStringLiteral("no"));
        QCOMPARE(normalizeCode(QStringLiteral("zh_CN")), QStringLiteral("zh"));
        QVERIFY(normalizeCode(QStringLiteral("auto")).isEmpty()); // whisper's "auto" is not a language
        QVERIFY(!find(QStringLiteral("auto")));
        QVERIFY(!find(QString()));
        // east-asian text carries more per character
        QVERIFY(find(QStringLiteral("zh"))->charsPerSecond < find(QStringLiteral("en"))->charsPerSecond);
    }

    void sentencesBreakAtPunctuationAndLongRuns()
    {
        QList<Captions::Word> w = say(QStringLiteral("Hello everyone. Welcome to my channel!"), 0);
        const QList<Sentence> s = sentences(w);
        QCOMPARE(s.size(), 2);
        QCOMPARE(s.at(0).text, QStringLiteral("Hello everyone."));
        QCOMPARE(s.at(1).text, QStringLiteral("Welcome to my channel!"));
        QCOMPARE(s.at(1).id, 1);
        QCOMPARE(s.at(0).startMs, qint64(0));
        QVERIFY(s.at(0).endMs <= s.at(1).startMs);
        QCOMPARE(s.at(1).words, 4);

        // a run with no punctuation at all is cut, never left at 60 words
        QString long_;
        for (int i = 0; i < 60; ++i)
            long_ += QStringLiteral("word%1%2 ").arg(i).arg(i % 11 == 10 ? QStringLiteral(",") : QString());
        const QList<Sentence> pieces = sentences(say(long_.simplified(), 0, 3.0));
        QVERIFY(pieces.size() >= 2);
        int words = 0;
        for (const Sentence &p : pieces) {
            QVERIFY2(p.words <= 30, qPrintable(QString::number(p.words)));
            QVERIFY(p.seconds() <= 14.0);
            words += p.words;
        }
        QCOMPARE(words, 60);

        // a pause is a boundary too
        QList<Captions::Word> gap = say(QStringLiteral("one two three"), 0) + say(QStringLiteral("four five six"), 6000);
        QCOMPARE(sentences(gap).size(), 2);
        QVERIFY(sentences({}).isEmpty());
    }

    void chunksGroupNeighbours()
    {
        QList<Captions::Word> w = say(QStringLiteral("One two three four. Five six seven eight."), 0) + say(QStringLiteral("Nine ten eleven twelve."), 9000);
        QList<Sentence> s = sentences(w);
        QCOMPARE(s.size(), 3);
        for (int i = 0; i < s.size(); ++i)
            s[i].translation = QStringLiteral("T%1.").arg(i);
        const QList<Chunk> c = chunks(s, 20000);
        QCOMPARE(c.size(), 2); // the first two are together, the third is after a 4 s pause
        QCOMPARE(c.at(0).firstSentence, 0);
        QCOMPARE(c.at(0).lastSentence, 1);
        QCOMPARE(c.at(0).startMs, s.at(0).startMs);
        QCOMPARE(c.at(0).endMs, s.at(1).endMs);
        QCOMPARE(c.at(0).slotEndMs, s.at(2).startMs);
        QCOMPARE(c.at(1).slotEndMs, qint64(20000));
        QCOMPARE(c.at(0).id(), QStringLiteral("c0"));
        QCOMPARE(c.at(1).id(), QStringLiteral("c2"));
        QCOMPARE(chunkText(s, c.at(0)), QStringLiteral("T0. T1."));
        QCOMPARE(chunkText(s, c.at(1)), QStringLiteral("T2."));

        // never one very long chunk
        QString text;
        for (int i = 0; i < 12; ++i)
            text += QStringLiteral("Sentence number %1 is here now. ").arg(i);
        const QList<Sentence> many = sentences(say(text.simplified(), 0, 3.0));
        for (const Chunk &x : chunks(many, 60000))
            QVERIFY(x.endMs - x.startMs <= 10000);
    }

    void translationRequest()
    {
        QList<Sentence> batch(2);
        batch[0] = {3, 1000, 4000, 6, QStringLiteral("Olá pessoal, tudo bem?"), QString()};
        batch[1] = {4, 4500, 6500, 5, QStringLiteral("Hoje vamos falar de servidores."), QString()};
        QList<Sentence> ctx(1);
        ctx[0] = {2, 0, 900, 2, QStringLiteral("Bem-vindos."), QStringLiteral("Welcome.")};
        const Language *en = find(QStringLiteral("en"));
        const QJsonObject body = QJsonDocument::fromJson(translateRequest(QStringLiteral("m"), batch, ctx, *en, QStringLiteral("Portuguese"))).object();
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("m"));
        QCOMPARE(body.value(QStringLiteral("think")).toBool(), false);
        QCOMPARE(body.value(QStringLiteral("stream")).toBool(), false);
        const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
        const QString system = messages.at(0).toObject().value(QStringLiteral("content")).toString();
        QVERIFY(system.contains(QStringLiteral("into English")));
        QVERIFY(system.contains(QStringLiteral("in Portuguese")));
        QVERIFY(system.contains(QStringLiteral("SPOKEN")));
        const QJsonObject user = QJsonDocument::fromJson(messages.at(1).toObject().value(QStringLiteral("content")).toString().toUtf8()).object();
        const QJsonArray items = user.value(QStringLiteral("translate")).toArray();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.at(0).toObject().value(QStringLiteral("id")).toInt(), 3);
        QCOMPARE(items.at(0).toObject().value(QStringLiteral("seconds")).toDouble(), 3.0);
        QCOMPARE(items.at(0).toObject().value(QStringLiteral("target_chars")).toInt(), targetChars(3.0, *en));
        QCOMPARE(user.value(QStringLiteral("previous_sentences")).toArray().at(0).toObject().value(QStringLiteral("translation")).toString(), QStringLiteral("Welcome."));
        // schema asks for an array of {id, text}
        QVERIFY(body.value(QStringLiteral("format")).toObject().value(QStringLiteral("properties")).toObject().contains(QStringLiteral("translations")));
        QVERIFY(targetChars(0.1, *en) >= 8);
    }

    void translationAnswer()
    {
        const QList<int> ids = {3, 4, 5};
        // fenced, with a thinking block, ids as strings, an id nobody asked for, an empty text
        const QString answer = QStringLiteral("<think>…</think>```json\n{\"translations\": [{\"id\": 3, \"text\": \"  \\\"Hi everyone, how are you?\\\"  \"}, "
                                              "{\"id\": \"4\", \"text\": \"Today we talk about servers.\"}, {\"id\": 5, \"text\": \"\"}, {\"id\": 99, \"text\": \"x\"}]}\n```");
        const QMap<int, QString> r = parseTranslation(ollama(answer), ids);
        QCOMPARE(r.size(), 2);
        QCOMPARE(r.value(3), QStringLiteral("Hi everyone, how are you?"));
        QCOMPARE(r.value(4), QStringLiteral("Today we talk about servers."));

        // an array at the top level, "translation" instead of "text"
        const QMap<int, QString> arr = parseTranslation(ollama(QStringLiteral("[{\"id\":3,\"translation\":\"One\"},{\"id\":4,\"text\":\"Two\"}]")), ids);
        QCOMPARE(arr.value(3), QStringLiteral("One"));
        QCOMPARE(arr.value(4), QStringLiteral("Two"));
        // the first answer for an id wins
        QCOMPARE(parseTranslation(ollama(QStringLiteral("{\"translations\":[{\"id\":3,\"text\":\"A\"},{\"id\":3,\"text\":\"B\"}]}")), ids).value(3), QStringLiteral("A"));
        QVERIFY(parseTranslation(ollama(QStringLiteral("I cannot do that.")), ids).isEmpty());
        QVERIFY(parseTranslation(QByteArray(), ids).isEmpty());
    }

    void voiceReferenceIsTheBestContinuousPassage()
    {
        // 6 s of music-like silence, then 40 s of speech in sentences of ~3 s, a 3 s pause, 8 s more
        QList<Captions::Word> w;
        qint64 t = 6000;
        for (int i = 0; i < 13; ++i) {
            w += say(QStringLiteral("this is a full sentence of calm speech."), t);
            t = w.last().endMs + 150;
        }
        t += 3000;
        w += say(QStringLiteral("and a later short remark. Done."), t);
        const Window win = referenceWindow(w);
        QVERIFY(win.valid());
        QVERIFY2(win.length() >= 9000 && win.length() <= 15000, qPrintable(QString::number(win.length())));
        QVERIFY(win.startMs >= 6000);
        // it starts on a sentence and ends on one
        bool starts = false, ends = false;
        for (int i = 0; i < w.size(); ++i) {
            starts |= w.at(i).startMs == win.startMs && (i == 0 || w.at(i - 1).text.endsWith(QLatin1Char('.')));
            ends |= w.at(i).endMs == win.endMs && w.at(i).text.endsWith(QLatin1Char('.'));
        }
        QVERIFY(starts);
        QVERIFY(ends);

        // too little speech: no reference
        QVERIFY(!referenceWindow(say(QStringLiteral("just three words"), 0)).valid());
        QVERIFY(!referenceWindow({}).valid());
        // short but continuous: the longest passage is better than nothing
        const Window shortWin = referenceWindow(say(QStringLiteral("one short sentence that is still four seconds long ok."), 1000, 2.8));
        QVERIFY(shortWin.valid());
        QVERIFY(shortWin.length() >= 3500);
        // pauses break a passage: nothing here is 9 s without a 2 s hole
        QList<Captions::Word> holes;
        for (int i = 0; i < 6; ++i)
            holes += say(QStringLiteral("a short phrase here."), i * 7000);
        const Window h = referenceWindow(holes);
        QVERIFY(!h.valid() || h.length() < 9000);
    }

    void fitKeepsNaturalSpeedWhenItFits()
    {
        const FitResult r = fit({{QStringLiteral("a"), 1.0, 5.0, 3.0}, {QStringLiteral("b"), 6.0, 10.0, 3.5}}, 12.0);
        QCOMPARE(r.items.size(), 2);
        QCOMPARE(r.items.at(0).startSec, 1.0);
        QCOMPARE(r.items.at(0).ratio, 1.0);
        QCOMPARE(r.items.at(1).startSec, 6.0);
        QCOMPARE(r.sped, 0);
        QCOMPARE(r.maxSpeedup, 1.0);
        QCOMPARE(r.maxDriftSec, 0.0);
        QCOMPARE(r.overflowEndSec <= 0.0, true);
    }

    void fitSpeedsUpOnlyAsMuchAsNeeded()
    {
        // 4.4 s of speech for a 4 s slot: ~10% faster
        const FitResult r = fit({{QStringLiteral("a"), 0.0, 4.0, 4.4}}, 10.0);
        QVERIFY(r.items.at(0).ratio < 1.0);
        QVERIFY(r.items.at(0).ratio > 0.85);
        QVERIFY(r.items.at(0).startSec + r.items.at(0).seconds <= 4.0);
        QCOMPARE(r.sped, 1);
        QVERIFY(r.maxSpeedup > 1.05 && r.maxSpeedup < 1.2);

        // far too long: capped at 1.3x, the rest spills into the pause that follows
        const FitResult cap = fit({{QStringLiteral("a"), 0.0, 3.0, 6.0}, {QStringLiteral("b"), 8.0, 12.0, 2.0}}, 14.0, 1.3);
        QVERIFY(std::abs(cap.items.at(0).ratio - 1.0 / 1.3) < 1e-9);
        QVERIFY(cap.items.at(0).seconds > 3.0);
        QCOMPARE(cap.items.at(1).startSec, 8.0); // the pause absorbed it: back on time
        QCOMPARE(cap.items.at(1).driftSec, 0.0);
    }

    void fitDriftsAndRecovers()
    {
        // each runs a bit over; the next starts late, then a long pause wins the time back
        const FitResult r = fit({{QStringLiteral("a"), 0.0, 2.0, 5.0}, {QStringLiteral("b"), 2.5, 4.5, 2.0}, {QStringLiteral("c"), 9.0, 12.0, 2.0}}, 14.0, 1.3);
        QVERIFY(r.items.at(1).driftSec > 0.5);
        QVERIFY(r.items.at(1).startSec >= r.items.at(0).startSec + r.items.at(0).seconds);
        QCOMPARE(r.items.at(2).startSec, 9.0);
        QVERIFY(r.maxDriftSec > 0.5);
        // pieces never overlap
        for (int i = 1; i < r.items.size(); ++i)
            QVERIFY(r.items.at(i).startSec >= r.items.at(i - 1).startSec + r.items.at(i - 1).seconds - 1e-9);
    }

    void fitReportsSpeechPastTheEnd()
    {
        const FitResult r = fit({{QStringLiteral("a"), 8.0, 10.0, 4.0}}, 10.0, 1.3);
        QVERIFY(r.overflowEndSec > 0.5);
        QVERIFY(fit({}, 10.0).items.isEmpty());
        // an empty piece takes no room and is never "sped up"
        QCOMPARE(fit({{QStringLiteral("a"), 1.0, 2.0, 0.0}}, 10.0).sped, 0);
    }
};

QTEST_GUILESS_MAIN(TestDubbing)
#include "tst_dubbing.moc"
