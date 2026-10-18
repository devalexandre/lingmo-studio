/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "transcriber.h"
#include "aitools.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStorageInfo>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>

#include <algorithm>

using namespace Studio;

static QString tr(const char *s)
{
    return QCoreApplication::translate("Transcriber", s);
}

static const QString WhisperDownloadUrl = QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-%1.bin");

Transcriber::Transcriber(QObject *parent)
    : QObject(parent)
    , m_tick(new QTimer(this))
{
    m_tick->setInterval(1000);
    connect(m_tick, &QTimer::timeout, this, &Transcriber::timingChanged);
    refresh(); // the hardware probe runs in the background

}

QString Transcriber::modelsDir()
{
    return QDir::home().filePath(QStringLiteral(".local/share/lingmo/whisper-models"));
}

static QString findProgram(const QString &name)
{
    QString p = QStandardPaths::findExecutable(name);
    if (p.isEmpty())
        p = QStandardPaths::findExecutable(name, {QDir::home().filePath(QStringLiteral(".local/bin"))});
    return p;
}

Transcriber::Engine Transcriber::detect(QString *problem, bool *programFound, const QString &modelOverride, const WhisperHw::Hardware *hw)
{
    // Lingmo Settings › AI writes these (older builds used an [ai] group)
    QSettings ai(QStringLiteral("lingmoos"), QStringLiteral("ai"));
    auto value = [&ai](const char *key) {
        const QString plain = ai.value(QLatin1String(key)).toString();
        return plain.isEmpty() ? ai.value(QStringLiteral("ai/") + QLatin1String(key)).toString() : plain;
    };
    const QString backend = value("subtitle_backend");
    const QString configured = value("subtitle_model");
    const QString model = modelOverride.isEmpty() ? configured : modelOverride;
    const QString lang = value("subtitle_language");

    Engine e;
    e.configuredModel = configured;
    e.language = !lang.isEmpty() && lang != QLatin1String("auto") ? lang
        : QLocale().name().startsWith(QLatin1String("pt")) ? QStringLiteral("pt")
                                                          : QStringLiteral("auto");
    const QString cli = [] {
        for (const char *n : {"whisper-cli", "whisper-cpp"}) {
            const QString p = findProgram(QString::fromLatin1(n));
            if (!p.isEmpty())
                return p;
        }
        return QString();
    }();
    const QString ct2 = findProgram(QStringLiteral("whisper-ctranslate2"));
    if (programFound)
        *programFound = !cli.isEmpty() || !ct2.isEmpty();

    auto useFaster = [&] {
        e.kind = QStringLiteral("faster-whisper");
        e.program = ct2;
        QString m = model;
        m.remove(QRegularExpression(QStringLiteral("-q\\d_\\d$"))); // ggml quantisation suffix
        e.model = m.isEmpty() ? QStringLiteral("small") : m;
    };
    if (backend == QLatin1String("faster-whisper") && !ct2.isEmpty()) {
        useFaster();
        return e;
    }
    if (!cli.isEmpty() && backend != QLatin1String("faster-whisper")) {
        e.kind = QStringLiteral("whisper.cpp");
        e.program = cli;
        const QDir dir(modelsDir());
        if (!model.isEmpty() && dir.exists(QStringLiteral("ggml-%1.bin").arg(model))) {
            e.model = model;
            e.modelFile = dir.filePath(QStringLiteral("ggml-%1.bin").arg(model));
            return e;
        }
        // A downloaded model: the best one that isn't too heavy for this
        // computer, else the lightest one
        if (hw) {
            const QStringList files = dir.entryList({QStringLiteral("ggml-*.bin")}, QDir::Files);
            QString best, lightest;
            for (const WhisperHw::Model &m : WhisperHw::catalogue()) {
                if (!files.contains(QStringLiteral("ggml-%1.bin").arg(m.name)))
                    continue;
                if (lightest.isEmpty())
                    lightest = m.name;
                if (!WhisperHw::tooHeavy(m.name, *hw))
                    best = m.name;
            }
            const QString pick = best.isEmpty() ? lightest : best;
            if (!pick.isEmpty()) {
                e.model = pick;
                e.modelFile = dir.filePath(QStringLiteral("ggml-%1.bin").arg(pick));
                return e;
            }
        }
        // any downloaded model, then the places older setups used
        QStringList dirs = {modelsDir()};
        const AiTools::WhisperEngine legacy = AiTools::findWhisper();
        if (!legacy.model.isEmpty())
            dirs << QFileInfo(legacy.model).absolutePath();
        for (const QString &d : std::as_const(dirs)) {
            const QStringList files = QDir(d).entryList({QStringLiteral("ggml-*.bin")}, QDir::Files, QDir::Size | QDir::Reversed);
            if (!files.isEmpty()) {
                e.modelFile = QDir(d).filePath(files.last()); // the biggest one: best quality installed
                e.model = files.last().mid(5).chopped(4);
                return e;
            }
        }
        if (!ct2.isEmpty()) {
            useFaster();
            return e;
        }
        if (problem)
            *problem = tr("whisper.cpp is installed, but no speech model was downloaded yet.");
        return e; // kind set, no model: not available
    }
    if (!ct2.isEmpty()) {
        useFaster();
        return e;
    }
    if (problem)
        *problem = backend == QLatin1String("faster-whisper") ? tr("faster-whisper (whisper-ctranslate2) is not installed.")
                                                              : tr("No transcription engine is installed.");
    return {};
}

void Transcriber::refresh()
{
    QString problem;
    bool found = false;
    const Engine e = detect(&problem, &found, m_override, m_recommendation.model.isEmpty() ? nullptr : &m_hw);
    m_engine = e;
    m_problem = e.available() ? QString() : problem;
    m_programFound = found;
    Q_EMIT engineChanged();
    Q_EMIT hardwareChanged();
    probeAsync();
}

// The GPU, its backend and the processor, again each time: ggml-cuda may have
// been installed since
void Transcriber::probeAsync()
{
    if (m_fixedHardware || m_probe)
        return;
    auto *watcher = new QFutureWatcher<WhisperHw::Hardware>(this);
    m_probe = watcher;
    connect(watcher, &QFutureWatcher<WhisperHw::Hardware>::finished, this, [this, watcher] {
        watcher->deleteLater();
        const WhisperHw::Hardware hw = watcher->result();
        m_probe = nullptr;
        if (!m_fixedHardware)
            applyHardware(hw);
        else
            Q_EMIT hardwareChanged();
    });
    watcher->setFuture(QtConcurrent::run([] { return WhisperHw::probe(); }));
    Q_EMIT hardwareChanged();
}

void Transcriber::applyHardware(const WhisperHw::Hardware &hw)
{
    m_hw = hw;
    m_recommendation = WhisperHw::recommend(hw);
    QString problem;
    bool found = false;
    m_engine = detect(&problem, &found, m_override, &m_hw);
    m_problem = m_engine.available() ? QString() : problem;
    m_programFound = found;
    Q_EMIT engineChanged();
    Q_EMIT hardwareChanged();
}

void Transcriber::setHardware(const WhisperHw::Hardware &hw)
{
    m_fixedHardware = true;
    applyHardware(hw);
}

QString Transcriber::device() const
{
    if (!m_hw.gpuUsable())
        return QStringLiteral("CPU");
    return m_hw.gpuName.isEmpty() ? WhisperHw::vendorDisplayName(m_hw.gpuVendor) : m_hw.gpuName;
}

QString Transcriber::summary() const
{
    if (m_engine.model.isEmpty())
        return {};
    if (m_engine.kind != QLatin1String("whisper.cpp"))
        return m_engine.model; // faster-whisper picks its own device
    QStringList parts = {m_engine.model, device()};
    const QString speed = speedText();
    if (!speed.isEmpty())
        parts << speed;
    return parts.join(QStringLiteral(" · "));
}

QString Transcriber::speedText() const
{
    if (m_engine.kind != QLatin1String("whisper.cpp"))
        return {};
    return WhisperHw::speedText(WhisperHw::estimatedSpeed(m_engine.model, m_hw));
}

bool Transcriber::unsuitable() const
{
    return m_engine.kind == QLatin1String("whisper.cpp") && !m_engine.model.isEmpty() && !m_recommendation.model.isEmpty()
        && WhisperHw::tooHeavy(m_engine.model, m_hw);
}

QString Transcriber::recommendedSummary() const
{
    const WhisperHw::Recommendation &r = m_recommendation;
    if (r.model.isEmpty())
        return {};
    QStringList parts = {r.model, r.device};
    const QString speed = WhisperHw::speedText(WhisperHw::estimatedSpeed(r.model, m_hw, r.gpu));
    if (!speed.isEmpty())
        parts << speed;
    return parts.join(QStringLiteral(" · "));
}

bool Transcriber::recommendedInstalled() const
{
    return !m_recommendation.model.isEmpty() && QDir(modelsDir()).exists(QStringLiteral("ggml-%1.bin").arg(m_recommendation.model));
}

void Transcriber::setModelOverride(const QString &name)
{
    if (name == m_override || (!name.isEmpty() && !WhisperHw::findModel(name)))
        return;
    m_override = name;
    refresh();
}

QVariantList Transcriber::modelChoices() const
{
    const QDir dir(modelsDir());
    QVariantList list;
    for (const WhisperHw::Model &m : WhisperHw::catalogue()) {
        const bool installed = dir.exists(QStringLiteral("ggml-%1.bin").arg(m.name));
        const bool recommended = m.name == m_recommendation.model;
        if (!installed && !recommended)
            continue;
        const bool heavy = WhisperHw::tooHeavy(m.name, m_hw);
        QStringList notes;
        if (recommended)
            notes << tr("recommended");
        if (!installed)
            notes << tr("download %1").arg(QLocale().formattedDataSize(m.bytes, 1, QLocale::DataSizeSIFormat));
        if (heavy)
            notes << tr("slow here");
        const QString speed = WhisperHw::speedText(WhisperHw::estimatedSpeed(m.name, m_hw));
        if (!speed.isEmpty())
            notes << speed;
        list.append(QVariantMap{
            {QStringLiteral("name"), m.name},
            {QStringLiteral("installed"), installed},
            {QStringLiteral("recommended"), recommended},
            {QStringLiteral("tooHeavy"), heavy},
            {QStringLiteral("label"), notes.isEmpty() ? m.name : QStringLiteral("%1 (%2)").arg(m.name, notes.join(QStringLiteral(", ")))},
        });
    }
    return list;
}

QStringList Transcriber::arguments(const Engine &e, const QString &wav, const QString &outBase, const WhisperHw::Hardware &hw)
{
    if (e.kind == QLatin1String("whisper.cpp"))
        return QStringList{QStringLiteral("-m"), e.modelFile, QStringLiteral("-f"), wav, QStringLiteral("-l"), e.language.isEmpty() ? QStringLiteral("auto") : e.language,
                           QStringLiteral("-ojf"), QStringLiteral("-of"), outBase, QStringLiteral("-pp"), QStringLiteral("-np")}
            + WhisperHw::runArguments(hw, hw.gpuUsable(), e.model.isEmpty() ? QFileInfo(e.modelFile).completeBaseName().mid(5) : e.model);
    const int threads = WhisperHw::threadCount(hw, false);
    if (e.kind == QLatin1String("faster-whisper")) {
        QStringList a = {wav, QStringLiteral("--model"), e.model, QStringLiteral("--output_format"), QStringLiteral("json"),
                         QStringLiteral("--output_dir"), QFileInfo(outBase).absolutePath(), QStringLiteral("--word_timestamps"), QStringLiteral("True"),
                         QStringLiteral("--threads"), QString::number(threads)};
        if (!e.language.isEmpty() && e.language != QLatin1String("auto"))
            a << QStringLiteral("--language") << e.language;
        return a;
    }
    return {};
}

double Transcriber::parseProgress(const QString &line, double durationSec)
{
    static const QRegularExpression pct(QStringLiteral("progress\\s*=\\s*(\\d+)%"));
    auto m = pct.match(line);
    if (m.hasMatch())
        return std::clamp(m.captured(1).toDouble() / 100.0, 0.0, 1.0);
    // faster-whisper prints "[00:01.000 --> 00:04.500] text" (or with hours)
    static const QRegularExpression seg(QStringLiteral("-->\\s*(?:(\\d+):)?(\\d+):(\\d+(?:\\.\\d+)?)\\]"));
    m = seg.match(line);
    if (m.hasMatch() && durationSec > 0) {
        const double t = m.captured(1).toDouble() * 3600 + m.captured(2).toDouble() * 60 + m.captured(3).toDouble();
        return std::clamp(t / durationSec, 0.0, 1.0);
    }
    return -1;
}

double Transcriber::remaining(double elapsedSec, double progress, double audioSec, double estimatedSpeed)
{
    if (progress >= 1)
        return 0;
    // From the pace so far once whisper has done a little
    if (progress >= 0.05 && elapsedSec > 0)
        return elapsedSec * (1 - progress) / progress;
    if (audioSec > 0 && estimatedSpeed > 0) {
        const double total = audioSec / estimatedSpeed;
        return std::max(total * (1 - std::max(0.0, progress)), total - elapsedSec);
    }
    return -1;
}

QString Transcriber::clockText(double seconds)
{
    const qint64 s = std::max<qint64>(0, qint64(std::llround(seconds)));
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

double Transcriber::elapsedSeconds() const
{
    return running() && m_clock.isValid() ? m_clock.elapsed() / 1000.0 : 0;
}

double Transcriber::remainingSeconds() const
{
    if (!running() || m_firstProgressAt < 0)
        return -1; // still extracting the audio
    return remaining(elapsedSeconds() - m_firstProgressAt, m_progress, m_mediaSeconds,
                     m_engine.kind == QLatin1String("whisper.cpp") ? WhisperHw::estimatedSpeed(m_engine.model, m_hw) : 0);
}

QString Transcriber::timing() const
{
    if (!running())
        return {};
    QStringList parts;
    if (m_progress >= 0)
        parts << tr("%1%").arg(int(m_progress * 100));
    parts << tr("%1 elapsed").arg(clockText(elapsedSeconds()));
    const double left = remainingSeconds();
    if (left >= 0)
        parts << tr("~%1 left").arg(clockText(left));
    if (m_mediaSeconds > 0)
        parts << tr("%1 of audio").arg(clockText(m_mediaSeconds));
    return parts.join(QStringLiteral(" · "));
}

void Transcriber::setProgress(double p, const QString &stage)
{
    m_progress = p;
    m_stage = stage;
    Q_EMIT progressChanged();
    Q_EMIT timingChanged();
}

QString Transcriber::cacheFile() const
{
    const QFileInfo fi(m_path);
    const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
                            .arg(fi.absoluteFilePath())
                            .arg(fi.size())
                            .arg(fi.lastModified().toMSecsSinceEpoch())
                            .arg(m_start)
                            .arg(m_duration)
                            .arg(m_engine.kind, m_engine.model, m_engine.language)
                        + QStringLiteral("|v3"); // v3: no text context, loops collapsed, DTW word times
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/transcripts");
    QDir().mkpath(dir);
    return QDir(dir).filePath(QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex()) + QStringLiteral(".json"));
}

void Transcriber::transcribe(const QString &path, qint64 startMs, qint64 durationMs, const QString &task)
{
    if (running())
        cancel();
    refresh();
    m_task = task.isEmpty() ? QStringLiteral("transcribe") : task;
    m_path = path;
    m_start = std::max<qint64>(0, startMs);
    m_duration = durationMs;
    m_result = {};
    m_log.clear();
    m_mediaSeconds = 0;
    m_firstProgressAt = -2; // extracting the audio
    m_clock.start();
    m_tick->start();
    // The hardware as it is now (a GPU backend may have been installed)
    if (!m_fixedHardware)
        applyHardware(WhisperHw::probe());
    Q_EMIT runningChanged();
    if (!m_engine.available()) {
        finish(m_problem.isEmpty() ? tr("No transcription engine is installed.") : m_problem);
        return;
    }
    QFile cached(cacheFile());
    if (cached.open(QIODevice::ReadOnly)) {
        m_result = Captions::transcriptFromJson(cached.readAll());
        m_result.words = Captions::collapseRepeats(m_result.words); // caches from before the loop filter
        if (!m_result.isEmpty()) {
            setProgress(1, tr("Transcript ready"));
            finish({});
            return;
        }
    }
    m_dir = std::make_unique<QTemporaryDir>();
    const QString wav = m_dir->filePath(QStringLiteral("speech.wav"));
    QStringList args = {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y")};
    if (m_start > 0)
        args << QStringLiteral("-ss") << QString::number(m_start / 1000.0, 'f', 3);
    args << QStringLiteral("-i") << path;
    if (m_duration > 0)
        args << QStringLiteral("-t") << QString::number(m_duration / 1000.0, 'f', 3);
    args << QStringLiteral("-vn") << QStringLiteral("-ac") << QStringLiteral("1") << QStringLiteral("-ar") << QStringLiteral("16000")
         << QStringLiteral("-c:a") << QStringLiteral("pcm_s16le") << wav;
    setProgress(-1, tr("Extracting the speech…"));
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::finished, this, [this, wav](int code, QProcess::ExitStatus st) {
        QProcess *p = m_process;
        m_process = nullptr;
        if (p)
            p->deleteLater();
        if (!running())
            return; // cancelled
        if (st != QProcess::NormalExit || code != 0 || !QFileInfo::exists(wav)) {
            finish(tr("Could not read the audio"));
            return;
        }
        m_mediaSeconds = std::max<qint64>(0, QFileInfo(wav).size() - 44) / 32000.0; // 16 kHz mono s16
        runWhisper();
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            finish(tr("ffmpeg is not installed"));
    });
    m_process->start(QStringLiteral("ffmpeg"), args);
}

void Transcriber::runWhisper()
{
    const QString wav = m_dir->filePath(QStringLiteral("speech.wav"));
    const QString base = m_dir->filePath(QStringLiteral("speech"));
    m_firstProgressAt = elapsedSeconds(); // whisper starts now: its pace gives the time left
    setProgress(0, m_engine.kind == QLatin1String("whisper.cpp")
                       ? tr("Transcribing with %1 on %2…").arg(m_engine.model, device())
                       : tr("Transcribing on this computer (%1, %2)…").arg(m_engine.kind, m_engine.model));
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    m_process->setProcessEnvironment(env);
    connect(m_process, &QProcess::readyRead, this, [this] {
        if (!m_process)
            return;
        const QString chunk = QString::fromUtf8(m_process->readAll());
        m_log += chunk;
        if (m_log.size() > 200000)
            m_log = m_log.right(100000);
        for (const QString &line : chunk.split(QLatin1Char('\n'))) {
            const double p = parseProgress(line, m_mediaSeconds);
            if (p >= 0)
                setProgress(p, m_stage);
        }
    });
    connect(m_process, &QProcess::finished, this, [this, base](int code, QProcess::ExitStatus st) {
        QProcess *p = m_process;
        m_process = nullptr;
        if (p) {
            m_log += QString::fromUtf8(p->readAll());
            p->deleteLater();
        }
        if (!running())
            return;
        QString json = base + QStringLiteral(".json");
        if (!QFileInfo::exists(json))
            json = QDir(QFileInfo(base).absolutePath()).filePath(QStringLiteral("speech.json"));
        QFile f(json);
        if (st != QProcess::NormalExit || code != 0 || !f.open(QIODevice::ReadOnly)) {
            const QString tail = m_log.trimmed().right(300);
            finish(tail.isEmpty() ? tr("Transcription failed") : tr("Transcription failed: %1").arg(tail.section(QLatin1Char('\n'), -1)));
            return;
        }
        m_result = Captions::parseTranscript(f.readAll());
        // back to the file's time
        for (Captions::Word &w : m_result.words) {
            w.startMs += m_start;
            w.endMs += m_start;
        }
        QSaveFile cache(cacheFile());
        if (cache.open(QIODevice::WriteOnly)) {
            cache.write(Captions::transcriptToJson(m_result));
            cache.commit();
        }
        setProgress(1, tr("Transcript ready"));
        finish(m_result.isEmpty() ? tr("No speech was found") : QString());
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            finish(tr("%1 could not be started").arg(m_engine.program));
    });
    m_process->start(m_engine.program, arguments(m_engine, wav, base, m_hw));
}

void Transcriber::cancel()
{
    if (!running())
        return;
    if (m_process) {
        QProcess *p = m_process;
        m_process = nullptr;
        p->disconnect(this);
        p->kill();
        p->waitForFinished(2000);
        p->deleteLater();
    }
    const QString task = m_task;
    m_task.clear();
    m_dir.reset();
    m_tick->stop();
    setProgress(-1, QString());
    Q_EMIT runningChanged();
    Q_EMIT failed(task, tr("Cancelled"));
}

void Transcriber::finish(const QString &error)
{
    const QString task = m_task;
    m_task.clear();
    m_dir.reset();
    m_tick->stop();
    Q_EMIT runningChanged();
    Q_EMIT timingChanged();
    if (error.isEmpty())
        Q_EMIT finished(task);
    else
        Q_EMIT failed(task, error);
}

void Transcriber::openSettings() const
{
    QProcess::startDetached(QStringLiteral("lingmo-settings"), {QStringLiteral("-m"), QStringLiteral("ai/transcription")});
}

// ------------------------------------------------------------------ models and GPU support

void Transcriber::writeModelSetting(const QString &name)
{
    // The same key Lingmo Settings writes (its AI page reads it again when opened)
    QSettings ai(QStringLiteral("lingmoos"), QStringLiteral("ai"));
    ai.setValue(QStringLiteral("subtitle_model"), name);
    ai.sync();
}

void Transcriber::useModel(const QString &name)
{
    if (!WhisperHw::findModel(name))
        return;
    if (!QDir(modelsDir()).exists(QStringLiteral("ggml-%1.bin").arg(name))) {
        downloadModel(name);
        m_useAfterDownload = downloading() && m_downloadName == name;
        return;
    }
    writeModelSetting(name);
    m_override.clear();
    refresh();
}

void Transcriber::downloadModel(const QString &name)
{
    const WhisperHw::Model *model = WhisperHw::findModel(name);
    if (!model || downloading())
        return;
    m_downloadError.clear();
    m_useAfterDownload = false;
    const QDir dir(modelsDir());
    if (!dir.mkpath(QStringLiteral("."))) {
        m_downloadError = tr("Could not create the folder %1.").arg(dir.absolutePath());
        Q_EMIT downloadChanged();
        return;
    }
    const QStorageInfo storage(dir.absolutePath());
    if (storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < model->bytes + 50 * 1000 * 1000) {
        m_downloadError = tr("There is not enough free disk space for %1.").arg(name);
        Q_EMIT downloadChanged();
        return;
    }
    auto *file = new QFile(dir.filePath(QStringLiteral("ggml-%1.bin.part").arg(name)), this);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_downloadError = tr("Could not write to %1.").arg(file->fileName());
        delete file;
        Q_EMIT downloadChanged();
        return;
    }
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    m_downloadFile = file;
    m_downloadHash = new QCryptographicHash(QCryptographicHash::Sha256);
    m_downloadName = name;
    m_downloadProgress = -1;
    m_downloadCancelled = false;
    m_downloadDetail = tr("Contacting Hugging Face…");

    QNetworkRequest request(QUrl(WhisperDownloadUrl.arg(name)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(10);
    request.setTransferTimeout(60000);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (!m_reply || !m_downloadFile)
            return;
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = m_reply->readAll();
        if (status != 200 || data.isEmpty())
            return; // redirect bodies
        if (m_downloadFile->write(data) != data.size()) {
            m_downloadError = m_downloadFile->errorString();
            m_reply->abort();
            return;
        }
        m_downloadHash->addData(data);
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        const WhisperHw::Model *m = WhisperHw::findModel(m_downloadName);
        if (total <= 0 && m)
            total = m->bytes;
        if (received <= 0 || total <= 0)
            return;
        const QLocale locale;
        m_downloadProgress = std::clamp(double(received) / double(total), 0.0, 1.0);
        m_downloadDetail = tr("%1% · %2 of %3").arg(int(m_downloadProgress * 100))
                               .arg(locale.formattedDataSize(received, 1, QLocale::DataSizeSIFormat),
                                    locale.formattedDataSize(total, 1, QLocale::DataSizeSIFormat));
        Q_EMIT downloadChanged();
    });
    connect(m_reply, &QNetworkReply::finished, this, &Transcriber::finishDownload);
    Q_EMIT downloadChanged();
}

void Transcriber::cancelDownload()
{
    if (!m_reply)
        return;
    m_downloadCancelled = true;
    m_reply->abort(); // finished() follows
}

void Transcriber::finishDownload()
{
    QNetworkReply *reply = m_reply;
    if (!reply)
        return;
    if (m_downloadFile && reply->error() == QNetworkReply::NoError) {
        const QByteArray rest = reply->readAll();
        if (!rest.isEmpty() && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
            m_downloadFile->write(rest);
            m_downloadHash->addData(rest);
        }
    }
    reply->deleteLater();
    m_reply = nullptr;

    const QString name = m_downloadName;
    const WhisperHw::Model *model = WhisperHw::findModel(name);
    const QString partPath = m_downloadFile->fileName();
    m_downloadFile->close();
    const qint64 written = m_downloadFile->size();
    delete m_downloadFile;
    m_downloadFile = nullptr;
    const QString sha = QString::fromLatin1(m_downloadHash->result().toHex());
    delete m_downloadHash;
    m_downloadHash = nullptr;

    bool ok = false;
    if (m_downloadCancelled) {
        m_downloadError.clear();
    } else if (!m_downloadError.isEmpty()) {
        m_downloadError = tr("Could not save %1: %2").arg(name, m_downloadError);
    } else if (reply->error() != QNetworkReply::NoError) {
        m_downloadError = tr("The download of %1 failed: %2").arg(name, reply->errorString());
    } else if (!model || written != model->bytes) {
        m_downloadError = tr("The download of %1 is incomplete. Try again.").arg(name);
    } else if (sha != model->sha256) {
        m_downloadError = tr("The downloaded %1 is damaged (its checksum doesn't match). Try again.").arg(name);
    } else {
        const QString finalPath = QDir(modelsDir()).filePath(QStringLiteral("ggml-%1.bin").arg(name));
        QFile::remove(finalPath);
        ok = QFile::rename(partPath, finalPath);
        if (!ok)
            m_downloadError = tr("Could not save %1.").arg(finalPath);
    }
    if (!ok)
        QFile::remove(partPath);
    m_downloadName.clear();
    m_downloadProgress = -1;
    m_downloadDetail.clear();
    const bool use = ok && m_useAfterDownload;
    m_useAfterDownload = false;
    Q_EMIT downloadChanged();
    if (use)
        useModel(name);
    else
        refresh();
}

void Transcriber::installGpuSupport()
{
    const QString package = m_recommendation.upgradePackage;
    if (package.isEmpty() || installing())
        return;
    const WhisperHw::Command command = WhisperHw::installCommand(package);
    m_installFailed = false;
    m_installMessage = tr("Installing %1…").arg(package);
    m_install = new QProcess(this);
    m_install->setProcessChannelMode(QProcess::MergedChannels);
    auto output = std::make_shared<QString>();
    connect(m_install, &QProcess::readyRead, this, [this, output] {
        if (m_install)
            output->append(QString::fromUtf8(m_install->readAll()));
    });
    connect(m_install, &QProcess::finished, this, [this, package, output](int code, QProcess::ExitStatus status) {
        if (m_install)
            output->append(QString::fromUtf8(m_install->readAll()));
        const QString log = *output;
        m_install->deleteLater();
        m_install = nullptr;
        if (status == QProcess::NormalExit && code == 0) {
            m_installMessage = tr("%1 is installed. Transcription now uses the graphics card.").arg(package);
        } else {
            m_installFailed = true;
            if (code == 126 || code == 127) // pkexec: dismissed or not authorised
                m_installMessage = tr("The installation of %1 was cancelled.").arg(package);
            else if (log.contains(QLatin1String("@@ERROR lock")))
                m_installMessage = tr("Another program is installing software. Try again when it finishes.");
            else
                m_installMessage = tr("Could not install %1. Install it from Lingmo Store or with “sudo pacman -S %1”.").arg(package);
        }
        Q_EMIT installChanged();
        // Probe again: the GPU backend should be there now
        if (m_fixedHardware)
            applyHardware(m_hw);
        else
            probeAsync();
    });
    connect(m_install, &QProcess::errorOccurred, this, [this, package](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || !m_install)
            return;
        m_install->deleteLater();
        m_install = nullptr;
        m_installFailed = true;
        m_installMessage = tr("Could not install %1. Install it from Lingmo Store or with “sudo pacman -S %1”.").arg(package);
        Q_EMIT installChanged();
    });
    m_install->start(command.program, command.arguments);
    Q_EMIT installChanged();
}
