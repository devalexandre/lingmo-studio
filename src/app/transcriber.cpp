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
#include <QThread>

using namespace Studio;

static QString tr(const char *s)
{
    return QCoreApplication::translate("Transcriber", s);
}

Transcriber::Transcriber(QObject *parent)
    : QObject(parent)
{
    refresh();
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

Transcriber::Engine Transcriber::detect(QString *problem, bool *programFound)
{
    // Lingmo Settings › AI writes these (older builds used an [ai] group)
    QSettings ai(QStringLiteral("lingmoos"), QStringLiteral("ai"));
    auto value = [&ai](const char *key) {
        const QString plain = ai.value(QLatin1String(key)).toString();
        return plain.isEmpty() ? ai.value(QStringLiteral("ai/") + QLatin1String(key)).toString() : plain;
    };
    const QString backend = value("subtitle_backend");
    const QString model = value("subtitle_model");
    const QString lang = value("subtitle_language");

    Engine e;
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
    const Engine e = detect(&problem, &found);
    m_engine = e;
    m_problem = e.available() ? QString() : problem;
    m_programFound = found;
    Q_EMIT engineChanged();
}

QStringList Transcriber::arguments(const Engine &e, const QString &wav, const QString &outBase, int threads)
{
    if (e.kind == QLatin1String("whisper.cpp"))
        return {QStringLiteral("-m"), e.modelFile, QStringLiteral("-f"), wav, QStringLiteral("-l"), e.language.isEmpty() ? QStringLiteral("auto") : e.language,
                QStringLiteral("-t"), QString::number(threads), QStringLiteral("-ojf"), QStringLiteral("-of"), outBase, QStringLiteral("-pp"), QStringLiteral("-np")};
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

void Transcriber::setProgress(double p, const QString &stage)
{
    m_progress = p;
    m_stage = stage;
    Q_EMIT progressChanged();
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
                            .arg(m_engine.kind, m_engine.model, m_engine.language);
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
    Q_EMIT runningChanged();
    if (!m_engine.available()) {
        finish(m_problem.isEmpty() ? tr("No transcription engine is installed.") : m_problem);
        return;
    }
    QFile cached(cacheFile());
    if (cached.open(QIODevice::ReadOnly)) {
        m_result = Captions::transcriptFromJson(cached.readAll());
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
    const int threads = std::clamp(QThread::idealThreadCount() - 1, 1, 8);
    setProgress(0, tr("Transcribing on this computer (%1, %2)…").arg(m_engine.kind, m_engine.model));
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
    m_process->start(m_engine.program, arguments(m_engine, wav, base, threads));
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
    setProgress(-1, QString());
    Q_EMIT runningChanged();
    Q_EMIT failed(task, tr("Cancelled"));
}

void Transcriber::finish(const QString &error)
{
    const QString task = m_task;
    m_task.clear();
    m_dir.reset();
    Q_EMIT runningChanged();
    if (error.isEmpty())
        Q_EMIT finished(task);
    else
        Q_EMIT failed(task, error);
}

void Transcriber::openSettings() const
{
    QProcess::startDetached(QStringLiteral("lingmo-settings"), {QStringLiteral("-m"), QStringLiteral("ai/transcription")});
}
