/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Captions: whisper JSON parsers, cue building with the readability rules,
// line breaking, ASS generation (karaoke timings, frame snapping), sidecars,
// caption clip edits, and frame-accurate rendering through libass in both
// the live (preview) and the MLT XML (export) paths.

#include "captions.h"
#include "mltbuilder.h"
#include "mltxml.h"
#include "plan.h"
#include "projectio.h"
#include "timelineops.h"
#include "testmedia.h"

#include <QFile>
#include <QImage>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <mlt++/Mlt.h>

using namespace Studio;
using namespace Studio::Captions;

static QByteArray fixture(const char *name)
{
    QFile f(QStringLiteral(TEST_FIXTURES "/") + QLatin1String(name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static QList<Word> words(std::initializer_list<std::tuple<int, int, const char *>> list)
{
    QList<Word> out;
    for (const auto &[s, e, t] : list)
        out.append({s, e, QString::fromUtf8(t)});
    return out;
}

// A sentence-y transcript: ~2.5 words per second
static QList<Word> longTranscript()
{
    const QStringList text = QStringLiteral(
        "Olá pessoal, hoje eu vou mostrar como editar um vídeo curto para as redes sociais. "
        "Primeiro a gente escolhe o melhor trecho da gravação. Depois coloca legendas grandes e fáceis de ler, "
        "porque a maioria das pessoas assiste sem som. E no final exporta no formato vertical!").split(QLatin1Char(' '));
    QList<Word> out;
    qint64 t = 500;
    for (const QString &w : text) {
        const qint64 len = 180 + w.size() * 25;
        out.append({t, t + len, w});
        t += len + 60;
        if (w.endsWith(QLatin1Char('.')))
            t += 700;
    }
    return out;
}

class TestCaptions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void whisperCppJson()
    {
        const Transcript t = parseWhisperCppJson(fixture("whisper-cpp-ojf.json"));
        QCOMPARE(t.language, QStringLiteral("pt"));
        QStringList texts;
        for (const Word &w : t.words)
            texts << w.text;
        QCOMPARE(texts.join(QLatin1Char(' ')), QStringLiteral("O que vamos falar sobre a edição de vídeo é muito mais fácil do que acontece."));
        // " ed" + "ição" is one word with both tokens' span; "." glued to the last word
        const int i = texts.indexOf(QStringLiteral("edição"));
        QVERIFY(i > 0);
        QCOMPARE(t.words.at(i).startMs, 2400);
        QCOMPARE(t.words.at(i).endMs, 3050);
        QCOMPARE(t.words.last().text, QStringLiteral("acontece."));
        QCOMPARE(t.words.last().endMs, 6400);
        for (int k = 0; k < t.words.size(); ++k) {
            QVERIFY2(t.words.at(k).endMs > t.words.at(k).startMs, qPrintable(t.words.at(k).text)); // " é" was 4010-4010
            if (k > 0)
                QVERIFY(t.words.at(k).startMs >= t.words.at(k - 1).startMs);
        }
        // the same document through the generic entry point
        QCOMPARE(parseTranscript(fixture("whisper-cpp-ojf.json")).words, t.words);
    }

    void whisperCppOneWordSegments()
    {
        // whisper-cli -ml 1 -oj: one word per segment, no tokens
        const QByteArray json = R"({"result":{"language":"pt"},"transcription":[
            {"offsets":{"from":0,"to":400},"text":" Olá"},{"offsets":{"from":400,"to":900},"text":" mundo"},
            {"offsets":{"from":900,"to":1000},"text":"!"}]})";
        const Transcript t = parseWhisperCppJson(json);
        QCOMPARE(t.words.size(), 2);
        QCOMPARE(t.words.at(1).text, QStringLiteral("mundo!"));
        QCOMPARE(t.words.at(1).startMs, 400);
        QCOMPARE(t.words.at(1).endMs, 1000);
    }

    void fasterWhisperJson()
    {
        const Transcript t = parseTranscript(fixture("faster-whisper.json"));
        QCOMPARE(t.language, QStringLiteral("pt"));
        QCOMPARE(t.words.size(), 9);
        QCOMPARE(t.words.first().text, QStringLiteral("Olá"));
        QCOMPARE(t.words.at(1).endMs, 1100);
        QCOMPARE(t.words.at(6).text, QStringLiteral("edição"));
        QCOMPARE(t.words.at(6).startMs, 2800);
        // cache round trip
        QCOMPARE(transcriptFromJson(transcriptToJson(t)).words, t.words);
    }

    void lineBreaksAtPhraseBoundaries()
    {
        // fits on one line
        QCOMPARE(breakLines(words({{0, 1, "Olá"}, {1, 2, "pessoal"}}), 20, 2), QStringList{QStringLiteral("Olá pessoal")});
        // breaks after the comma, not before the last word
        const QList<Word> w = words({{0, 1, "No"}, {1, 2, "começo,"}, {1, 2, "a"}, {2, 3, "gente"}, {3, 4, "escolhe"}, {4, 5, "o"}, {5, 6, "trecho"}});
        const QStringList lines = breakLines(w, 26, 2);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.first(), QStringLiteral("No começo,"));
        // never a lone word on a line when another split fits
        const QList<Word> w2 = words({{0, 1, "vamos"}, {1, 2, "falar"}, {2, 3, "sobre"}, {3, 4, "edição"}});
        for (const QString &l : breakLines(w2, 18, 2))
            QVERIFY2(l.contains(QLatin1Char(' ')), qPrintable(l));
        // don't end a line on an article/preposition
        const QStringList l3 = breakLines(words({{0, 1, "coloca"}, {1, 2, "legendas"}, {2, 3, "grandes"}, {3, 4, "e"}, {4, 5, "fáceis"}, {5, 6, "de"}, {6, 7, "ler"}}), 24, 2);
        QCOMPARE(l3.size(), 2);
        QVERIFY2(!l3.first().endsWith(QLatin1String(" e")) && !l3.first().endsWith(QLatin1String(" de")), qPrintable(l3.join(QLatin1Char('|'))));
        // too long for two lines
        QVERIFY(breakLines(longTranscript().mid(0, 12), 16, 2).isEmpty());
        // one line only
        QVERIFY(breakLines(w2, 18, 1).isEmpty());
        // two words slightly over the limit stay on one line instead of one word per line
        QCOMPARE(breakLines(words({{0, 1, "da"}, {1, 2, "ruim-to-resultado."}}), 18, 2),
                 QStringList{QStringLiteral("da ruim-to-resultado.")});
        // far over the limit: two lines are still better
        QCOMPARE(breakLines(words({{0, 1, "extraordinariamente"}, {1, 2, "desconfortável"}}), 20, 2).size(), 2);
    }

    void assShrinksLinesWiderThanTheSafeArea()
    {
        Style s = Style::preset(QStringLiteral("karaoke"));
        Cue c;
        c.startMs = 0;
        c.endMs = 2000;
        c.words = words({{0, 500, "da"}, {500, 2000, "ruim-to-resultado-maravilhoso."}});
        c.lines = {QStringLiteral("da ruim-to-resultado-maravilhoso.")};
        const QString ass = toAss({c}, s, 1080, 1920, 30);
        QVERIFY2(ass.contains(QLatin1String("\\fscx")), qPrintable(ass));
        // short lines keep their size
        c.words = words({{0, 500, "Olá"}, {500, 2000, "mundo"}});
        c.lines = {QStringLiteral("Olá mundo")};
        QVERIFY(!toAss({c}, s, 1080, 1920, 30).contains(QLatin1String("\\fscx")));
    }

    void cuesFollowTheRules_data()
    {
        QTest::addColumn<int>("chars");
        QTest::addColumn<int>("lines");
        QTest::addColumn<double>("cps");
        QTest::newRow("vertical 2 lines") << 24 << 2 << 17.0;
        QTest::newRow("one line") << 32 << 1 << 17.0;
        QTest::newRow("narrow, slow") << 16 << 2 << 12.0;
    }
    void cuesFollowTheRules()
    {
        QFETCH(int, chars);
        QFETCH(int, lines);
        QFETCH(double, cps);
        Rules r;
        r.maxCharsPerLine = chars;
        r.maxLines = lines;
        r.maxCps = cps;
        const QList<Word> in = longTranscript();
        const QList<Cue> cues = buildCues(in, r);
        QVERIFY(cues.size() > 3);
        int total = 0;
        for (int i = 0; i < cues.size(); ++i) {
            const Cue &c = cues.at(i);
            total += c.words.size();
            QVERIFY(c.lines.size() <= lines);
            for (const QString &l : c.lines)
                QVERIFY2(l.size() <= chars, qPrintable(l));
            // display window covers the speech
            QVERIFY(c.startMs == c.words.first().startMs);
            QVERIFY(c.endMs >= c.words.last().endMs);
            QVERIFY(c.endMs - c.startMs >= r.minDurationMs || (i + 1 < cues.size() && c.endMs >= cues.at(i + 1).startMs - r.minGapMs));
            if (i + 1 < cues.size()) {
                QVERIFY2(c.endMs <= cues.at(i + 1).startMs, "cues overlap");
                // CPS holds whenever the pause after the cue leaves room for it
                const double room = cues.at(i + 1).startMs - r.minGapMs - c.startMs;
                if (room >= c.characters() * 1000.0 / cps)
                    QVERIFY2(c.characters() * 1000.0 / (c.endMs - c.startMs) <= cps + 0.01, qPrintable(c.text()));
            }
            // a cue never straddles a sentence end
            for (int k = 0; k + 1 < c.words.size(); ++k)
                QVERIFY2(!c.words.at(k).text.endsWith(QLatin1Char('.')), qPrintable(c.text()));
            if (c.lines.size() == 2 && c.words.size() >= 3)
                for (const QString &l : c.lines)
                    QVERIFY2(l.contains(QLatin1Char(' ')), qPrintable(c.text()));
        }
        QCOMPARE(total, in.size()); // nothing lost
    }

    void longPauseEndsACue()
    {
        Rules r;
        r.maxCharsPerLine = 40;
        const QList<Cue> cues = buildCues(words({{0, 300, "Olá"}, {300, 700, "gente"}, {3000, 3300, "tudo"}, {3300, 3800, "bem?"}}), r);
        QCOMPARE(cues.size(), 2);
        QCOMPARE(cues.at(0).text(), QStringLiteral("Olá gente"));
        QCOMPARE(cues.at(0).endMs, 833); // min duration
        QCOMPARE(cues.at(1).startMs, 3000);
    }

    void assKaraokeTimings()
    {
        Style s = Style::preset(QStringLiteral("karaoke"));
        Cue c;
        c.startMs = 1000;
        c.endMs = 3000;
        c.words = words({{1000, 1500, "Olá"}, {1700, 2500, "mundo"}});
        c.lines = {QStringLiteral("Olá mundo")};
        const QString ass = toAss({c}, s, 1080, 1920, 30);
        QVERIFY(ass.contains(QLatin1String("PlayResX: 1080")));
        QVERIFY(ass.contains(QLatin1String("PlayResY: 1920")));
        QVERIFY(ass.contains(QLatin1String("WrapStyle: 2")));
        // \kf fills from Secondary (white, unsung) to Primary (yellow #ffd60a → &H000AD6FF)
        QVERIFY2(ass.contains(QLatin1String("Style: Main,Inter,76,&H000AD6FF,&H00FFFFFF")), qPrintable(ass));
        // Olá: 50 cs sweep; pause 20 cs; mundo: 80 cs sweep
        QVERIFY2(ass.contains(QStringLiteral("Dialogue: 0,0:00:01.00,0:00:03.00,Main,,0,0,0,,{\\b800}{\\kf50}Olá {\\k20}{\\kf80}mundo")), qPrintable(ass));
    }

    void assTimesSnapToFrames()
    {
        // 1033.4 ms is frame 31 at 30 fps (1033.33 ms): the event must start at
        // or before that frame's time and after frame 30's (1000 ms)
        Style s = Style::preset(QStringLiteral("classic"));
        Cue c;
        c.startMs = 1033;
        c.endMs = 2067; // frame 62 (2066.67 ms)
        c.words = words({{1033, 2000, "Oi"}});
        c.lines = {QStringLiteral("Oi")};
        const QString ass = toAss({c}, s, 1920, 1080, 30);
        QVERIFY2(ass.contains(QLatin1String("Dialogue: 0,0:00:01.03,0:00:02.06,Main")), qPrintable(ass));
        // 29.97 fps: frame 30 = 1001 ms → 1.00 (floored), never 1.01 (that would miss the frame)
        c.startMs = 1001;
        c.endMs = 2002;
        const QString ntsc = toAss({c}, s, 1920, 1080, 30000.0 / 1001);
        QVERIFY2(ntsc.contains(QLatin1String("Dialogue: 0,0:00:01.00,0:00:02.00,Main")), qPrintable(ntsc));
    }

    void assEffects()
    {
        Cue c;
        c.startMs = 0;
        c.endMs = 2000;
        c.words = words({{0, 400, "um"}, {500, 900, "dois"}, {1000, 1400, "três"}, {1500, 1900, "quatro"}});
        c.lines = {QStringLiteral("um dois"), QStringLiteral("três quatro")};
        auto events = [](const QString &ass) { return ass.count(QStringLiteral("\nDialogue:")); };
        // Destaque: base line + one pill event per word
        const QString hl = toAss({c}, Style::preset(QStringLiteral("highlight")), 1080, 1920, 30);
        QCOMPARE(events(hl), 5);
        QVERIFY(hl.contains(QLatin1String("Style: Pill,")));
        QVERIFY(hl.contains(QLatin1String("{\\alpha&H00&}dois{\\alpha&HFF&}\\N")));
        // Palavra a palavra: one event per word, uppercase, with a pop
        const QString ww = toAss({c}, Style::preset(QStringLiteral("word")), 1080, 1920, 30);
        QCOMPARE(events(ww), 4);
        QVERIFY(ww.contains(QStringLiteral("TRÊS")));
        QVERIFY(ww.contains(QLatin1String("\\t(0,90,")));
        // Podcast: uppercase with a coloured keyword
        const QString pc = toAss({c}, Style::preset(QStringLiteral("podcast")), 1080, 1920, 30);
        QVERIFY(pc.contains(QLatin1String("{\\1c&H0AD6FF&}QUATRO{\\1c&HFFFFFF&}")));
        // Caixa / Alto contraste: opaque box border style
        QVERIFY(toAss({c}, Style::preset(QStringLiteral("box")), 1080, 1920, 30).contains(QLatin1String(",0,0,3,")));
        QVERIFY(toAss({c}, Style::preset(QStringLiteral("contrast")), 1080, 1920, 30).contains(QLatin1String("&H0000FFFF"))); // yellow
        // Neon: glow layer under the text
        QCOMPARE(events(toAss({c}, Style::preset(QStringLiteral("neon")), 1080, 1920, 30)), 2);
        // Duas cores: speaker 2 uses its own style
        c.speaker = 2;
        QVERIFY(toAss({c}, Style::preset(QStringLiteral("twocolor")), 1080, 1920, 30).contains(QLatin1String(",Speaker2,,")));
        // the line break comes from the cue's lines
        QVERIFY(toAss({c}, Style::preset(QStringLiteral("classic")), 1080, 1920, 30).contains(QStringLiteral("um dois\\Ntrês quatro")));
        // user text can't inject override tags
        c.words = words({{0, 400, "{\\pos(0,0)}x"}});
        c.lines = {QStringLiteral("{\\pos(0,0)}x")};
        QVERIFY(!toAss({c}, Style::preset(QStringLiteral("classic")), 1080, 1920, 30).contains(QLatin1String("{\\pos")));
    }

    void safeAreaMargins()
    {
        Style s = Style::preset(QStringLiteral("classic"));
        s.safeArea = {0.14, 0.35, 0.06, 0.06}; // Reels
        const QString ass = toAss({}, s, 1080, 1920, 30);
        // MarginV = (0.35 + 0.03) * 1920 = 730; MarginL/R = (0.06 + 0.04) * 1080 = 108
        QVERIFY2(ass.contains(QLatin1String(",2,108,108,730,1")), qPrintable(ass));
        // fewer characters per line on a vertical frame than on a horizontal one
        QVERIFY(s.charsPerLine(1080, 1920) < Style::preset(QStringLiteral("classic")).charsPerLine(1920, 1080));
    }

    void sidecars()
    {
        Cue c;
        c.startMs = 1500;
        c.endMs = 3250;
        c.words = words({{1500, 2000, "Olá"}, {2000, 3000, "mundo"}});
        c.lines = {QStringLiteral("Olá"), QStringLiteral("mundo")};
        QCOMPARE(toSrt({c}), QStringLiteral("1\n00:00:01,500 --> 00:00:03,250\nOlá\nmundo\n\n"));
        QCOMPARE(toVtt({c}), QStringLiteral("WEBVTT\n\n00:00:01.500 --> 00:00:03.250\nOlá\nmundo\n\n"));
    }

    void styleRoundTrip()
    {
        for (const Style &s : Style::presets()) {
            const Style back = Style::fromMap(s.toMap());
            QCOMPARE(back.toMap(), s.toMap());
            QVERIFY(!Style::presetName(s.id).isEmpty());
        }
        QCOMPARE(Style::presets().size(), 9);
        QVariantMap m = Style::preset(QStringLiteral("karaoke")).toMap();
        m[QStringLiteral("size")] = 9.0;
        m[QStringLiteral("activeColor")] = QStringLiteral("#ff0000");
        const Style s = Style::fromMap(m);
        QCOMPARE(s.effect, QStringLiteral("karaoke"));
        QCOMPARE(s.size, 9.0);
        QCOMPARE(s.activeColor, QStringLiteral("#ff0000"));
    }

    void captionClipsStayInSync()
    {
        const double fps = 30;
        Cue c;
        c.startMs = 1000;
        c.endMs = 3000;
        c.words = words({{1000, 1400, "editar"}, {1500, 2000, "é"}, {2100, 2900, "fácil."}});
        Timeline tl;
        Track t;
        t.id = QStringLiteral("s");
        t.kind = TrackKind::Subtitle;
        t.clips << makeClip(c, fps);
        tl.tracks << t;
        const QString id = t.clips.first().id;
        QCOMPARE(tl.clip(id)->position, 30);
        QCOMPARE(tl.clip(id)->duration(), 60);
        const Style style;
        auto cues = [&] { return trackCues(tl.tracks.first(), fps, style, 1080, 1920); };
        QCOMPARE(cues().first().words.first().startMs, 1000);

        // moving the cue moves its words
        QVERIFY(Ops::moveClip(tl, id, 0, 60));
        QCOMPARE(cues().first().startMs, 2000);
        QCOMPARE(cues().first().words.first().startMs, 2000);
        QCOMPARE(cues().first().words.last().endMs, 3900);

        // dragging the left edge hides the first word without losing it
        QVERIFY(Ops::trimIn(tl, id, 75, [](const Clip &) { return Ops::Unlimited; }));
        QCOMPARE(cues().first().words.size(), 2);
        QCOMPARE(cues().first().words.first().text, QStringLiteral("é"));
        QVERIFY(Ops::trimIn(tl, id, 60, [](const Clip &) { return Ops::Unlimited; }));
        QCOMPARE(cues().first().words.size(), 3);

        // fixing the spelling keeps every timing
        QVERIFY(Ops::setCaptionText(tl, id, QStringLiteral("Editar é fácil!"), fps));
        QCOMPARE(cues().first().words.at(2).text, QStringLiteral("fácil!"));
        QCOMPARE(cues().first().words.at(2).startMs, 3100);
        // a different word count spreads the words over the spoken span
        QVERIFY(Ops::setCaptionText(tl, id, QStringLiteral("Editar vídeo é fácil!"), fps));
        QCOMPARE(cues().first().words.size(), 4);
        QCOMPARE(cues().first().words.first().startMs, 2000);
        QCOMPARE(cues().first().words.last().endMs, 3900);

        // split before word 2, then merge back
        const QString right = Ops::splitCaption(tl, id, 2, fps);
        QVERIFY(!right.isEmpty());
        QCOMPARE(tl.tracks.first().clips.size(), 2);
        QCOMPARE(clipWords(*tl.clip(id)).size(), 2);
        QCOMPARE(clipWords(*tl.clip(right)).size(), 2);
        // the new cue starts on the frame of its first word
        QCOMPARE(tl.clip(right)->position, int(std::lround(cues().at(1).words.first().startMs * fps / 1000.0)));
        QVERIFY(Ops::mergeCaptionWithNext(tl, id, fps));
        QCOMPARE(tl.tracks.first().clips.size(), 1);
        QCOMPARE(cues().first().words.size(), 4);
        QCOMPARE(cues().first().words.last().endMs, 3900);

        // nudging a word stays between its neighbours
        QVERIFY(Ops::nudgeCaptionWord(tl, id, 1, 100));
        QVERIFY(clipWords(*tl.clip(id)).at(1).startMs < clipWords(*tl.clip(id)).at(2).startMs);
        QVERIFY(!Ops::nudgeCaptionWord(tl, id, 0, -100000) || clipWords(*tl.clip(id)).at(0).startMs < clipWords(*tl.clip(id)).at(1).startMs);

        // speakers and keywords
        QVERIFY(Ops::setCaptionSpeaker(tl, id, 2));
        QCOMPARE(cues().first().speaker, 2);
        QVERIFY(Ops::toggleCaptionEmphasis(tl, id, 3));
        QVERIFY(clipWords(*tl.clip(id)).at(3).emphasis);

        // the razor keeps each word on one side
        const QString r2 = Ops::split(tl, id, 90);
        QVERIFY(!r2.isEmpty());
        QCOMPARE(clipWords(*tl.clip(id)).size() + clipWords(*tl.clip(r2)).size(), 4);
    }

    void projectRoundTrip()
    {
        ProjectData d;
        d.settings = ProjectSettings::preset(QStringLiteral("vertical1080"));
        d.doc.timeline = Timeline::defaultTimeline();
        Track s;
        s.id = QStringLiteral("subs");
        s.kind = TrackKind::Subtitle;
        s.name = QStringLiteral("S1");
        s.captionStyle = Style::preset(QStringLiteral("karaoke")).toMap();
        for (const Cue &c : buildCues(longTranscript().mid(0, 10), Style().effectiveRules(1080, 1920)))
            s.clips << makeClip(c, 30);
        d.doc.timeline.tracks.prepend(s);
        const QByteArray xml = ProjectIO::toXml(d);
        QVERIFY(xml.contains("avfilter.ass"));
        ProjectData back;
        QString err;
        QVERIFY2(ProjectIO::loadXml(xml, &back, &err), qPrintable(err));
        const Track &bt = back.doc.timeline.tracks.first();
        QCOMPARE(bt.kind, TrackKind::Subtitle);
        QCOMPARE(bt.captionStyle, s.captionStyle);
        QCOMPARE(bt.clips.size(), s.clips.size());
        for (int i = 0; i < s.clips.size(); ++i) {
            QCOMPARE(bt.clips.at(i).position, s.clips.at(i).position);
            QCOMPARE(clipWords(bt.clips.at(i)), clipWords(s.clips.at(i)));
        }
    }

    // The whole point: what the preview shows at a frame is what the export
    // renders, and karaoke sweeps follow the word times frame by frame.
    void renderedFramesMatchWordTimes()
    {
        initMlt();
        QTemporaryDir tmp;
        if (qEnvironmentVariableIsSet("KEEP_TEST_FILES")) {
            tmp.setAutoRemove(false);
            qInfo() << "files in" << tmp.path();
        }
        ProjectData d;
        d.settings.width = 540;
        d.settings.height = 960;
        d.settings.fpsNum = 30;
        d.doc.timeline = Timeline::defaultTimeline();
        Track s;
        s.id = QStringLiteral("subs");
        s.kind = TrackKind::Subtitle;
        Style style = Style::preset(QStringLiteral("karaoke"));
        style.textColor = QStringLiteral("#ffffff");
        style.activeColor = QStringLiteral("#ffff00");
        s.captionStyle = style.toMap();
        Cue c;
        c.startMs = 1000; // frame 30
        c.endMs = 3000;   // frames 30..89
        c.words = words({{1000, 1500, "Olá"}, {1700, 2500, "mundo"}}); // Olá sweeps over frames 30-45, mundo 51-75
        s.clips << makeClip(c, 30);
        d.doc.timeline.tracks.prepend(s);
        // some video under the captions
        Clip bg;
        bg.id = QStringLiteral("bg");
        bg.kind = QStringLiteral("color");
        bg.genParams[QStringLiteral("color")] = QStringLiteral("#203040");
        bg.position = 0;
        bg.out = 119;
        d.doc.timeline.tracks[1].clips << bg;

        PlanOptions opts;
        opts.captionDir = tmp.path();
        auto counts = [](const QImage &img) {
            int yellow = 0, white = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x) {
                    const QRgb p = img.pixel(x, y);
                    if (qRed(p) > 200 && qGreen(p) > 200 && qBlue(p) < 90)
                        ++yellow;
                    else if (qRed(p) > 200 && qGreen(p) > 200 && qBlue(p) > 200)
                        ++white;
                }
            return QPair<int, int>(yellow, white);
        };

        // ---- preview path: live MLT objects, rendered at half size like the player
        Mlt::Profile profile;
        profile.set_width(270);
        profile.set_height(480);
        profile.set_frame_rate(30, 1);
        profile.set_sample_aspect(1, 1);
        profile.set_display_aspect(9, 16);
        profile.set_progressive(1);
        profile.set_explicit(1);
        opts.scale = 0.5;
        MltBuilder builder(profile);
        const RenderPlan plan = Planner(d.settings, d.media, d.doc.sequences, opts).plan(d.doc.timeline);
        std::unique_ptr<Mlt::Tractor> tractor = builder.build(plan);
        QVERIFY2(builder.errors().isEmpty(), qPrintable(builder.errors().join(QLatin1Char('\n'))));
        auto live = [&](int pos) {
            tractor->seek(pos);
            std::unique_ptr<Mlt::Frame> frame(tractor->get_frame());
            mlt_image_format fmt = mlt_image_rgba;
            int w = 270, h = 480;
            const uint8_t *img = frame->get_image(fmt, w, h);
            return QImage(img, w, h, w * 4, QImage::Format_RGBA8888).copy();
        };
        QHash<int, QPair<int, int>> preview;
        for (int f : {29, 30, 31, 38, 44, 45, 46, 47, 50, 51, 52, 53, 63, 75, 76, 89, 90})
            preview[f] = counts(live(f));

        QCOMPARE(preview[29], qMakePair(0, 0));  // nothing before the cue
        QVERIFY(preview[30].second > 50);        // appears on its first frame, all white
        QCOMPARE(preview[30].first, 0);
        QVERIFY(preview[31].first > 0);          // the sweep has started
        QVERIFY(preview[38].first > preview[31].first);
        QVERIFY(preview[46].first >= preview[45].first);
        QCOMPARE(preview[50].first, preview[46].first); // pause: nothing moves
        QVERIFY(preview[63].first > preview[51].first);  // "mundo" filling
        QCOMPARE(preview[76].second, 0);         // everything sung
        QVERIFY(preview[89].first > 0);          // still shown on its last frame
        QCOMPARE(preview[90], qMakePair(0, 0));  // gone right after

        // ---- export path: the same plan as MLT XML, rendered by melt at full size
        if (QStandardPaths::findExecutable(QStringLiteral("melt")).isEmpty())
            QSKIP("melt not installed");
        PlanOptions xo = opts;
        xo.scale = 1.0;
        const RenderPlan full = Planner(d.settings, d.media, d.doc.sequences, xo).plan(d.doc.timeline);
        const QString project = tmp.filePath(QStringLiteral("p.mlt"));
        QFile pf(project);
        QVERIFY(pf.open(QIODevice::WriteOnly));
        pf.write(writeMltXml(full, QStringLiteral("t"), {}));
        pf.close();
        QProcess melt;
        melt.setProcessChannelMode(QProcess::MergedChannels);
        melt.start(QStringLiteral("melt"), {QStringLiteral("-silent"), project, QStringLiteral("-consumer"),
                                            QStringLiteral("avformat:") + tmp.filePath(QStringLiteral("f%03d.png")), QStringLiteral("vcodec=png"),
                                            QStringLiteral("width=270"), QStringLiteral("height=480"),
                                            // as Exporter::meltArguments does: explicit rate for filters made while loading
                                            QStringLiteral("frame_rate_num=30"), QStringLiteral("frame_rate_den=1"),
                                            QStringLiteral("real_time=-4"), QStringLiteral("threads=4")});
        QVERIFY(melt.waitForFinished(120000));
        // Same renderer, same frame: the caption shows on the same frames and the
        // karaoke fill (sung share of the text) matches. Pixel counts differ a
        // little between 270x480 rendering and a 540x960 render scaled down.
        QStringList bad;
        for (auto it = preview.cbegin(); it != preview.cend(); ++it) {
            const QImage img(tmp.filePath(QStringLiteral("f%1.png").arg(it.key() + 1, 3, 10, QLatin1Char('0'))));
            QVERIFY2(!img.isNull(), qPrintable(QString::number(it.key())));
            const auto exported = counts(img.convertToFormat(QImage::Format_RGB32));
            const auto share = [](QPair<int, int> c) { return c.first + c.second > 0 ? double(c.first) / (c.first + c.second) : -1.0; };
            const bool shown = it.value().first + it.value().second > 0;
            const bool exportShown = exported.first + exported.second > 0;
            if (qEnvironmentVariableIsSet("KEEP_TEST_FILES"))
                qInfo() << it.key() << "preview" << it.value() << share(it.value()) << "export" << exported << share(exported);
            if (shown != exportShown || std::abs(share(it.value()) - share(exported)) > 0.06)
                bad << QStringLiteral("frame %1: preview %2/%3, export %4/%5").arg(it.key()).arg(it.value().first).arg(it.value().second)
                           .arg(exported.first).arg(exported.second);
        }
        QVERIFY2(bad.isEmpty(), qPrintable(bad.join(QLatin1Char('\n'))));
    }
};

QTEST_MAIN(TestCaptions)
#include "tst_captions.moc"
