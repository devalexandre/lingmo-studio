/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Transcriber and this computer: whisper-cli options, time left, the model
// that runs (configured, per-run override, fallback) and whether it suits the
// hardware. Uses a scratch HOME/XDG_CONFIG_HOME and a fake whisper-cli.

#include "transcriber.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

namespace {

const QString Scratch = QStringLiteral(TEST_SCRATCH "/transcriber");

void touch(const QString &path, bool executable = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(executable ? "#!/bin/sh\nexit 0\n" : "model");
    f.close();
    if (executable)
        f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
}

WhisperHw::Hardware rtx2060(bool cuda)
{
    return WhisperHw::fromSpec(QStringLiteral("ram=32,cores=6,threads=12,avx2=1,gpu=nvidia,gpuname=NVIDIA GeForce RTX 2060,vram=6")
                               + (cuda ? QStringLiteral(",backends=cuda") : QString()));
}

void setModel(const QString &name)
{
    QSettings ai(QStringLiteral("lingmoos"), QStringLiteral("ai"));
    ai.setValue(QStringLiteral("subtitle_model"), name);
    ai.setValue(QStringLiteral("subtitle_language"), QStringLiteral("pt"));
    ai.sync();
}

} // namespace

class TestTranscriber : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QDir(Scratch).removeRecursively();
        qputenv("HOME", (Scratch + QStringLiteral("/home")).toUtf8());
        qputenv("XDG_CONFIG_HOME", (Scratch + QStringLiteral("/home/.config")).toUtf8());
        qputenv("XDG_CACHE_HOME", (Scratch + QStringLiteral("/home/.cache")).toUtf8());
        qputenv("PATH", (Scratch + QStringLiteral("/bin")).toUtf8());
        qputenv("LINGMO_WHISPER_HW", WhisperHw::toSpec(rtx2060(false)).toUtf8());
        touch(Scratch + QStringLiteral("/bin/whisper-cli"), true);
        QCOMPARE(Transcriber::modelsDir(), Scratch + QStringLiteral("/home/.local/share/lingmo/whisper-models"));
    }

    void cliArguments()
    {
        Transcriber::Engine e;
        e.kind = QStringLiteral("whisper.cpp");
        e.modelFile = QStringLiteral("/m/ggml-large-v3-turbo.bin");
        e.language = QStringLiteral("pt");
        const QStringList gpu = Transcriber::arguments(e, QStringLiteral("/t/a.wav"), QStringLiteral("/t/a"), rtx2060(true));
        for (const char *flag : {"-pp", "-np", "-ojf", "-nfa", "-dtw", "-mc", "-sns"})
            QVERIFY2(gpu.contains(QLatin1String(flag)), flag);
        QCOMPARE(gpu.at(gpu.indexOf(QStringLiteral("-dtw")) + 1), QStringLiteral("large.v3.turbo")); // from the file name
        QCOMPARE(gpu.at(gpu.indexOf(QStringLiteral("-m")) + 1), e.modelFile);
        QCOMPARE(gpu.at(gpu.indexOf(QStringLiteral("-l")) + 1), QStringLiteral("pt"));
        QVERIFY(!gpu.contains(QStringLiteral("-ng")));

        const QStringList cpu = Transcriber::arguments(e, QStringLiteral("/t/a.wav"), QStringLiteral("/t/a"), rtx2060(false));
        QVERIFY(cpu.contains(QStringLiteral("-ng")));
        QVERIFY(cpu.contains(QStringLiteral("-pp")));
        QCOMPARE(cpu.at(cpu.indexOf(QStringLiteral("-t")) + 1), QStringLiteral("12"));
        QCOMPARE(cpu.at(cpu.indexOf(QStringLiteral("-bs")) + 1), QStringLiteral("1"));
    }

    void timeLeft()
    {
        // from the pace once there is progress
        QCOMPARE(Transcriber::remaining(60, 0.5, 600, 2), 60.0);
        QCOMPARE(Transcriber::remaining(30, 0.25, 0, 0), 90.0);
        // before: audio length / estimated speed
        QCOMPARE(Transcriber::remaining(0, 0, 600, 2), 300.0);
        QCOMPARE(Transcriber::remaining(100, 0.01, 600, 2), 297.0);
        QCOMPARE(Transcriber::remaining(10, 1, 600, 2), 0.0);
        QCOMPARE(Transcriber::remaining(10, 0, 0, 0), -1.0);
        QCOMPARE(Transcriber::clockText(1109), QStringLiteral("18:29"));
        QCOMPARE(Transcriber::clockText(3723), QStringLiteral("1:02:03"));
        QCOMPARE(Transcriber::clockText(5), QStringLiteral("0:05"));
        QCOMPARE(Transcriber::parseProgress(QStringLiteral("whisper_print_progress_callback: progress =  37%"), 0), 0.37);
    }

    void modelForThisComputer()
    {
        const QString dir = Transcriber::modelsDir();
        touch(dir + QStringLiteral("/ggml-large-v3-turbo.bin"));
        setModel(QStringLiteral("large-v3-turbo"));

        Transcriber t;
        t.setHardware(rtx2060(false)); // RTX 2060 without ggml-cuda
        QVERIFY(t.available());
        QCOMPARE(t.runModel(), QStringLiteral("large-v3-turbo"));
        QCOMPARE(t.device(), QStringLiteral("CPU"));
        QVERIFY(!t.gpu());
        QVERIFY(t.unsuitable());
        QVERIFY(t.summary().startsWith(QStringLiteral("large-v3-turbo · CPU · ")));
        QCOMPARE(t.recommendedModel(), WhisperHw::recommend(rtx2060(false)).model);
        QVERIFY(!t.recommendedInstalled());
        QCOMPARE(t.gpuPackage(), QStringLiteral("ggml-cuda"));
        QVERIFY(t.gpuHint().contains(QStringLiteral("ggml-cuda")));

        // the recommended model shows up in the per-run choice even before it is downloaded
        bool listed = false;
        for (const QVariant &v : t.modelChoices())
            listed |= v.toMap().value(QStringLiteral("name")).toString() == t.recommendedModel();
        QVERIFY(listed);

        // ggml-cuda installed: the same model on the GPU is fine
        t.setHardware(rtx2060(true));
        QVERIFY(!t.unsuitable());
        QVERIFY(t.gpu());
        QCOMPARE(t.device(), QStringLiteral("NVIDIA GeForce RTX 2060"));
        QVERIFY(t.summary().startsWith(QStringLiteral("large-v3-turbo · NVIDIA GeForce RTX 2060 · ")));
        QVERIFY(t.gpuPackage().isEmpty());

        // per-run override
        t.setHardware(rtx2060(false));
        const QString light = WhisperHw::recommend(rtx2060(false)).model;
        touch(dir + QStringLiteral("/ggml-%1.bin").arg(light));
        t.setModelOverride(light);
        QCOMPARE(t.runModel(), light);
        QVERIFY(!t.unsuitable());
        QVERIFY(t.recommendedInstalled());
        t.setModelOverride(QString());
        QCOMPARE(t.runModel(), QStringLiteral("large-v3-turbo"));

        // "Use the recommended" writes subtitle_model like Settings does
        t.useRecommended();
        QCOMPARE(t.runModel(), light);
        QCOMPARE(QSettings(QStringLiteral("lingmoos"), QStringLiteral("ai")).value(QStringLiteral("subtitle_model")).toString(), light);

        // configured model missing: the best downloaded one that suits the computer
        setModel(QStringLiteral("medium"));
        t.refresh();
        QCOMPARE(t.runModel(), light);
    }

    void gpuInstallFails()
    {
        // pkexec isn't in this PATH: the failure is reported, nothing hangs
        Transcriber t;
        t.setHardware(rtx2060(false));
        QSignalSpy spy(&t, &Transcriber::installChanged);
        t.installGpuSupport();
        QTRY_VERIFY(!t.installing());
        QVERIFY(t.installFailed());
        QVERIFY(t.installMessage().contains(QStringLiteral("ggml-cuda")));
    }
};

QTEST_GUILESS_MAIN(TestTranscriber)
#include "tst_transcriber.moc"
