/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Local analysis: SRT parsing/writing, transcript search, and ffmpeg-based
// silence, scene-cut and loudness detection on generated media.

#include "aitools.h"
#include "testmedia.h"

#include <QTest>
#include <cmath>

using namespace Studio;
using namespace Studio::AiTools;

class TestAiTools : public QObject
{
    Q_OBJECT

    static QString runFfmpeg(const QStringList &args)
    {
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(QStringLiteral("ffmpeg"), args);
        p.waitForFinished(60000);
        return QString::fromUtf8(p.readAll());
    }

private Q_SLOTS:
    void srtRoundTrip()
    {
        const QString srt = QStringLiteral("1\r\n00:00:01,000 --> 00:00:02,500\r\nOlá mundo\r\n\r\n"
                                           "2\n00:01:00.25 --> 00:01:03,000\nsegunda\nlinha\n\n");
        const QList<SubtitleCue> cues = parseSrt(srt);
        QCOMPARE(cues.size(), 2);
        QCOMPARE(cues.at(0).startMs, 1000);
        QCOMPARE(cues.at(0).endMs, 2500);
        QCOMPARE(cues.at(0).text, QStringLiteral("Olá mundo"));
        QCOMPARE(cues.at(1).startMs, 60250);
        QCOMPARE(cues.at(1).text, QStringLiteral("segunda\nlinha"));
        QCOMPARE(parseSrt(writeSrt(cues)), cues);
    }

    void searchTranscript()
    {
        const QList<SubtitleCue> cues = {{0, 1000, QStringLiteral("Bem-vindo ao Lingmo")}, {1000, 2000, QStringLiteral("Edição de VÍDEO rápida")}};
        QCOMPARE(search(cues, QStringLiteral("video edicao")).size(), 1);
        QCOMPARE(search(cues, QStringLiteral("lingmo")).first().startMs, 0);
        QVERIFY(search(cues, QStringLiteral("nada")).isEmpty());
    }

    void whisperArgs()
    {
        WhisperEngine e{QStringLiteral("whisper.cpp"), QStringLiteral("/usr/bin/whisper-cli"), QStringLiteral("/m/ggml-base.bin")};
        const QStringList a = whisperArguments(e, QStringLiteral("/t/a.wav"), QStringLiteral("/t/a"), QStringLiteral("pt"));
        QVERIFY(a.contains(QStringLiteral("-osrt")));
        QCOMPARE(a.at(a.indexOf(QStringLiteral("-l")) + 1), QStringLiteral("pt"));
        QVERIFY(whisperArguments(WhisperEngine{}, {}, {}, {}).isEmpty());
    }

    void silence()
    {
        if (!TestMedia::haveFfmpeg())
            QSKIP("needs ffmpeg");
        const QList<Interval> gaps = parseSilence(runFfmpeg(silenceArguments(TestMedia::speech())));
        QCOMPARE(gaps.size(), 1);
        QVERIFY(std::abs(gaps.first().start - 2.0) < 0.1);
        QVERIFY(std::abs(gaps.first().end - 4.0) < 0.1);
    }

    void scenes()
    {
        if (!TestMedia::haveFfmpeg())
            QSKIP("needs ffmpeg");
        const QList<double> cuts = parseScenes(runFfmpeg(sceneArguments(TestMedia::scenes())));
        QCOMPARE(cuts.size(), 1);
        QVERIFY(std::abs(cuts.first() - 3.0) < 0.1);
    }

    void loudness()
    {
        if (!TestMedia::haveFfmpeg())
            QSKIP("needs ffmpeg");
        const double lufs = parseIntegratedLoudness(runFfmpeg(loudnessArguments(TestMedia::speech())));
        QVERIFY(!std::isnan(lufs));
        QVERIFY(lufs < -3 && lufs > -30);
        QVERIFY(std::isnan(parseIntegratedLoudness(QStringLiteral("nothing"))));
    }
};

QTEST_GUILESS_MAIN(TestAiTools)
#include "tst_aitools.moc"
