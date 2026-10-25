/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "dubber.h"
#include "editor.h"
#include "metadata.h"
#include "transcriber.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace Studio;

namespace {

// Chatterbox with the multilingual V3 model: the PyPI release (0.1.7) only has V2
constexpr const char *ChatterboxGit = "git+https://github.com/resemble-ai/chatterbox.git@5de7a54aa4e5e2baadb0182dde554908b48b85c2";

QUrl ollamaUrl(const QString &path)
{
    QString host = qEnvironmentVariable("OLLAMA_HOST", QStringLiteral("127.0.0.1:11434"));
    if (!host.contains(QLatin1String("://")))
        host.prepend(QLatin1String("http://"));
    QUrl u(host);
    u.setPath(path);
    return u;
}

QString cacheRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/dub");
}

} // namespace

// ------------------------------------------------------------------ the engine's files

QString Dubber::venvDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/lingmo/dub-venv");
}

QString Dubber::python()
{
    const QString env = qEnvironmentVariable("LINGMO_STUDIO_DUB_PYTHON");
    if (!env.isEmpty())
        return env;
    const QString p = venvDir() + QStringLiteral("/bin/python");
    return QFileInfo::exists(p) ? p : QString();
}

QString Dubber::script()
{
    const QString env = qEnvironmentVariable("LINGMO_STUDIO_DUB_SCRIPT");
    if (!env.isEmpty() && QFile::exists(env))
        return env;
    const QString installed = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("lingmo-studio/dub.py"));
    if (!installed.isEmpty())
        return installed;
    const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/../share/lingmo-studio/dub.py");
    if (QFile::exists(beside))
        return beside;
    // the copy built into the app (uninstalled builds): run it from the cache
    QFile builtin(QStringLiteral(":/data/dub.py"));
    if (!builtin.open(QIODevice::ReadOnly))
        return {};
    const QByteArray code = builtin.readAll();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/dub.py");
    QFile out(path);
    if (out.open(QIODevice::ReadOnly) && out.readAll() == code)
        return path;
    out.close();
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    out.write(code);
    return path;
}

QString Dubber::lastLines(const QString &text, int n)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    return lines.mid(std::max<qsizetype>(0, lines.size() - n)).join(QLatin1Char('\n'));
}

// ------------------------------------------------------------------ construction

Dubber::Dubber(Editor *editor)
    : QObject(editor)
    , m_editor(editor)
    , m_network(new QNetworkAccessManager(this))
{
    Transcriber *t = m_editor->transcriber();
    connect(t, &Transcriber::finished, this, [this](const QString &task) {
        if (task != QLatin1String("dub") || !m_running || m_jobIndex >= m_jobs.size())
            return;
        const Captions::Transcript result = m_editor->transcriber()->result();
        m_editor->rememberWords(m_jobs.at(m_jobIndex).mediaId, result.words);
        const QString lang = Dubbing::normalizeCode(result.language);
        if (!lang.isEmpty())
            m_sourceLang = lang;
        ++m_jobIndex;
        transcribeNext();
    });
    connect(t, &Transcriber::failed, this, [this](const QString &task, const QString &message) {
        if (task == QLatin1String("dub") && m_running)
            fail(tr("The speech could not be transcribed: %1").arg(message));
    });
    connect(t, &Transcriber::progressChanged, this, [this] {
        if (m_running && m_editor->transcriber()->running() && m_langTotal == 0) {
            m_stage = m_editor->transcriber()->stage();
            m_progress = -1;
            Q_EMIT stateChanged();
        }
    });
    connect(m_editor, &Editor::documentChanged, this, &Dubber::stateChanged);
    checkEngine();
}

QVariantList Dubber::languages() const
{
    QVariantList out;
    for (const Dubbing::Language &l : Dubbing::languages())
        out.append(QVariantMap{{QStringLiteral("code"), l.code}, {QStringLiteral("name"), l.name}, {QStringLiteral("native"), l.native}});
    return out;
}

void Dubber::setTargets(const QStringList &codes)
{
    QStringList clean;
    for (const QString &c : codes) {
        const QString n = Dubbing::normalizeCode(c);
        if (Dubbing::find(n) && !clean.contains(n))
            clean << n;
    }
    if (clean == m_targets)
        return;
    m_targets = clean;
    Q_EMIT targetsChanged();
    Q_EMIT stateChanged();
}

void Dubber::setReferencePath(const QString &path)
{
    const QString p = path.startsWith(QLatin1String("file:")) ? QUrl(path).toLocalFile() : path;
    if (p == m_referencePath)
        return;
    m_referencePath = p;
    m_referenceInfo = p.isEmpty() ? QString() : tr("File: %1").arg(QFileInfo(p).fileName());
    Q_EMIT referenceChanged();
}

QString Dubber::outputFolder() const
{
    if (!m_outputFolder.isEmpty())
        return m_outputFolder;
    return QFileInfo(m_editor->suggestedExportPath(QStringLiteral("mp4-h264"))).absolutePath();
}

void Dubber::setOutputFolder(const QString &folder)
{
    const QString f = folder.startsWith(QLatin1String("file:")) ? QUrl(folder).toLocalFile() : folder;
    if (f == m_outputFolder)
        return;
    m_outputFolder = f;
    Q_EMIT outputChanged();
}

QString Dubber::baseName() const
{
    return QFileInfo(m_editor->suggestedExportPath(QStringLiteral("mp4-h264"))).completeBaseName();
}

QString Dubber::workDir() const
{
    return cacheRoot() + QStringLiteral("/work");
}

QString Dubber::estimate() const
{
    if (!m_ready)
        return {};
    double speech = 0;
    for (const Captions::Word &w : m_editor->timelineWords())
        speech += (w.endMs - w.startMs) / 1000.0;
    if (speech < 5)
        return {};
    // measured: ~12x the speech's length on the CPU; a graphics card runs about as long as the speech
    const double minutes = std::max(1.0, speech * (onGpu() ? 1.0 : 12.0) / 60.0);
    const int m = int(std::ceil(minutes));
    return onGpu() ? tr("About %n minute(s) per language on the graphics card", nullptr, m)
                   : tr("About %n minute(s) per language on the CPU. A graphics card is far faster.", nullptr, m);
}

// ------------------------------------------------------------------ the engine

QString Dubber::engineSummary() const
{
    if (m_checking)
        return tr("Checking…");
    if (!m_ready)
        return m_problem.isEmpty() ? tr("Not installed") : m_problem;
    const QString model = m_v3 ? QStringLiteral("Chatterbox Multilingual V3") : QStringLiteral("Chatterbox Multilingual V2");
    if (m_device == QLatin1String("cuda") && m_gpuBusy)
        return tr("%1 · %2 is busy").arg(model, m_gpu.isEmpty() ? tr("graphics card") : m_gpu);
    if (m_device == QLatin1String("cuda"))
        return tr("%1 · %2 (%3 GB)").arg(model, m_gpu, QString::number(m_vram, 'f', 0));
    return tr("%1 · CPU (slow)").arg(model);
}

void Dubber::checkEngine()
{
    if (m_checking || installing())
        return;
    const QString py = python(), sc = script();
    m_ready = false;
    m_problem.clear();
    if (py.isEmpty() || sc.isEmpty()) {
        Q_EMIT engineChanged();
        return;
    }
    m_checking = true;
    Q_EMIT engineChanged();
    auto *p = new QProcess(this);
    m_probe = p;
    p->setProcessChannelMode(QProcess::SeparateChannels);
    auto *timeout = new QTimer(p);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, p, [p] { p->kill(); });
    connect(p, &QProcess::finished, this, [this, p](int, QProcess::ExitStatus) {
        const QByteArray out = p->readAllStandardOutput();
        p->deleteLater();
        m_checking = false;
        for (const QByteArray &line : out.split('\n')) {
            const QJsonObject o = QJsonDocument::fromJson(line).object();
            if (o.value(QStringLiteral("event")).toString() != QLatin1String("ready"))
                continue;
            m_ready = o.value(QStringLiteral("ready")).toBool();
            m_v3 = o.value(QStringLiteral("v3")).toBool();
            m_device = o.value(QStringLiteral("device")).toString();
            m_gpu = o.value(QStringLiteral("gpu")).toString();
            m_vram = o.value(QStringLiteral("vramGb")).toDouble();
            m_problem = o.value(QStringLiteral("problem")).toString();
            m_gpuBusy = o.value(QStringLiteral("gpuBusy")).toBool();
        }
        if (!m_ready && m_problem.isEmpty())
            m_problem = tr("The voice engine does not start. Install it again.");
        Q_EMIT engineChanged();
        Q_EMIT stateChanged();
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        m_checking = false;
        m_problem = tr("Python could not be started.");
        Q_EMIT engineChanged();
    });
    p->start(py, {sc, QStringLiteral("check")});
    timeout->start(120 * 1000);
}

void Dubber::unloadOllama(std::function<void()> then)
{
    // every loaded model (the translation one and any other app's), each with keep_alive 0
    QNetworkRequest ps(ollamaUrl(QStringLiteral("/api/ps")));
    ps.setTransferTimeout(5000);
    QNetworkReply *list = m_network->get(ps);
    connect(list, &QNetworkReply::finished, this, [this, list, then]() {
        list->deleteLater();
        const QJsonArray models = QJsonDocument::fromJson(list->readAll()).object().value(QStringLiteral("models")).toArray();
        if (models.isEmpty()) {
            then();
            return;
        }
        auto pending = std::make_shared<int>(models.size());
        for (const QJsonValue &m : models) {
            QNetworkRequest unload(ollamaUrl(QStringLiteral("/api/generate")));
            unload.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            unload.setTransferTimeout(10000);
            QNetworkReply *r = m_network->post(unload, QJsonDocument(QJsonObject{{QStringLiteral("model"), m.toObject().value(QStringLiteral("name")).toString()},
                                                                                 {QStringLiteral("keep_alive"), 0}}).toJson(QJsonDocument::Compact));
            connect(r, &QNetworkReply::finished, this, [r, pending, then]() {
                r->deleteLater();
                if (--*pending == 0)
                    then();
            });
        }
    });
}

void Dubber::freeGpu()
{
    unloadOllama([this]() {
        // the driver needs a moment to hand the memory back
        QTimer::singleShot(1500, this, &Dubber::checkEngine);
    });
}

void Dubber::installEngine()
{
    if (installing() || m_running)
        return;
    m_installFailed = false;
    m_installStep = 0;
    m_installPyPi = false;
    runInstallStep();
}

void Dubber::cancelInstall()
{
    if (!m_install)
        return;
    m_install->disconnect(this);
    m_install->kill();
    m_install->deleteLater();
    m_install = nullptr;
    m_installFailed = true;
    m_installMessage = tr("The installation was cancelled. It continues where it stopped the next time.");
    Q_EMIT installChanged();
}

void Dubber::runInstallStep()
{
    const QString uv = QStandardPaths::findExecutable(QStringLiteral("uv"));
    const QString venv = venvDir();
    const QString venvPython = venv + QStringLiteral("/bin/python");
    QString program;
    QStringList args;
    QString label;

    if (m_installStep == 0) {
        if (QFileInfo::exists(venvPython)) { // from an earlier attempt
            m_installStep = 1;
        } else {
            QDir().mkpath(QFileInfo(venv).absolutePath());
            QString found;
            for (const char *name : {"python3.11", "python3.12", "python3.10"}) {
                found = QStandardPaths::findExecutable(QString::fromLatin1(name));
                if (!found.isEmpty())
                    break;
            }
            label = tr("Creating the Python environment…");
            if (!uv.isEmpty()) {
                // uv uses an installed 3.11 or fetches one: Chatterbox pins PyTorch 2.6, which stops at Python 3.13
                program = uv;
                args = {QStringLiteral("venv"), QStringLiteral("--python"), QStringLiteral("3.11"), venv};
            } else if (!found.isEmpty()) {
                program = found;
                args = {QStringLiteral("-m"), QStringLiteral("venv"), venv};
            } else {
                m_installFailed = true;
                m_installMessage = tr("Python 3.10 to 3.12 is needed (Chatterbox does not support newer versions yet). Install it with “sudo pacman -S uv” and try again.");
                Q_EMIT installChanged();
                return;
            }
        }
    }
    if (m_installStep == 1) {
        label = tr("Installing PyTorch and Chatterbox (about 6 GB, this takes a while)…");
        const QStringList packages = {m_installPyPi ? QStringLiteral("chatterbox-tts") : QStringLiteral("chatterbox-tts @ ") + QString::fromLatin1(ChatterboxGit),
                                      QStringLiteral("setuptools<81")}; // perth needs pkg_resources
        if (!uv.isEmpty()) {
            program = uv;
            args = QStringList{QStringLiteral("pip"), QStringLiteral("install"), QStringLiteral("--python"), venvPython} + packages;
        } else {
            program = venvPython;
            args = QStringList{QStringLiteral("-m"), QStringLiteral("pip"), QStringLiteral("install"), QStringLiteral("--no-input")} + packages;
        }
    } else if (m_installStep == 2) {
        label = tr("Downloading the voice model (about 3 GB)…");
        program = venvPython;
        args = {script(), QStringLiteral("warmup"), m_installPyPi ? QStringLiteral("v2") : QStringLiteral("v3")};
    } else if (m_installStep > 2) {
        installFinished(true);
        return;
    }

    m_install = new QProcess(this);
    m_install->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    m_install->setProcessEnvironment(env);
    m_installMessage = label;
    m_installFailed = false;
    auto log = std::make_shared<QString>();
    connect(m_install, &QProcess::readyRead, this, [this, label, log] {
        if (!m_install)
            return;
        const QString text = QString::fromUtf8(m_install->readAll());
        log->append(text);
        if (log->size() > 20000)
            log->remove(0, log->size() - 20000);
        const QString tail = lastLines(text, 1).simplified().left(90);
        if (!tail.isEmpty() && !tail.startsWith(QLatin1Char('{'))) {
            m_installMessage = label + QStringLiteral("\n") + tail;
            Q_EMIT installChanged();
        }
    });
    connect(m_install, &QProcess::finished, this, [this, log](int code, QProcess::ExitStatus status) {
        if (m_install) {
            log->append(QString::fromUtf8(m_install->readAll()));
            m_install->deleteLater();
            m_install = nullptr;
        }
        if (status == QProcess::NormalExit && code == 0) {
            ++m_installStep;
            runInstallStep();
            return;
        }
        if (m_installStep == 1 && !m_installPyPi) {
            // the pinned GitHub build is not reachable (no git, no network to GitHub): the stable PyPI one
            m_installPyPi = true;
            runInstallStep();
            return;
        }
        m_installFailed = true;
        m_installMessage = tr("The installation failed:") + QLatin1Char('\n') + lastLines(*log, 4);
        Q_EMIT installChanged();
    });
    connect(m_install, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || !m_install)
            return;
        m_install->deleteLater();
        m_install = nullptr;
        m_installFailed = true;
        m_installMessage = tr("The installer could not be started.");
        Q_EMIT installChanged();
    });
    m_install->start(program, args);
    Q_EMIT installChanged();
}

void Dubber::installFinished(bool ok)
{
    Q_UNUSED(ok)
    m_installMessage = tr("The voice engine is installed.");
    m_installFailed = false;
    Q_EMIT installChanged();
    checkEngine();
}

// ------------------------------------------------------------------ the run

void Dubber::setStage(const QString &stage, double local)
{
    m_stage = stage;
    m_progress = m_langTotal > 0 ? std::clamp((m_langDone + local) / m_langTotal, 0.0, 1.0) : -1;
    Q_EMIT stateChanged();
}

void Dubber::addNotice(const QString &text)
{
    if (text.isEmpty())
        return;
    m_notice += (m_notice.isEmpty() ? QString() : QStringLiteral("\n")) + text;
}

void Dubber::fail(const QString &message)
{
    ++m_session;
    m_running = false;
    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() == QProcess::NotRunning) {
            m_process->deleteLater();
        } else {
            connect(m_process, &QProcess::finished, m_process, &QObject::deleteLater);
            m_process->kill();
        }
    }
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
    m_error = message;
    m_stage.clear();
    m_progress = -1;
    Q_EMIT stateChanged();
    m_editor->setStatus(message);
}

void Dubber::cancel()
{
    if (!m_running)
        return;
    m_running = false; // the transcriber's own failure on cancel is then ignored
    if (m_editor->transcriber()->running())
        m_editor->transcriber()->cancel();
    fail(tr("Cancelled"));
}

QString Dubber::ollamaModel(QString *problem) const
{
    // Settings › AI: the provider and model the whole system uses
    QSettings ai(QStringLiteral("lingmoos"), QStringLiteral("ai"));
    auto value = [&ai](const char *key) {
        const QString plain = ai.value(QLatin1String(key)).toString();
        return plain.isEmpty() ? ai.value(QStringLiteral("ai/") + QLatin1String(key)).toString() : plain;
    };
    const QString provider = value("provider");
    const QString model = value("model");
    if (!provider.isEmpty() && provider != QLatin1String("ollama")) {
        *problem = tr("The translation uses an Ollama model. Choose Ollama in Settings › AI.");
        return {};
    }
    if (model.isEmpty()) {
        *problem = tr("The translation needs an AI model. Choose one in Settings › AI.");
        return {};
    }
    return model;
}

void Dubber::start()
{
    if (m_running)
        return;
    m_error.clear();
    m_notice.clear();
    if (!m_ready) {
        fail(tr("Install the voice engine first."));
        return;
    }
    if (m_targets.isEmpty()) {
        fail(tr("Choose at least one language."));
        return;
    }
    QString problem;
    m_model = ollamaModel(&problem);
    if (m_model.isEmpty()) {
        fail(problem);
        return;
    }
    if (m_editor->document().timeline.duration() < 2) {
        fail(tr("Put something on the timeline first."));
        return;
    }
    ++m_session;
    m_running = true;
    m_results.clear();
    m_langTotal = 0;
    m_langDone = 0;
    m_sourceLang.clear();
    Q_EMIT resultsChanged();
    m_jobs = m_editor->speechJobs();
    m_jobIndex = 0;
    setStage(tr("Reading what is said in the video…"), 0);
    transcribeNext();
}

void Dubber::transcribeNext()
{
    if (!m_running)
        return;
    if (m_jobIndex >= m_jobs.size()) {
        prepare();
        return;
    }
    Transcriber *t = m_editor->transcriber();
    t->refresh();
    if (t->running()) {
        fail(tr("The transcriber is busy with another task."));
        return;
    }
    if (!t->available()) {
        fail(t->problem());
        return;
    }
    const Editor::SpeechJob &j = m_jobs.at(m_jobIndex);
    setStage(tr("Transcribing the speech…"), 0);
    t->transcribe(j.path, j.startMs, j.lengthMs, QStringLiteral("dub"));
}

void Dubber::prepare()
{
    m_words = m_editor->timelineWords();
    if (m_words.size() < 3) {
        fail(tr("No speech was found on the timeline."));
        return;
    }
    if (m_sourceLang.isEmpty())
        m_sourceLang = Dubbing::normalizeCode(m_editor->transcriber()->language());
    const double fps = std::max(1.0, m_editor->fps());
    m_totalMs = qint64(std::llround(m_editor->document().timeline.duration() * 1000.0 / fps));
    m_source = Dubbing::sentences(m_words);
    m_chunks = Dubbing::chunks(m_source, m_totalMs);
    if (m_source.isEmpty() || m_chunks.isEmpty()) {
        fail(tr("No speech was found on the timeline."));
        return;
    }
    m_queue.clear();
    for (const QString &c : m_targets) {
        if (c == m_sourceLang)
            addNotice(tr("The video is already in %1: skipped.").arg(Dubbing::find(c)->name));
        else
            m_queue << c;
    }
    if (m_queue.isEmpty()) {
        fail(tr("Choose a language other than the one spoken in the video."));
        return;
    }
    m_langTotal = int(m_queue.size());
    Q_EMIT targetsChanged();
    prepareReference();
}

// ------------------------------------------------------------------ the voice to clone

void Dubber::prepareReference()
{
    setStage(tr("Choosing the voice to clone…"), 0);
    QString source;
    QStringList before, after; // ffmpeg options around the input: where to start, how long
    QString info;
    QString filters = QStringLiteral("highpass=f=70,loudnorm=I=-20:TP=-2:LRA=7");
    if (!m_referencePath.isEmpty()) {
        source = m_referencePath;
        // a file you chose: its first 15 s of sound, leading silence cut
        after = {QStringLiteral("-t"), QStringLiteral("15")};
        filters.prepend(QStringLiteral("silenceremove=start_periods=1:start_threshold=-45dB:start_silence=0.15,"));
        info = tr("File: %1").arg(QFileInfo(source).fileName());
    } else {
        QString mediaId;
        if (!m_editor->voiceSource(&mediaId, &source)) {
            fail(tr("No clip with speech to take the voice from. Choose a voice file."));
            return;
        }
        const Dubbing::Window w = Dubbing::referenceWindow(m_editor->mediaWords(mediaId));
        if (!w.valid()) {
            fail(tr("There is not enough continuous speech to clone the voice. Choose a voice file of 10 seconds or more."));
            return;
        }
        before = {QStringLiteral("-ss"), QString::number(std::max<qint64>(0, w.startMs - 150) / 1000.0, 'f', 3)};
        after = {QStringLiteral("-t"), QString::number((w.length() + 300) / 1000.0, 'f', 3)};
        info = tr("Taken from the video: %1 s starting at %2").arg(QString::number(w.length() / 1000.0, 'f', 0), Metadata::timestamp(int(w.startMs / 1000)));
    }
    QDir().mkpath(workDir());
    const QString wav = workDir() + QStringLiteral("/reference.wav");
    QFile::remove(wav);
    QStringList args = {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y")};
    args << before << QStringLiteral("-i") << source << after;
    args << QStringLiteral("-vn") << QStringLiteral("-ac") << QStringLiteral("1") << QStringLiteral("-ar") << QStringLiteral("24000") << QStringLiteral("-af") << filters << wav;

    auto *p = new QProcess(this);
    m_process = p;
    p->setProcessChannelMode(QProcess::MergedChannels);
    const int session = m_session;
    connect(p, &QProcess::finished, this, [this, p, session, wav, info](int code, QProcess::ExitStatus) {
        const QString out = QString::fromUtf8(p->readAll());
        p->deleteLater();
        if (session != m_session || !m_running)
            return;
        if (code != 0 || !QFileInfo::exists(wav) || QFileInfo(wav).size() < 40000) {
            fail(tr("Could not extract the voice sample: %1").arg(lastLines(out, 3)));
            return;
        }
        useReference(wav, info);
    });
    p->start(QStringLiteral("ffmpeg"), args);
}

void Dubber::useReference(const QString &wav, const QString &info)
{
    m_referenceWav = wav;
    m_referenceInfo = info;
    Q_EMIT referenceChanged();
    nextLanguage();
}

// ------------------------------------------------------------------ one language: translate

void Dubber::nextLanguage()
{
    if (m_queue.isEmpty()) {
        finishAll();
        return;
    }
    m_lang = Dubbing::find(m_queue.takeFirst());
    m_work = m_source;
    m_batchStart = 0;
    m_untranslated = 0;
    m_langDir = workDir() + QLatin1Char('/') + m_lang->code;
    QDir(m_langDir).removeRecursively();
    QDir().mkpath(m_langDir);
    translateNext();
}

void Dubber::translateNext()
{
    if (!m_running)
        return;
    if (m_batchStart >= m_work.size()) {
        if (m_untranslated > 0)
            addNotice(tr("%1: %n sentence(s) could not be translated and stay in the original language.", nullptr, m_untranslated).arg(m_lang->name));
        // Ollama keeps models in video memory for minutes after the last request: free
        // them first, or the voice engine finds the GPU full and falls back to the CPU
        unloadOllama([this]() {
            if (m_running)
                startSynth();
        });
        return;
    }
    // up to 10 sentences or ~1500 characters at a time
    int chars = 0;
    m_batchEnd = m_batchStart;
    while (m_batchEnd < m_work.size() && m_batchEnd - m_batchStart < 10 && (chars == 0 || chars + m_work.at(m_batchEnd).text.size() < 1500)) {
        chars += m_work.at(m_batchEnd).text.size();
        ++m_batchEnd;
    }
    QList<int> ids;
    for (int i = m_batchStart; i < m_batchEnd; ++i)
        ids << i;
    m_retried = false;
    setStage(tr("Translating to %1…").arg(m_lang->name), 0.15 * m_batchStart / double(m_work.size()));
    sendTranslation(ids, false);
}

void Dubber::sendTranslation(const QList<int> &ids, bool retry)
{
    QList<Dubbing::Sentence> batch, context;
    for (int id : ids)
        batch << m_work.at(id);
    for (int i = std::max(0, ids.first() - 3); i < ids.first(); ++i)
        context << m_work.at(i);
    const Dubbing::Language *source = Dubbing::find(m_sourceLang);

    QNetworkRequest req(ollamaUrl(QStringLiteral("/api/chat")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(240 * 1000);
    const int session = m_session;
    m_reply = m_network->post(req, Dubbing::translateRequest(m_model, batch, context, *m_lang, source ? source->name : QString()));
    connect(m_reply, &QNetworkReply::finished, this, [this, session, ids, retry] {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        if (!r)
            return;
        r->deleteLater();
        if (session != m_session || !m_running)
            return;
        if (r->error() != QNetworkReply::NoError) {
            fail(tr("The AI model %1 did not answer (%2). Is Ollama running?").arg(m_model, r->errorString()));
            return;
        }
        const QMap<int, QString> done = Dubbing::parseTranslation(r->readAll(), ids);
        QList<int> missing;
        for (int id : ids) {
            if (done.contains(id))
                m_work[id].translation = done.value(id);
            else if (m_work.at(id).translation.isEmpty())
                missing << id;
        }
        if (!missing.isEmpty() && !retry) {
            sendTranslation(missing, true); // once more, only what is missing
            return;
        }
        for (int id : missing) {
            m_work[id].translation = m_work.at(id).text; // better the original than silence
            ++m_untranslated;
        }
        m_batchStart = m_batchEnd;
        translateNext();
    });
}

// ------------------------------------------------------------------ one language: speak

void Dubber::startSynth()
{
    QJsonArray segments;
    for (const Dubbing::Chunk &c : m_chunks) {
        const QString text = Dubbing::chunkText(m_work, c);
        if (text.isEmpty())
            continue;
        segments.append(QJsonObject{{QStringLiteral("id"), c.id()}, {QStringLiteral("text"), text}, {QStringLiteral("slot"), (c.slotEndMs - c.startMs) / 1000.0}});
    }
    const QString cache = cacheRoot() + QStringLiteral("/synth");
    QDir().mkpath(cache);
    const QJsonObject job{{QStringLiteral("language"), m_lang->code},
                          {QStringLiteral("reference"), m_referenceWav},
                          {QStringLiteral("out_dir"), m_langDir},
                          {QStringLiteral("cache_dir"), cache},
                          {QStringLiteral("model"), m_v3 ? QStringLiteral("v3") : QStringLiteral("v2")},
                          {QStringLiteral("exaggeration"), 0.5},
                          // 0: the voice's timbre without the accent of the language it was recorded in
                          {QStringLiteral("cfg_weight"), 0.0},
                          {QStringLiteral("temperature"), 0.8},
                          {QStringLiteral("seed"), 1234},
                          {QStringLiteral("attempts"), onGpu() ? 3 : 1},
                          {QStringLiteral("device"), QStringLiteral("auto")},
                          {QStringLiteral("segments"), segments}};
    const QString jobPath = m_langDir + QStringLiteral("/job.json");
    QSaveFile f(jobPath);
    if (!f.open(QIODevice::WriteOnly)) {
        fail(tr("Could not write %1").arg(jobPath));
        return;
    }
    f.write(QJsonDocument(job).toJson());
    f.commit();

    setStage(tr("Loading the voice model…"), 0.15);
    m_synthLog.clear();
    m_synthBuffer.clear();
    QProcess *p = startPython({script(), QStringLiteral("synth"), jobPath}, QStringLiteral("synth"));
    if (!p)
        return;
    const int total = int(segments.size());
    const int session = m_session;
    auto errorText = std::make_shared<QString>();
    connect(p, &QProcess::readyReadStandardOutput, this, [this, p, total, errorText, session] {
        if (session != m_session)
            return;
        m_synthBuffer += QString::fromUtf8(p->readAllStandardOutput());
        int nl;
        while ((nl = m_synthBuffer.indexOf(QLatin1Char('\n'))) >= 0) {
            const QByteArray line = m_synthBuffer.left(nl).toUtf8();
            m_synthBuffer.remove(0, nl + 1);
            const QJsonObject o = QJsonDocument::fromJson(line).object();
            const QString ev = o.value(QStringLiteral("event")).toString();
            if (ev == QLatin1String("progress")) {
                const int done = o.value(QStringLiteral("done")).toInt();
                setStage(tr("Speaking in %1 (%2 of %3)…").arg(m_lang->name).arg(done).arg(total), 0.15 + 0.75 * done / std::max(1, total));
            } else if (ev == QLatin1String("error")) {
                *errorText = o.value(QStringLiteral("message")).toString();
            }
        }
    });
    connect(p, &QProcess::readyReadStandardError, this, [this, p, session] {
        if (session != m_session)
            return;
        m_synthLog += QString::fromUtf8(p->readAllStandardError());
        if (m_synthLog.size() > 20000)
            m_synthLog.remove(0, m_synthLog.size() - 20000);
    });
    connect(p, &QProcess::finished, this, [this, p, errorText, session](int code, QProcess::ExitStatus status) {
        p->deleteLater();
        if (session != m_session || !m_running)
            return;
        if (status != QProcess::NormalExit || code != 0) {
            fail(tr("The voice engine failed: %1").arg(!errorText->isEmpty() ? *errorText : lastLines(m_synthLog, 3)));
            return;
        }
        synthFinished(code);
    });
}

QProcess *Dubber::startPython(const QStringList &args, const QString &stage)
{
    Q_UNUSED(stage)
    const QString py = python();
    if (py.isEmpty() || args.value(0).isEmpty()) {
        fail(tr("The voice engine is not installed."));
        return nullptr;
    }
    auto *p = new QProcess(this);
    m_process = p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    p->setProcessEnvironment(env);
    p->setProcessChannelMode(QProcess::SeparateChannels);
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && m_running) {
            p->deleteLater();
            fail(tr("Python could not be started."));
        }
    });
    p->start(py, args);
    return p;
}

void Dubber::synthFinished(int)
{
    QFile f(m_langDir + QStringLiteral("/result.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        fail(tr("The voice engine produced no result."));
        return;
    }
    assemble(QJsonDocument::fromJson(f.readAll()).object().toVariantMap());
}

// ------------------------------------------------------------------ one language: place and mix

void Dubber::assemble(const QVariantMap &spoken)
{
    setStage(tr("Fitting the speech to the video…"), 0.9);
    const double total = m_totalMs / 1000.0;
    QList<Dubbing::FitInput> inputs;
    QHash<QString, QString> files;
    for (const Dubbing::Chunk &c : m_chunks) {
        const QVariantMap r = spoken.value(c.id()).toMap();
        if (r.isEmpty())
            continue;
        files.insert(c.id(), r.value(QStringLiteral("file")).toString());
        inputs.append({c.id(), c.startMs / 1000.0, c.slotEndMs / 1000.0, r.value(QStringLiteral("seconds")).toDouble()});
    }
    m_fit = Dubbing::fit(inputs, total);

    QJsonArray items;
    for (const Dubbing::Placement &p : m_fit.items)
        items.append(QJsonObject{{QStringLiteral("file"), files.value(p.id)}, {QStringLiteral("start"), p.startSec}, {QStringLiteral("ratio"), p.ratio}});
    const QString out = m_langDir + QStringLiteral("/dub.wav");
    const QString planPath = m_langDir + QStringLiteral("/plan.json");
    QSaveFile f(planPath);
    if (!f.open(QIODevice::WriteOnly)) {
        fail(tr("Could not write %1").arg(planPath));
        return;
    }
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("length"), total}, {QStringLiteral("output"), out}, {QStringLiteral("items"), items}}).toJson());
    f.commit();

    QProcess *p = startPython({script(), QStringLiteral("assemble"), planPath}, QStringLiteral("assemble"));
    if (!p)
        return;
    const int session = m_session;
    connect(p, &QProcess::finished, this, [this, p, out, total, session](int code, QProcess::ExitStatus status) {
        const QString err = QString::fromUtf8(p->readAllStandardError());
        p->deleteLater();
        if (session != m_session || !m_running)
            return;
        if (status != QProcess::NormalExit || code != 0 || !QFileInfo::exists(out)) {
            fail(tr("The voice engine could not build the track: %1").arg(lastLines(err, 3)));
            return;
        }
        encode(out, total);
    });
}

void Dubber::encode(const QString &wav, double seconds)
{
    setStage(tr("Saving the audio…"), 0.96);
    const QString folder = outputFolder();
    if (!QDir().mkpath(folder)) {
        fail(tr("Could not create the folder %1").arg(folder));
        return;
    }
    const QString stem = QDir(folder).filePath(baseName() + QLatin1Char('-') + m_lang->code);
    const QString outWav = stem + QStringLiteral(".wav");
    const QString outMp3 = stem + QStringLiteral(".mp3");
    const int session = m_session;
    // YouTube normalises loudness anyway: -16 LUFS is what it plays voice at
    auto *p = new QProcess(this);
    m_process = p;
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::finished, this, [this, p, session, outWav, outMp3, seconds](int code, QProcess::ExitStatus) {
        const QString text = QString::fromUtf8(p->readAll());
        p->deleteLater();
        if (session != m_session || !m_running)
            return;
        if (code != 0 || !QFileInfo::exists(outWav)) {
            fail(tr("Could not save the audio: %1").arg(lastLines(text, 3)));
            return;
        }
        auto *q = new QProcess(this);
        m_process = q;
        q->setProcessChannelMode(QProcess::MergedChannels);
        connect(q, &QProcess::finished, this, [this, q, session, outWav, outMp3, seconds](int c, QProcess::ExitStatus) {
            q->deleteLater();
            if (session != m_session || !m_running)
                return;
            const bool mp3 = c == 0 && QFileInfo::exists(outMp3);
            const double speedShare = m_fit.items.isEmpty() ? 0 : double(m_fit.sped) / m_fit.items.size();
            QVariantMap r{{QStringLiteral("code"), m_lang->code},
                          {QStringLiteral("name"), m_lang->name},
                          {QStringLiteral("native"), m_lang->native},
                          {QStringLiteral("wav"), outWav},
                          {QStringLiteral("mp3"), mp3 ? outMp3 : QString()},
                          {QStringLiteral("path"), mp3 ? outMp3 : outWav},
                          {QStringLiteral("seconds"), seconds},
                          {QStringLiteral("passages"), int(m_fit.items.size())},
                          {QStringLiteral("sped"), m_fit.sped},
                          {QStringLiteral("maxSpeedup"), m_fit.maxSpeedup}};
            if (speedShare > 0.35 || m_fit.maxSpeedup >= 1.28)
                addNotice(tr("%1: the translation runs longer than the original, so %2 of %3 passages were sped up (by up to %4×). "
                             "It still sounds natural, but a shorter translation would be better.")
                              .arg(m_lang->name)
                              .arg(m_fit.sped)
                              .arg(m_fit.items.size())
                              .arg(QString::number(m_fit.maxSpeedup, 'f', 2)));
            if (m_fit.overflowEndSec > 0.5)
                addNotice(tr("%1: the last passage runs %2 s past the end of the video and was cut.").arg(m_lang->name, QString::number(m_fit.overflowEndSec, 'f', 1)));
            finishLanguage(r);
        });
        q->start(QStringLiteral("ffmpeg"), {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
                                            QStringLiteral("-i"), outWav, QStringLiteral("-c:a"), QStringLiteral("libmp3lame"), QStringLiteral("-b:a"),
                                            QStringLiteral("256k"), outMp3});
    });
    p->start(QStringLiteral("ffmpeg"),
             {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-i"), wav,
              QStringLiteral("-af"), QStringLiteral("loudnorm=I=-16:TP=-1.5:LRA=11,aresample=48000"), QStringLiteral("-ac"), QStringLiteral("2"),
              QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), outWav});
}

void Dubber::finishLanguage(const QVariantMap &result)
{
    m_results.append(result);
    ++m_langDone;
    Q_EMIT resultsChanged();
    setStage(QString(), 0);
    nextLanguage();
}

void Dubber::finishAll()
{
    m_running = false;
    m_stage.clear();
    m_progress = 1;
    Q_EMIT stateChanged();
    m_editor->setStatus(tr("Dubbing is ready: %n language(s) saved", nullptr, int(m_results.size())));
}

// ------------------------------------------------------------------ results

void Dubber::addToTimeline(const QString &code)
{
    for (const QVariant &v : std::as_const(m_results)) {
        const QVariantMap r = v.toMap();
        if (r.value(QStringLiteral("code")).toString() == code) {
            m_editor->placeDubTrack(r.value(QStringLiteral("wav")).toString(),
                                    tr("Dub · %1").arg(r.value(QStringLiteral("name")).toString()));
            return;
        }
    }
}

void Dubber::openFolder() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(outputFolder()));
}
