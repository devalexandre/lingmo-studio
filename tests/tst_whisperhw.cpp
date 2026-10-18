/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// whisperhw: hardware probe (on a fake root), ggml backend detection, the
// model recommendation for CPU only / GPU / GPU without backend, speed
// estimates, whisper-cli options and the install command.
// The same test lives in lingmo-studio and lingmo-settings (see whisperhw.h).

#include "whisperhw.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

using namespace WhisperHw;

namespace {

void writeFile(const QString &path, const QByteArray &data, bool executable = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
    f.close();
    if (executable)
        f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
}

// /proc/cpuinfo of a 6-core / 12-thread AVX2 processor (Ryzen 5 1600)
QByteArray ryzenCpuinfo()
{
    QByteArray text;
    for (int i = 0; i < 12; ++i) {
        text += "processor\t: " + QByteArray::number(i) + "\n"
                "vendor_id\t: AuthenticAMD\n"
                "model name\t: AMD Ryzen 5 1600 Six-Core Processor\n"
                "physical id\t: 0\n"
                "core id\t\t: " + QByteArray::number(i / 2) + "\n"
                "cpu cores\t: 6\n"
                "flags\t\t: fpu vme sse sse2 avx fma avx2 sha_ni\n\n";
    }
    return text;
}

Hardware machine(double ram, int cores, bool avx2, const QString &gpu = {}, double vram = 0, const QStringList &backends = {})
{
    Hardware hw;
    hw.ramGb = ram;
    hw.cores = cores;
    hw.threads = cores * 2;
    hw.avx2 = avx2;
    hw.gpuVendor = gpu;
    hw.gpuName = gpu.isEmpty() ? QString() : vendorDisplayName(gpu) + QStringLiteral(" Test GPU");
    hw.vramGb = vram;
    hw.backends = backends;
    return hw;
}

// The machine this was tuned on: Ryzen 5 1600, 32 GB, RTX 2060 6 GB
Hardware rtx2060(const QStringList &backends)
{
    Hardware hw = machine(32, 6, true, QStringLiteral("nvidia"), 6, backends);
    hw.gpuName = QStringLiteral("NVIDIA GeForce RTX 2060");
    return hw;
}

} // namespace

class TestWhisperHw : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsers()
    {
        Hardware hw;
        parseCpuinfo(QString::fromLatin1(ryzenCpuinfo()), hw);
        QCOMPARE(hw.cores, 6);
        QCOMPARE(hw.threads, 12);
        QVERIFY(hw.avx2);
        QVERIFY(!hw.avx512);
        QCOMPARE(hw.cpuName, QStringLiteral("AMD Ryzen 5 1600 Six-Core Processor"));

        Hardware xeon;
        parseCpuinfo(QStringLiteral("processor : 0\ncpu cores : 4\nflags : sse avx2 avx512f avx512bw\n\nprocessor : 1\n"), xeon);
        QCOMPARE(xeon.cores, 4); // no core ids: "cpu cores"
        QCOMPARE(xeon.threads, 2);
        QVERIFY(xeon.avx512);

        QCOMPARE(qRound(parseMeminfoGb(QStringLiteral("MemTotal:       32785192 kB\nMemFree: 1 kB\n"))), 31);
        QCOMPARE(parseMeminfoGb(QStringLiteral("nothing")), 0.0);

        QString name;
        double vram = 0;
        QVERIFY(parseNvidiaSmi(QStringLiteral("NVIDIA GeForce RTX 2060, 6144\n"), &name, &vram));
        QCOMPARE(name, QStringLiteral("NVIDIA GeForce RTX 2060"));
        QCOMPARE(vram, 6.0);
        QVERIFY(parseNvidiaSmi(QStringLiteral("NVIDIA T400, 2048\nNVIDIA GeForce RTX 3090, 24576\n"), &name, &vram));
        QCOMPARE(name, QStringLiteral("NVIDIA GeForce RTX 3090"));
        QVERIFY(!parseNvidiaSmi(QStringLiteral("NVIDIA-SMI has failed"), &name, &vram));
    }

    void backendFiles()
    {
        // ggml 0.26 file names (pacman -Fl ggml-cuda ggml-vulkan ggml-hip ggml-sycl)
        QCOMPARE(backendsFromFiles({QStringLiteral("libggml-cpu-haswell.so"), QStringLiteral("libggml-cpu-x64.so"),
                                    QStringLiteral("libggml-base.so"), QStringLiteral("libggml-blas.so")}),
                 QStringList());
        QCOMPARE(backendsFromFiles({QStringLiteral("libggml-vulkan.so"), QStringLiteral("libggml-cuda.so")}),
                 (QStringList{QStringLiteral("cuda"), QStringLiteral("vulkan")}));
        QCOMPARE(backendsFromFiles({QStringLiteral("/usr/lib/ggml/libggml-hip.so"), QStringLiteral("libggml-sycl.so.0.26.0")}),
                 (QStringList{QStringLiteral("hip"), QStringLiteral("sycl")}));
        QCOMPARE(backendPackage(QStringLiteral("nvidia")), QStringLiteral("ggml-cuda"));
        QCOMPARE(backendPackage(QStringLiteral("amd")), QStringLiteral("ggml-vulkan"));
        QCOMPARE(backendPackage(QStringLiteral("intel")), QStringLiteral("ggml-vulkan"));
    }

    void probeFakeRoot()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString r = root.path();
        writeFile(r + QStringLiteral("/proc/cpuinfo"), ryzenCpuinfo());
        writeFile(r + QStringLiteral("/proc/meminfo"), "MemTotal:       16384000 kB\n");
        // an AMD card with 8 GiB and an Intel iGPU; connectors are skipped
        writeFile(r + QStringLiteral("/sys/class/drm/card0/device/vendor"), "0x8086\n");
        writeFile(r + QStringLiteral("/sys/class/drm/card1/device/vendor"), "0x1002\n");
        writeFile(r + QStringLiteral("/sys/class/drm/card1/device/mem_info_vram_total"), "8589934592\n");
        writeFile(r + QStringLiteral("/sys/class/drm/card1/device/product_name"), "AMD Radeon RX 6600\n");
        writeFile(r + QStringLiteral("/sys/class/drm/card1-DP-1/device/vendor"), "0x10de\n");
        writeFile(r + QStringLiteral("/usr/lib/ggml/libggml-cpu-haswell.so"), "");

        Hardware hw = probe(r);
        QCOMPARE(hw.cores, 6);
        QCOMPARE(qRound(hw.ramGb), 16);
        QCOMPARE(hw.gpuVendor, QStringLiteral("amd"));
        QCOMPARE(hw.gpuName, QStringLiteral("AMD Radeon RX 6600"));
        QCOMPARE(hw.vramGb, 8.0);
        QVERIFY(hw.backends.isEmpty());
        QVERIFY(!hw.gpuUsable());

        writeFile(r + QStringLiteral("/usr/lib/ggml/libggml-vulkan.so"), "");
        hw = probe(r);
        QCOMPARE(hw.backends, QStringList{QStringLiteral("vulkan")});
        QCOMPARE(hw.gpuBackend(), QStringLiteral("vulkan"));
        QVERIFY(hw.gpuUsable());

        // NVIDIA without nvidia-smi: found by its PCI vendor
        writeFile(r + QStringLiteral("/sys/class/drm/card2/device/vendor"), "0x10de\n");
        hw = probe(r);
        QCOMPARE(hw.gpuVendor, QStringLiteral("nvidia"));
    }

    void spec()
    {
        const Hardware hw = fromSpec(QStringLiteral("ram=32,cores=6,threads=12,avx2=1,gpu=nvidia,gpuname=NVIDIA GeForce RTX 2060,vram=6,backends=cuda+vulkan"));
        QCOMPARE(hw.cores, 6);
        QCOMPARE(hw.gpuName, QStringLiteral("NVIDIA GeForce RTX 2060"));
        QCOMPARE(hw.backends, (QStringList{QStringLiteral("cuda"), QStringLiteral("vulkan")}));
        QCOMPARE(hw.gpuBackend(), QStringLiteral("cuda"));
        const Hardware again = fromSpec(toSpec(hw));
        QCOMPARE(again.gpuName, hw.gpuName);
        QCOMPARE(again.backends, hw.backends);
        QCOMPARE(again.avx2, true);
    }

    void recommendation_data()
    {
        QTest::addColumn<QString>("spec");
        QTest::addColumn<QString>("model");
        QTest::addColumn<bool>("gpu");
        QTest::addColumn<QString>("upgradePackage");
        QTest::addColumn<QString>("upgradeModel");

        // GPU usable
        QTest::newRow("rtx2060 + cuda") << "ram=32,cores=6,avx2=1,gpu=nvidia,vram=6,backends=cuda" << "large-v3-turbo" << true << "" << "";
        QTest::newRow("nvidia + vulkan") << "ram=16,cores=4,avx2=1,gpu=nvidia,vram=8,backends=vulkan" << "large-v3-turbo" << true << "" << "";
        QTest::newRow("amd 8 GB + vulkan") << "ram=16,cores=8,avx2=1,gpu=amd,vram=8,backends=vulkan" << "large-v3-turbo" << true << "" << "";
        QTest::newRow("amd + hip") << "ram=16,cores=8,avx2=1,gpu=amd,vram=16,backends=hip" << "large-v3-turbo" << true << "" << "";
        QTest::newRow("nvidia 3 GB") << "ram=16,cores=4,avx2=1,gpu=nvidia,vram=3,backends=cuda" << "medium" << true << "" << "";
        QTest::newRow("nvidia 2 GB") << "ram=8,cores=4,avx2=1,gpu=nvidia,vram=2,backends=cuda" << "medium-q5_0" << true << "" << "";
        // GPU present, backend missing: CPU model now, the package as a hint
        QTest::newRow("rtx2060, no backend") << "ram=32,cores=6,avx2=1,gpu=nvidia,vram=6" << "small-q8_0" << false << "ggml-cuda" << "large-v3-turbo";
        QTest::newRow("amd, no backend") << "ram=16,cores=8,avx2=1,gpu=amd,vram=8,backends=cuda" << "small-q8_0" << false << "ggml-vulkan" << "large-v3-turbo";
        QTest::newRow("small nvidia, no backend") << "ram=8,cores=4,avx2=1,gpu=nvidia,vram=2" << "small-q8_0" << false << "ggml-cuda" << "medium-q5_0";
        // too little video memory: CPU, no hint
        QTest::newRow("amd apu 512 MB") << "ram=16,cores=8,avx2=1,gpu=amd,vram=0.5,backends=vulkan" << "small-q8_0" << false << "" << "";
        // Intel integrated graphics share memory: CPU only, even with Vulkan
        QTest::newRow("intel igpu + vulkan") << "ram=16,cores=4,avx2=1,gpu=intel,backends=vulkan" << "small-q8_0" << false << "" << "";
        // CPU only
        QTest::newRow("laptop 4 cores") << "ram=8,cores=4,avx2=1" << "small-q8_0" << false << "" << "";
        QTest::newRow("6 cores") << "ram=16,cores=6,avx2=1" << "small-q8_0" << false << "" << "";
        QTest::newRow("8 fast cores") << "ram=16,cores=8,avx2=1" << "small-q8_0" << false << "" << "";
        QTest::newRow("8 cores, no avx2") << "ram=16,cores=8,avx2=0" << "base-q8_0" << false << "" << "";
        QTest::newRow("16 cores") << "ram=64,cores=16,avx2=1" << "medium-q8_0" << false << "" << "";
        QTest::newRow("12 cores avx512") << "ram=32,cores=12,avx2=1,avx512=1" << "medium-q8_0" << false << "" << "";
        QTest::newRow("2 cores 4 GB") << "ram=4,cores=2,avx2=1" << "base-q8_0" << false << "" << "";
        QTest::newRow("4 cores 4 GB") << "ram=4,cores=4,avx2=1" << "base-q8_0" << false << "" << "";
        QTest::newRow("old netbook") << "ram=2,cores=2,avx2=0" << "tiny" << false << "" << "";
    }

    void recommendation()
    {
        QFETCH(QString, spec);
        QFETCH(QString, model);
        QFETCH(bool, gpu);
        QFETCH(QString, upgradePackage);
        QFETCH(QString, upgradeModel);
        const Hardware hw = fromSpec(spec);
        const Recommendation r = recommend(hw);
        QCOMPARE(r.model, model);
        QVERIFY(findModel(r.model));
        QCOMPARE(r.gpu, gpu);
        QCOMPARE(r.device, gpu ? hw.gpuName : QStringLiteral("CPU"));
        QVERIFY(!r.reason.isEmpty());
        QCOMPARE(r.upgradePackage, upgradePackage);
        QCOMPARE(r.upgradeModel, upgradeModel);
        QCOMPARE(r.upgradeHint.isEmpty(), upgradePackage.isEmpty());
        if (!upgradePackage.isEmpty())
            QVERIFY(r.upgradeHint.contains(upgradePackage));
        // whatever is recommended keeps up with the speech
        QVERIFY2(estimatedSpeed(r.model, hw) >= 1.0 || r.model == QLatin1String("tiny"), qPrintable(r.model));
        QVERIFY(!tooHeavy(r.model, hw));
    }

    void thisMachine()
    {
        // RTX 2060 without ggml-cuda: CPU now, hint names the package and the speed-up
        const Recommendation before = recommend(rtx2060({}));
        QCOMPARE(before.device, QStringLiteral("CPU"));
        QVERIFY(before.upgradeHint.contains(QStringLiteral("ggml-cuda")));
        QVERIFY(before.upgradeHint.contains(QStringLiteral("NVIDIA")));
        QVERIFY(before.upgradeSpeedup >= 3);
        QVERIFY(tooHeavy(QStringLiteral("large-v3-turbo"), rtx2060({})));
        QVERIFY(estimatedSpeed(QStringLiteral("large-v3-turbo"), rtx2060({})) < 1.0);

        // ... and with it
        const Hardware cuda = rtx2060({QStringLiteral("cuda")});
        const Recommendation after = recommend(cuda);
        QCOMPARE(after.model, QStringLiteral("large-v3-turbo"));
        QCOMPARE(after.device, QStringLiteral("NVIDIA GeForce RTX 2060"));
        QVERIFY(after.upgradeHint.isEmpty());
        QVERIFY(!tooHeavy(QStringLiteral("large-v3-turbo"), cuda));
        QVERIFY(tooHeavy(QStringLiteral("large-v3"), cuda));
        QVERIFY(estimatedSpeed(QStringLiteral("large-v3-turbo"), cuda) > 5);
        QVERIFY(runSummary(QStringLiteral("large-v3-turbo"), cuda).startsWith(QStringLiteral("large-v3-turbo · GPU · ")));
        QVERIFY(runSummary(QStringLiteral("large-v3-turbo"), rtx2060({})).startsWith(QStringLiteral("large-v3-turbo · CPU · ")));
    }

    void heavy()
    {
        const Hardware laptop = fromSpec(QStringLiteral("ram=8,cores=4,avx2=1"));
        QVERIFY(tooHeavy(QStringLiteral("large-v3-turbo"), laptop));
        QVERIFY(tooHeavy(QStringLiteral("medium"), laptop));
        QVERIFY(!tooHeavy(QStringLiteral("tiny"), laptop));
        QVERIFY(!tooHeavy(QStringLiteral("unknown-model"), laptop));
        // quantised or not, the same family weighs the same
        QCOMPARE(familyRank(QStringLiteral("small-q5_1")), familyRank(QStringLiteral("small")));
        QCOMPARE(family(QStringLiteral("large-v3-turbo-q8_0")), QStringLiteral("large-v3-turbo"));
        QVERIFY(familyRank(QStringLiteral("large-v3")) > familyRank(QStringLiteral("large-v3-turbo")));
    }

    void speeds()
    {
        const Hardware six = fromSpec(QStringLiteral("ram=16,cores=6,avx2=1"));
        const Hardware twelve = fromSpec(QStringLiteral("ram=16,cores=12,avx2=1"));
        const Hardware old = fromSpec(QStringLiteral("ram=16,cores=6,avx2=0"));
        for (const Model &m : catalogue()) {
            QVERIFY2(estimatedSpeed(m.name, six, false) > 0, qPrintable(m.name));
            QVERIFY2(estimatedSpeed(m.name, six, true) > 0, qPrintable(m.name));
            QVERIFY(estimatedSpeed(m.name, twelve, false) > estimatedSpeed(m.name, six, false));
            QVERIFY(estimatedSpeed(m.name, old, false) < estimatedSpeed(m.name, six, false));
        }
        // bigger families are slower
        QVERIFY(estimatedSpeed(QStringLiteral("tiny"), six, false) > estimatedSpeed(QStringLiteral("base"), six, false));
        QVERIFY(estimatedSpeed(QStringLiteral("base"), six, false) > estimatedSpeed(QStringLiteral("small"), six, false));
        QVERIFY(estimatedSpeed(QStringLiteral("small"), six, false) > estimatedSpeed(QStringLiteral("medium"), six, false));
        QCOMPARE(estimatedSpeed(QStringLiteral("nope"), six, false), 0.0);
        QVERIFY(speedText(4).contains(QStringLiteral("4")));
        QVERIFY(speedText(0.5).contains(QStringLiteral("2")));
        QVERIFY(speedText(0).isEmpty());
    }

    void runOptions()
    {
        // GPU: flash attention, greedy, 4 threads
        const Hardware cuda = rtx2060({QStringLiteral("cuda")});
        QCOMPARE(runArguments(cuda, true), (QStringList{QStringLiteral("-t"), QStringLiteral("4"), QStringLiteral("-fa"),
                                                        QStringLiteral("-bs"), QStringLiteral("1"), QStringLiteral("-bo"), QStringLiteral("1"),
                                                        QStringLiteral("-mc"), QStringLiteral("0"), QStringLiteral("-sns")}));
        // a known model: word times by DTW, which needs flash attention off
        const QStringList dtw = runArguments(cuda, true, QStringLiteral("large-v3-turbo"));
        QCOMPARE(dtw.at(dtw.indexOf(QStringLiteral("-dtw")) + 1), QStringLiteral("large.v3.turbo"));
        QVERIFY(dtw.contains(QStringLiteral("-nfa")));
        QVERIFY(!dtw.contains(QStringLiteral("-fa")));
        QCOMPARE(dtwPreset(QStringLiteral("small-q8_0")), QStringLiteral("small"));
        QCOMPARE(dtwPreset(QStringLiteral("large-v3-turbo-q5_0")), QStringLiteral("large.v3.turbo"));
        QCOMPARE(dtwPreset(QStringLiteral("large-v3")), QStringLiteral("large.v3"));
        QCOMPARE(dtwPreset(QStringLiteral("medium-q5_0")), QStringLiteral("medium"));
        QVERIFY(dtwPreset(QStringLiteral("something")).isEmpty());
        // CPU: every logical processor, no GPU, no VAD (word timestamps)
        const QStringList cpu = runArguments(rtx2060({}), false);
        QCOMPARE(cpu.at(cpu.indexOf(QStringLiteral("-t")) + 1), QStringLiteral("12"));
        QVERIFY(cpu.contains(QStringLiteral("-ng")));
        QVERIFY(cpu.contains(QStringLiteral("-fa")));
        QVERIFY(!cpu.contains(QStringLiteral("--vad")));
        QCOMPARE(threadCount(fromSpec(QStringLiteral("cores=64,threads=128")), false), 16);
        QCOMPARE(threadCount(fromSpec(QStringLiteral("cores=1")), false), 1);
    }

    void install()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        Command c = installCommand(QStringLiteral("ggml-cuda"), root.path());
        QCOMPARE(c.program, QStringLiteral("pkexec"));
        QCOMPARE(c.arguments, (QStringList{QStringLiteral("pacman"), QStringLiteral("-S"), QStringLiteral("--needed"),
                                           QStringLiteral("--noconfirm"), QStringLiteral("ggml-cuda")}));
        writeFile(root.path() + QStringLiteral("/usr/lib/lingmo-store/lingmo-store-helper"), "#!/bin/sh\n", true);
        c = installCommand(QStringLiteral("ggml-cuda"), root.path());
        QCOMPARE(c.arguments, (QStringList{QStringLiteral("/usr/lib/lingmo-store/lingmo-store-helper"), QStringLiteral("install"),
                                           QStringLiteral("ggml-cuda")}));
    }

    void catalogueEntries()
    {
        QSet<QString> names;
        for (const Model &model : catalogue()) {
            QVERIFY(!names.contains(model.name));
            names.insert(model.name);
            QVERIFY(model.bytes > 30'000'000);
            QCOMPARE(model.sha256.size(), 64);
            QVERIFY(model.speed >= 1 && model.speed <= 5);
            QVERIFY(model.quality >= 1 && model.quality <= 4);
            QCOMPARE(model.quantized, model.name.contains(QLatin1Char('_')));
            QVERIFY(familyRank(model.name) >= 0);
        }
        // Published sizes: tiny 75 MiB, base 142 MiB, small 466 MiB, large-v3 2.9 GiB
        QCOMPARE(findModel(QStringLiteral("tiny"))->bytes / (1024 * 1024), 74);
        QCOMPARE(findModel(QStringLiteral("base"))->bytes / (1024 * 1024), 141);
        QCOMPARE(findModel(QStringLiteral("small"))->bytes / (1024 * 1024), 465);
        QCOMPARE(findModel(QStringLiteral("large-v3"))->bytes / (1024 * 1024), 2951);
        QVERIFY(findModel(QStringLiteral("large-v3-turbo-q8_0")));
        QVERIFY(!findModel(QStringLiteral("base.en")));
    }
};

QTEST_GUILESS_MAIN(TestWhisperHw)
#include "tst_whisperhw.moc"
