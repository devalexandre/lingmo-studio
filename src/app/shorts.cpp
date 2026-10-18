/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "shorts.h"
#include "editor.h"
#include "platforms.h"
#include "player.h"
#include "reframe.h"
#include "renderqueue.h"
#include "transcriber.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <cmath>
#include <limits>

using namespace Studio;

static QString tr(const char *s)
{
    return QCoreApplication::translate("Shorts", s);
}

static QUrl ollamaUrl(const QString &path)
{
    QString host = qEnvironmentVariable("OLLAMA_HOST", QStringLiteral("127.0.0.1:11434"));
    if (!host.contains(QLatin1String("://")))
        host.prepend(QLatin1String("http://"));
    QUrl u(host);
    u.setPath(path);
    return u;
}

ShortsGenerator::ShortsGenerator(Editor *editor)
    : QObject(editor)
    , m_editor(editor)
    , m_network(new QNetworkAccessManager(this))
{
    m_platforms = {QStringLiteral("tiktok"), QStringLiteral("instagram-reels"), QStringLiteral("youtube-shorts")};
    connect(m_editor->transcriber(), &Transcriber::finished, this, [this](const QString &task) {
        if (task != QLatin1String("shorts") || !m_running)
            return;
        m_transcript = m_editor->transcriber()->result();
        m_editor->rememberWords(m_mediaId, m_transcript.words);
        runAnalysisTools();
    });
    connect(m_editor->transcriber(), &Transcriber::failed, this, [this](const QString &task, const QString &message) {
        if (task == QLatin1String("shorts") && m_running)
            fail(message);
    });
    connect(m_editor->transcriber(), &Transcriber::progressChanged, this, [this] {
        if (m_running && m_editor->transcriber()->running())
            setState(m_editor->transcriber()->stage(), std::max(0.0, m_editor->transcriber()->progress()) * 0.7);
    });
    // OpenCV through Python, checked once in the background
    auto *probe = new QProcess(this);
    m_probe = probe;
    connect(probe, &QProcess::finished, this, [this, probe](int code, QProcess::ExitStatus st) {
        m_opencv = st == QProcess::NormalExit && code == 0 && !Reframe::detectorScript().isEmpty();
        probe->deleteLater();
        Q_EMIT toolsChanged();
    });
    connect(probe, &QProcess::errorOccurred, probe, [probe](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            probe->deleteLater();
    });
    probe->start(QStringLiteral("python3"), {QStringLiteral("-c"), QStringLiteral("import cv2")});

    m_exportFolder = QSettings().value(QStringLiteral("shorts/exportFolder")).toString();
    if (m_exportFolder.isEmpty())
        m_exportFolder = Editor::defaultShortsFolder();
    // per-cut export progress from the render queue
    auto *queue = static_cast<RenderQueue *>(m_editor->renderQueueObject());
    connect(queue, &QAbstractItemModel::dataChanged, this, &ShortsGenerator::syncExports);
    connect(queue, &QAbstractItemModel::rowsInserted, this, &ShortsGenerator::syncExports);
    connect(queue, &QAbstractItemModel::rowsRemoved, this, &ShortsGenerator::syncExports);
    connect(queue, &RenderQueue::jobFinished, this, &ShortsGenerator::syncExports);
    connect(m_editor, &Editor::documentChanged, this, &ShortsGenerator::exportChanged);
    connect(this, &ShortsGenerator::trackingChanged, this, &ShortsGenerator::exportChanged); // "reframing" on the cuts
    // an export asked for while faces were being followed starts afterwards
    connect(this, &ShortsGenerator::trackingChanged, this, [this] {
        if (m_exportPending && !tracking())
            startExport();
    });
}

ShortsGenerator::~ShortsGenerator()
{
    for (QProcess *p : {m_process.data(), m_trackProcess.data(), m_probe.data()})
        if (p) {
            p->disconnect(this);
            p->kill();
            p->waitForFinished(1000);
        }
}

QVariantList ShortsGenerator::platforms() const
{
    QVariantList list;
    for (const Platform &p : Platforms::all())
        list.append(p.toMap());
    return list;
}

void ShortsGenerator::setSelectedPlatforms(const QStringList &ids)
{
    QStringList valid;
    for (const QString &id : ids)
        if (Platforms::find(id).valid() && !valid.contains(id))
            valid << id;
    if (valid == m_platforms)
        return;
    m_platforms = valid;
    Q_EMIT optionsChanged();
}

void ShortsGenerator::togglePlatform(const QString &id)
{
    QStringList ids = m_platforms;
    if (ids.contains(id))
        ids.removeAll(id);
    else
        ids << id;
    setSelectedPlatforms(ids);
}

void ShortsGenerator::setMediaId(const QString &id)
{
    if (id == m_mediaId)
        return;
    m_mediaId = id;
    clear();
    Q_EMIT optionsChanged();
}

void ShortsGenerator::setCount(int n)
{
    n = std::clamp(n, 1, 20);
    if (n == m_count)
        return;
    m_count = n;
    Q_EMIT optionsChanged();
}

void ShortsGenerator::setCaptions(bool on)
{
    if (on == m_captions)
        return;
    m_captions = on;
    Q_EMIT optionsChanged();
}

void ShortsGenerator::setCaptionStyle(const QString &id)
{
    if (id == m_style || !Captions::Style::presetIds().contains(id))
        return;
    m_style = id;
    Q_EMIT optionsChanged();
}

void ShortsGenerator::setReframe(bool on)
{
    if (on == m_reframe)
        return;
    m_reframe = on;
    Q_EMIT optionsChanged();
}

// The length range that suits every selected platform
int ShortsGenerator::minSeconds() const
{
    int lo = 0, hi = 1 << 20;
    for (const QString &id : m_platforms) {
        const Platform p = Platforms::find(id);
        if (!p.valid())
            continue;
        lo = std::max(lo, p.idealMin());
        hi = std::min(hi, p.idealMax());
    }
    return lo > 0 ? std::min(lo, hi) : 15;
}

int ShortsGenerator::maxSeconds() const
{
    int hi = 1 << 20;
    for (const QString &id : m_platforms) {
        const Platform p = Platforms::find(id);
        if (p.valid())
            hi = std::min(hi, p.idealMax());
    }
    return hi < (1 << 20) ? std::max(hi, minSeconds()) : 60;
}

bool ShortsGenerator::mixedAspects() const
{
    bool same = true;
    Platforms::primary(m_platforms, &same);
    return !same;
}

const MediaItem *ShortsGenerator::media() const
{
    return m_editor->mediaItem(m_mediaId);
}

QVariantList ShortsGenerator::candidates() const
{
    QVariantList list;
    const MediaItem *m = media();
    const double fps = m_editor->fps();
    for (const Highlights::Candidate &c : m_candidates) {
        QVariantMap v = c.toMap();
        v.insert(QStringLiteral("selected"), !m_unselected.contains(c.id));
        v.insert(QStringLiteral("range"), formatTime(c.startMs) + QStringLiteral(" – ") + formatTime(c.endMs));
        const int thumb = int(std::lround((c.startMs + std::min<qint64>(1500, (c.endMs - c.startMs) / 3)) * fps / 1000.0));
        v.insert(QStringLiteral("thumbnail"), m ? QStringLiteral("image://thumb/%1/%2").arg(m->id).arg(thumb) : QString());
        v.insert(QStringLiteral("llm"), c.llmScore >= 0);
        list.append(v);
    }
    return list;
}

QVariantList ShortsGenerator::mediaChoices() const
{
    QVariantList list;
    for (const MediaItem &m : m_editor->media()) {
        if (!m.hasAudio)
            continue;
        list.append(QVariantMap{{QStringLiteral("id"), m.id},
                                {QStringLiteral("name"), m.name},
                                {QStringLiteral("duration"), formatTime(qint64(m.length * 1000.0 / m_editor->fps()))},
                                {QStringLiteral("video"), m.hasVideo},
                                {QStringLiteral("aspect"), m.hasVideo && m.width > 0 && m.height > 0 ? double(m.width) / m.height : 0.0}});
    }
    return list;
}

int ShortsGenerator::selectedCount() const
{
    int n = 0;
    for (const Highlights::Candidate &c : m_candidates)
        n += !m_unselected.contains(c.id);
    return n;
}

QString ShortsGenerator::formatTime(qint64 ms) const
{
    const qint64 s = std::max<qint64>(0, ms) / 1000;
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

void ShortsGenerator::setState(const QString &stage, double progress)
{
    m_stage = stage;
    m_progress = progress;
    Q_EMIT stateChanged();
}

void ShortsGenerator::fail(const QString &message)
{
    m_running = false;
    m_error = message;
    setState(QString(), -1);
}

void ShortsGenerator::clear()
{
    if (m_running)
        cancel();
    m_candidates.clear();
    m_pool.clear();
    m_unselected.clear();
    m_usedLlm = false;
    m_error.clear();
    if (!m_testData)
        m_transcript = {};
    Q_EMIT candidatesChanged();
    Q_EMIT stateChanged();
}

void ShortsGenerator::setTestData(const Captions::Transcript &t, const Highlights::Features &f)
{
    m_testData = !t.isEmpty();
    m_transcript = t;
    m_features = f;
}

void ShortsGenerator::analyze()
{
    if (m_running)
        return;
    const MediaItem *m = media();
    if (!m || !m->hasAudio) {
        m_error = tr("Choose a video with speech.");
        Q_EMIT stateChanged();
        return;
    }
    if (m_platforms.isEmpty()) {
        m_error = tr("Choose at least one platform.");
        Q_EMIT stateChanged();
        return;
    }
    m_error.clear();
    m_candidates.clear();
    m_unselected.clear();
    m_usedLlm = false;
    Q_EMIT candidatesChanged();
    m_running = true;
    if (m_testData) {
        m_editor->rememberWords(m_mediaId, m_transcript.words);
        buildCandidates();
        return;
    }
    m_features = {};
    setState(tr("Transcribing…"), 0);
    m_editor->transcriber()->transcribe(m->path, 0, -1, QStringLiteral("shorts"));
}

void ShortsGenerator::runFfmpeg(const QStringList &args, std::function<void(const QString &)> done)
{
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    m_process->setProcessEnvironment(env);
    connect(m_process, &QProcess::finished, this, [this, done](int, QProcess::ExitStatus) {
        QProcess *p = m_process;
        m_process = nullptr;
        const QString out = p ? QString::fromUtf8(p->readAll()) : QString();
        if (p)
            p->deleteLater();
        if (m_running)
            done(out);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            fail(tr("ffmpeg is not installed"));
    });
    m_process->start(QStringLiteral("ffmpeg"), args);
}

void ShortsGenerator::runAnalysisTools()
{
    const MediaItem *m = media();
    if (!m) {
        fail(tr("The video is no longer in the project."));
        return;
    }
    const QString path = m->path;
    const bool video = m->hasVideo;
    setState(tr("Listening for pauses and loud moments…"), 0.72);
    runFfmpeg(AiTools::silenceArguments(path, -35, 0.7), [this, path, video](const QString &out) {
        m_features.silences = AiTools::parseSilence(out);
        setState(tr("Measuring the energy of the audio…"), 0.76);
        runFfmpeg(Highlights::loudnessCurveArguments(path), [this, path, video](const QString &out2) {
            m_features.loudness = Highlights::parseLoudnessCurve(out2);
            if (!video) {
                buildCandidates();
                return;
            }
            setState(tr("Finding scene changes…"), 0.8);
            runFfmpeg(AiTools::sceneArguments(path, 0.35), [this](const QString &out3) {
                m_features.scenes = AiTools::parseScenes(out3);
                buildCandidates();
            });
        });
    });
}

void ShortsGenerator::buildCandidates()
{
    const MediaItem *m = media();
    const qint64 mediaMs = m && m_editor->fps() > 0 ? qint64(m->length * 1000.0 / m_editor->fps()) : -1;
    const QList<Captions::Word> &words = m_transcript.words;
    if (words.isEmpty()) {
        fail(tr("No speech was found in this video."));
        return;
    }
    const int lo = minSeconds(), hi = maxSeconds();
    const double ideal = (lo + hi) / 2.0;
    QList<Highlights::Candidate> all = Highlights::candidates(Highlights::sentences(words), words, lo, hi, mediaMs);
    if (all.isEmpty()) // short video: take what there is
        all = Highlights::candidates(Highlights::sentences(words), words, std::min(lo, 5), hi, mediaMs);
    if (all.isEmpty()) {
        fail(tr("The video is too short for these platforms."));
        return;
    }
    for (Highlights::Candidate &c : all) {
        c.heuristic = Highlights::heuristicScore(c, words, m_features, ideal);
        c.score = c.heuristic;
        c.platforms = m_platforms;
        // title until the LLM gives a better one: the opening sentence
        QString first = c.text.section(QRegularExpression(QStringLiteral("(?<=[.!?…])\\s")), 0, 0).simplified();
        if (first.size() > 60)
            first = first.left(57).section(QLatin1Char(' '), 0, -2) + QStringLiteral("…");
        c.title = first;
    }
    m_pool = Highlights::pickBest(all, m_count * 2 + 2, 0.25); // distinct moments to score
    scoreWithLlm();
}

void ShortsGenerator::scoreWithLlm()
{
    // Settings › AI: the provider and model the whole system uses
    QSettings ai(QStringLiteral("lingmoos"), QStringLiteral("ai"));
    auto value = [&ai](const char *key) {
        const QString plain = ai.value(QLatin1String(key)).toString();
        return plain.isEmpty() ? ai.value(QStringLiteral("ai/") + QLatin1String(key)).toString() : plain;
    };
    const QString provider = value("provider");
    m_llmModel = value("model");
    if ((!provider.isEmpty() && provider != QLatin1String("ollama")) || m_llmModel.isEmpty()) {
        m_llmModel.clear();
        finishAnalysis();
        return;
    }
    setState(tr("Asking the local AI (%1)…").arg(m_llmModel), 0.84);
    QNetworkRequest req(ollamaUrl(QStringLiteral("/api/tags")));
    req.setTransferTimeout(3000);
    m_reply = m_network->get(req);
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        if (!r)
            return;
        r->deleteLater();
        if (!m_running)
            return;
        bool found = false;
        if (r->error() == QNetworkReply::NoError) {
            for (const QJsonValue &v : QJsonDocument::fromJson(r->readAll()).object().value(QStringLiteral("models")).toArray()) {
                const QString name = v.toObject().value(QStringLiteral("name")).toString();
                found |= name == m_llmModel || name == m_llmModel + QStringLiteral(":latest");
            }
        }
        if (!found) { // Ollama off or model missing: the heuristic decides
            m_llmModel.clear();
            finishAnalysis();
            return;
        }
        m_scoreIndex = 0;
        scoreNext();
    });
}

void ShortsGenerator::scoreNext()
{
    if (!m_running)
        return;
    if (m_scoreIndex >= m_pool.size()) {
        finishAnalysis();
        return;
    }
    const Highlights::Candidate &c = m_pool.at(m_scoreIndex);
    setState(tr("Scoring with %1 (%2 of %3)…").arg(m_llmModel).arg(m_scoreIndex + 1).arg(m_pool.size()),
             0.84 + 0.15 * m_scoreIndex / std::max<qsizetype>(1, m_pool.size()));
    const Platform p = Platforms::find(m_platforms.value(0));
    QNetworkRequest req(ollamaUrl(QStringLiteral("/api/chat")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(120 * 1000);
    m_reply = m_network->post(req, Highlights::llmRequest(m_llmModel, c, p.name, m_transcript.language));
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        if (!r)
            return;
        r->deleteLater();
        if (!m_running)
            return;
        if (r->error() == QNetworkReply::NoError) {
            const Highlights::LlmScore s = Highlights::parseLlmReply(r->readAll());
            if (s.ok && m_scoreIndex < m_pool.size()) {
                Highlights::Candidate &c = m_pool[m_scoreIndex];
                c.llmScore = s.score;
                c.score = Highlights::penaliseRepetition(Highlights::combine(c.heuristic, s.score), c.text);
                if (!s.title.isEmpty())
                    c.title = s.title;
                c.reason = s.reason;
                m_usedLlm = true;
            }
        }
        ++m_scoreIndex;
        scoreNext();
    });
}

void ShortsGenerator::finishAnalysis()
{
    for (Highlights::Candidate &c : m_pool) {
        if (c.reason.isEmpty()) {
            const Highlights::ScoreParts p = Highlights::scoreParts(c, m_transcript.words, m_features, (minSeconds() + maxSeconds()) / 2.0);
            QStringList why;
            if (p.hook >= 0.7)
                why << tr("opens with a hook");
            if (p.density >= 0.8)
                why << tr("lively speech");
            if (p.energy >= 0.6)
                why << tr("louder than the rest");
            if (p.scenes >= 0.5)
                why << tr("visual changes");
            if (p.complete >= 1)
                why << tr("ends on a complete sentence");
            if (p.pauses >= 0.3)
                why << tr("has long pauses");
            c.reason = why.isEmpty() ? tr("Fits the length") : why.join(QStringLiteral(", "));
            c.reason[0] = c.reason.at(0).toUpper();
        }
    }
    m_candidates = Highlights::pickBest(m_pool, m_count, 0.25);
    int n = 0;
    for (Highlights::Candidate &c : m_candidates)
        c.id = QStringLiteral("cut%1").arg(++n);
    m_running = false;
    setState(m_candidates.isEmpty() ? QString() : tr("%n cut(s) found", nullptr, int(m_candidates.size())), 1);
    Q_EMIT candidatesChanged();
    Q_EMIT analysisFinished();
}

void ShortsGenerator::cancel()
{
    if (!m_running)
        return;
    m_running = false;
    if (m_editor->transcriber()->running())
        m_editor->transcriber()->cancel();
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->deleteLater();
    }
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
    m_error = tr("Cancelled");
    setState(QString(), -1);
}

void ShortsGenerator::setSelected(const QString &id, bool on)
{
    if (on)
        m_unselected.remove(id);
    else
        m_unselected.insert(id);
    Q_EMIT candidatesChanged();
}

void ShortsGenerator::trim(const QString &id, bool start, int deltaMs)
{
    for (Highlights::Candidate &c : m_candidates) {
        if (c.id != id)
            continue;
        const QList<Captions::Word> &w = m_transcript.words;
        qint64 target = (start ? c.startMs : c.endMs) + deltaMs;
        // snap to the nearest word edge in the direction of the move
        qint64 best = target;
        qint64 bestDist = std::numeric_limits<qint64>::max();
        for (const Captions::Word &word : w) {
            const qint64 edge = start ? std::max<qint64>(0, word.startMs - 250) : word.endMs + 400;
            if ((deltaMs < 0 && edge > (start ? c.startMs : c.endMs) - 1) || (deltaMs > 0 && edge < (start ? c.startMs : c.endMs) + 1))
                continue;
            const qint64 d = std::abs(edge - target);
            if (d < bestDist) {
                bestDist = d;
                best = edge;
            }
        }
        if (bestDist > 3000)
            best = target;
        if (start)
            c.startMs = std::clamp<qint64>(best, 0, c.endMs - 1000);
        else
            c.endMs = std::max<qint64>(best, c.startMs + 1000);
        if (const MediaItem *m = media())
            c.endMs = std::min<qint64>(c.endMs, qint64(m->length * 1000.0 / m_editor->fps()));
        QStringList texts;
        for (const Captions::Word &word : w)
            if (word.startMs >= c.startMs && word.endMs <= c.endMs)
                texts << word.text;
        c.text = texts.join(QLatin1Char(' '));
        Q_EMIT candidatesChanged();
        return;
    }
}

void ShortsGenerator::preview(const QString &id)
{
    const MediaItem *m = media();
    if (!m)
        return;
    for (const Highlights::Candidate &c : std::as_const(m_candidates)) {
        if (c.id != id)
            continue;
        Player *p = m_editor->player();
        if (p->sourceMediaId() != m->id)
            p->openSource(m->id, m->path, m->length);
        const int in = int(std::lround(c.startMs * m_editor->fps() / 1000.0));
        const int out = int(std::lround(c.endMs * m_editor->fps() / 1000.0));
        setPreviewCaptions(&c);
        p->setMode(QStringLiteral("source"));
        p->setSourceIn(in);
        p->setSourceOut(out);
        p->seek(in);
        p->play();
        return;
    }
}

void ShortsGenerator::stopPreview()
{
    Player *p = m_editor->player();
    p->pause();
    p->setMode(QStringLiteral("timeline"));
    setPreviewCaptions(nullptr);
}

QList<Captions::Word> ShortsGenerator::wordsInRange(const QList<Captions::Word> &words, qint64 startMs, qint64 endMs)
{
    QList<Captions::Word> local;
    for (Captions::Word w : words) {
        const qint64 mid = (w.startMs + w.endMs) / 2;
        if (mid < startMs || mid >= endMs)
            continue;
        w.startMs = std::max<qint64>(0, w.startMs - startMs);
        w.endMs = std::min<qint64>(endMs - startMs, w.endMs - startMs);
        local.append(w);
    }
    return local;
}

// The caption track the cut will get (Editor::shortCaptionTrack, the same
// style, safe area, frame size and frame rate), as an ASS script in the
// source's time so it plays over the source between the cut's edges
QString ShortsGenerator::previewCaptionsFor(qint64 startMs, qint64 endMs) const
{
    if (!m_captions || m_style.isEmpty() || m_transcript.words.isEmpty())
        return {};
    const Platform platform = Platforms::primary(m_platforms);
    const QSize size = platform.valid() ? QSize(platform.width, platform.height) : QSize(1080, 1920);
    Captions::Style style = Captions::Style::preset(m_style);
    style.safeArea = platform.safeArea;
    const double fps = platform.valid() ? platform.fps : m_editor->fps();
    const int length = int(std::lround(endMs * fps / 1000.0)) - int(std::lround(startMs * fps / 1000.0));
    const Track track = Editor::shortCaptionTrack(m_transcript.words, startMs, endMs, style, size.width(), size.height(), fps, length);
    QList<Captions::Cue> cues = Captions::trackCues(track, fps, style, size.width(), size.height());
    for (Captions::Cue &cue : cues) {
        cue.startMs += startMs;
        cue.endMs += startMs;
        for (Captions::Word &w : cue.words) {
            w.startMs += startMs;
            w.endMs += startMs;
        }
    }
    return cues.isEmpty() ? QString() : Editor::cacheCaptions(Captions::toAss(cues, style, size.width(), size.height(), fps));
}

void ShortsGenerator::setPreviewCaptions(const Highlights::Candidate *c)
{
    const Platform platform = Platforms::primary(m_platforms);
    const QSize size = platform.valid() ? QSize(platform.width, platform.height) : QSize(1080, 1920);
    const QString file = c ? previewCaptionsFor(c->startMs, c->endMs) : QString();
    if (file != m_previewAss || size != m_previewSize) {
        m_previewAss = file;
        m_previewSize = size;
        Q_EMIT previewChanged();
    }
}

int ShortsGenerator::createCuts()
{
    const MediaItem *m = media();
    if (!m || m_candidates.isEmpty())
        return 0;
    QList<Editor::ShortCut> cuts;
    for (const Highlights::Candidate &c : std::as_const(m_candidates)) {
        if (m_unselected.contains(c.id))
            continue;
        cuts.append({c.startMs, c.endMs, c.title, c.reason, c.score});
    }
    if (cuts.isEmpty())
        return 0;
    stopPreview();
    const MediaItem item = *m;
    const QList<QPair<QString, QString>> made =
        m_editor->createShorts(item, cuts, m_platforms, m_captions ? m_style : QString(), m_transcript.words);
    m_cutsMade = made.size();
    // the cards became cuts: they move to "Your cuts"
    m_candidates.erase(std::remove_if(m_candidates.begin(), m_candidates.end(),
                                      [this](const Highlights::Candidate &c) { return !m_unselected.contains(c.id); }),
                       m_candidates.end());
    Q_EMIT candidatesChanged();
    Q_EMIT cutsChanged();
    Q_EMIT exportChanged();
    if (m_reframe && m_opencv && item.hasVideo) {
        for (int i = 0; i < made.size() && i < cuts.size(); ++i)
            m_trackQueue.append({made.at(i).first, made.at(i).second, item.path, cuts.at(i).startMs / 1000.0, (cuts.at(i).endMs - cuts.at(i).startMs) / 1000.0});
        Q_EMIT trackingChanged();
        trackNext();
    }
    return made.size();
}

void ShortsGenerator::trackNext()
{
    if (m_trackProcess || m_trackQueue.isEmpty()) {
        if (!m_trackProcess)
            m_trackingSequence.clear();
        Q_EMIT trackingChanged();
        Q_EMIT exportChanged();
        return;
    }
    const TrackJob job = m_trackQueue.takeFirst();
    m_trackingSequence = job.sequenceId;
    const QString script = Reframe::detectorScript();
    if (script.isEmpty()) {
        m_trackQueue.clear();
        Q_EMIT trackingChanged();
        return;
    }
    m_editor->setBusy(tr("Following faces for the vertical crop…"));
    m_trackProcess = new QProcess(this);
    connect(m_trackProcess, &QProcess::finished, this, [this, job](int code, QProcess::ExitStatus st) {
        QProcess *p = m_trackProcess;
        m_trackProcess = nullptr;
        const QByteArray out = p ? p->readAllStandardOutput() : QByteArray();
        if (p)
            p->deleteLater();
        m_editor->setBusy(QString());
        if (st == QProcess::NormalExit && code == 0) {
            QList<Reframe::Sample> samples = Reframe::parseDetections(out);
            for (Reframe::Sample &s : samples)
                s.t -= job.startSec; // relative to the cut, like the clip's keyframes
            m_editor->applyReframe(job.sequenceId, job.clipId, samples);
        }
        trackNext();
    });
    connect(m_trackProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        if (m_trackProcess)
            m_trackProcess->deleteLater();
        m_trackProcess = nullptr;
        m_trackQueue.clear();
        m_editor->setBusy(QString());
        Q_EMIT trackingChanged();
    });
    m_trackProcess->start(QStringLiteral("python3"), Reframe::detectorArguments(script, job.path, job.startSec, job.durationSec, 4));
    Q_EMIT trackingChanged();
}

// ------------------------------------------------------------------ export

void ShortsGenerator::setExportFolder(const QString &folder)
{
    QString f = folder;
    if (f.startsWith(QLatin1String("file:")))
        f = QUrl(f).toLocalFile();
    if (f.isEmpty() || f == m_exportFolder)
        return;
    m_exportFolder = f;
    QSettings().setValue(QStringLiteral("shorts/exportFolder"), f);
    Q_EMIT exportChanged();
}

QUrl ShortsGenerator::exportFolderUrl() const
{
    return QUrl::fromLocalFile(m_exportFolder);
}

void ShortsGenerator::setCaptionOutput(const QString &mode)
{
    static const QStringList modes = {QStringLiteral("video"), QStringLiteral("video+srt"), QStringLiteral("srt")};
    if (!modes.contains(mode) || mode == m_captionOutput)
        return;
    m_captionOutput = mode;
    Q_EMIT exportChanged();
}

void ShortsGenerator::exportCuts(const QUrl &folder, const QString &captions)
{
    if (!folder.isEmpty())
        setExportFolder(folder.isLocalFile() ? folder.toLocalFile() : folder.toString());
    m_pendingCaptions = captions.isEmpty() ? m_captionOutput : captions;
    m_exportOnly.clear();
    // nothing generated yet: the selected cuts first
    if (m_editor->shortSequences().isEmpty() && createCuts() == 0) {
        m_editor->setStatus(tr("There are no generated cuts to export"));
        return;
    }
    m_exportPending = true;
    m_exports.clear();
    Q_EMIT exportChanged();
    if (!tracking())
        startExport();
}

void ShortsGenerator::startExport()
{
    m_exportPending = false;
    m_exports.clear();
    for (const Editor::ShortExport &e : m_editor->exportShorts(QUrl::fromLocalFile(m_exportFolder), m_pendingCaptions, m_exportOnly))
        m_exports.append({e.sequenceId, e.platform, e.output});
    m_exportOnly.clear();
    // the choice holds for this export only: the next one burns them in again
    m_captionOutput = QStringLiteral("video");
    syncExports();
    Q_EMIT exportChanged();
}

void ShortsGenerator::syncExports()
{
    if (m_exports.isEmpty())
        return;
    auto *queue = static_cast<RenderQueue *>(m_editor->renderQueueObject());
    QHash<QString, int> rows;
    for (int r = 0; r < queue->rowCount(); ++r)
        rows.insert(queue->data(queue->index(r), RenderQueue::OutputRole).toString(), r); // the latest wins
    for (ExportJob &j : m_exports) {
        const auto it = rows.constFind(j.output);
        if (it == rows.constEnd())
            continue; // removed from the queue: keep what was last seen
        j.state = queue->data(queue->index(*it), RenderQueue::StateRole).toString();
        j.progress = j.state == QLatin1String("done") ? 100 : queue->data(queue->index(*it), RenderQueue::ProgressRole).toInt();
    }
    Q_EMIT exportChanged();
}

static QString overallState(const QStringList &states)
{
    if (states.isEmpty())
        return {};
    if (states.contains(QLatin1String("running")))
        return QStringLiteral("running");
    if (states.contains(QLatin1String("queued")))
        return QStringLiteral("queued");
    if (states.contains(QLatin1String("failed")))
        return QStringLiteral("failed");
    if (states.contains(QLatin1String("cancelled")))
        return QStringLiteral("cancelled");
    return QStringLiteral("done");
}

QVariantList ShortsGenerator::cuts() const
{
    QVariantList list;
    for (const QVariant &v : m_editor->shortSequences()) {
        QVariantMap m = v.toMap();
        const QString id = m.value(QStringLiteral("id")).toString();
        QVariantList files;
        QStringList states;
        int progress = 0;
        for (const ExportJob &j : m_exports) {
            if (j.sequenceId != id)
                continue;
            files.append(QVariantMap{{QStringLiteral("platform"), j.platform},
                                     {QStringLiteral("name"), Platforms::find(j.platform).name},
                                     {QStringLiteral("output"), j.output},
                                     {QStringLiteral("file"), QFileInfo(j.output).fileName()},
                                     {QStringLiteral("state"), j.state},
                                     {QStringLiteral("progress"), j.progress}});
            states << j.state;
            progress += j.progress;
        }
        QStringList platformNames;
        for (const QString &pid : m.value(QStringLiteral("platforms")).toStringList())
            platformNames << Platforms::find(pid).name;
        m.insert(QStringLiteral("platformNames"), platformNames.join(QStringLiteral(", ")));
        m.insert(QStringLiteral("length"), m_editor->formatDuration(m.value(QStringLiteral("duration")).toInt()));
        m.insert(QStringLiteral("files"), files);
        const bool reframing = id == m_trackingSequence || std::any_of(m_trackQueue.cbegin(), m_trackQueue.cend(), [&](const TrackJob &j) { return j.sequenceId == id; });
        QString state = overallState(states);
        if (m_exportPending && (m_exportOnly.isEmpty() || m_exportOnly.contains(id)))
            state = QStringLiteral("queued");
        if (state.isEmpty())
            state = reframing ? QStringLiteral("reframing") : QStringLiteral("ready");
        m.insert(QStringLiteral("state"), state);
        m.insert(QStringLiteral("reframing"), reframing);
        const double fps = m_editor->fps();
        const qint64 s0 = m.value(QStringLiteral("startMs")).toLongLong(), s1 = m.value(QStringLiteral("endMs")).toLongLong();
        m.insert(QStringLiteral("thumbnail"), QStringLiteral("image://thumb/%1/%2").arg(m.value(QStringLiteral("mediaId")).toString())
                                                  .arg(int(std::lround((s0 + std::min<qint64>(1500, (s1 - s0) / 3)) * fps / 1000.0))));
        m.insert(QStringLiteral("range"), formatTime(s0) + QStringLiteral(" – ") + formatTime(s1));
        m.insert(QStringLiteral("progress"), files.isEmpty() ? 0.0 : progress / (100.0 * files.size()));
        list.append(m);
    }
    return list;
}

bool ShortsGenerator::exporting() const
{
    if (m_exportPending)
        return true;
    for (const ExportJob &j : m_exports)
        if (j.state == QLatin1String("queued") || j.state == QLatin1String("running"))
            return true;
    return false;
}

bool ShortsGenerator::exportDone() const
{
    return !m_exports.isEmpty() && !exporting();
}

int ShortsGenerator::exportFailed() const
{
    int n = 0;
    for (const ExportJob &j : m_exports)
        n += j.state == QLatin1String("failed") || j.state == QLatin1String("cancelled");
    return n;
}

double ShortsGenerator::exportProgress() const
{
    if (m_exports.isEmpty())
        return 0;
    double sum = 0;
    for (const ExportJob &j : m_exports)
        sum += j.progress;
    return sum / (100.0 * m_exports.size());
}

QString ShortsGenerator::exportSummary() const
{
    if (m_exportPending)
        return tr("Waiting for the face tracking to finish…");
    if (m_exports.isEmpty())
        return {};
    int done = 0;
    for (const ExportJob &j : m_exports)
        done += j.state == QLatin1String("done");
    const int total = int(m_exports.size());
    if (exporting())
        return tr("Exporting %1 of %2 files · %3%").arg(std::min(total, done + 1)).arg(total).arg(int(exportProgress() * 100));
    if (exportFailed() > 0)
        return tr("%1 of %2 files exported; %3 failed (details in Deliver › Render Queue)").arg(done).arg(total).arg(exportFailed());
    return tr("%n file(s) exported to %1", nullptr, total).arg(QDir::toNativeSeparators(m_exportFolder));
}

void ShortsGenerator::exportCut(const QString &sequenceId, const QString &captions)
{
    if (exporting())
        return;
    m_pendingCaptions = captions.isEmpty() ? QStringLiteral("video") : captions;
    m_exportOnly = {sequenceId};
    m_exportPending = true;
    m_exports.clear();
    Q_EMIT exportChanged();
    if (!tracking())
        startExport();
}

void ShortsGenerator::previewCut(const QString &sequenceId)
{
    const QVariantMap info = [&] {
        for (const QVariant &v : m_editor->shortSequences())
            if (v.toMap().value(QStringLiteral("id")).toString() == sequenceId)
                return v.toMap();
        return QVariantMap();
    }();
    const MediaItem *m = m_editor->mediaItem(info.value(QStringLiteral("mediaId")).toString());
    if (!m)
        return;
    const qint64 startMs = info.value(QStringLiteral("startMs")).toLongLong(), endMs = info.value(QStringLiteral("endMs")).toLongLong();
    // the cut's own caption track (as edited), shifted to the source's time
    QString file;
    QSize size = m_previewSize;
    for (const Sequence &s : m_editor->document().sequences) {
        if (s.id != sequenceId)
            continue;
        const Platform platform = Platforms::primary(info.value(QStringLiteral("platforms")).toStringList());
        if (platform.valid())
            size = QSize(platform.width, platform.height);
        for (const Track &t : s.timeline.tracks) {
            if (!Captions::trackHasCaptions(t) || t.hidden)
                continue;
            const Captions::Style style = Captions::Style::fromMap(t.captionStyle);
            const double fps = m_editor->fps();
            QList<Captions::Cue> cues = Captions::trackCues(t, fps, style, size.width(), size.height());
            for (Captions::Cue &cue : cues) {
                cue.startMs += startMs;
                cue.endMs += startMs;
                for (Captions::Word &w : cue.words) {
                    w.startMs += startMs;
                    w.endMs += startMs;
                }
            }
            if (!cues.isEmpty())
                file = Editor::cacheCaptions(Captions::toAss(cues, style, size.width(), size.height(), fps));
            break;
        }
    }
    Player *p = m_editor->player();
    if (p->sourceMediaId() != m->id)
        p->openSource(m->id, m->path, m->length);
    m_previewAss = file;
    m_previewSize = size;
    Q_EMIT previewChanged();
    const int in = int(std::lround(startMs * m_editor->fps() / 1000.0));
    p->setMode(QStringLiteral("source"));
    p->setSourceIn(in);
    p->setSourceOut(int(std::lround(endMs * m_editor->fps() / 1000.0)));
    p->seek(in);
    p->play();
}

void ShortsGenerator::openExportFolder() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_exportFolder));
}

bool ShortsGenerator::editCut(const QString &sequenceId)
{
    stopPreview();
    m_editor->leaveCompound(0);
    for (const Track &t : m_editor->currentTimeline().tracks)
        for (const Clip &c : t.clips)
            if (c.kind == QLatin1String("compound") && c.mediaId == sequenceId)
                return m_editor->openCompound(c.id);
    return false;
}
