/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "metadatagenerator.h"
#include "editor.h"
#include "thumbnail.h"
#include "transcriber.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QClipboard>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

using namespace Studio;

namespace {
constexpr int CandidateCount = 14; // frames looked at
constexpr int KeptCount = 6;       // frames offered

QUrl ollamaUrl(const QString &path)
{
    QString host = qEnvironmentVariable("OLLAMA_HOST", QStringLiteral("127.0.0.1:11434"));
    if (!host.contains(QLatin1String("://")))
        host.prepend(QLatin1String("http://"));
    QUrl u(host);
    u.setPath(path);
    return u;
}
} // namespace

MetadataGenerator::MetadataGenerator(Editor *editor)
    : QObject(editor)
    , m_editor(editor)
    , m_network(new QNetworkAccessManager(this))
{
    m_composeTimer.setSingleShot(true);
    m_composeTimer.setInterval(150);
    connect(&m_composeTimer, &QTimer::timeout, this, &MetadataGenerator::composeNow);

    Transcriber *t = m_editor->transcriber();
    connect(t, &Transcriber::finished, this, [this](const QString &task) {
        if (task != QLatin1String("metadata") || !m_waitingTranscript)
            return;
        m_waitingTranscript = false;
        const Captions::Transcript result = m_editor->transcriber()->result();
        m_language = result.language;
        m_editor->rememberWords(m_mediaId, result.words);
        m_input = m_editor->metadataInput();
        if (m_input.transcript.isEmpty())
            m_notice = tr("No speech was found in the video: the text uses the project name and what is on screen.");
        askModel();
    });
    connect(t, &Transcriber::failed, this, [this](const QString &task, const QString &message) {
        if (task != QLatin1String("metadata") || !m_waitingTranscript)
            return;
        m_waitingTranscript = false;
        m_notice = tr("The speech could not be transcribed (%1): the text uses the project name and what is on screen.").arg(message);
        askModel();
    });
    connect(t, &Transcriber::progressChanged, this, [this] {
        if (m_running && m_waitingTranscript)
            setStage(m_editor->transcriber()->stage(), std::max(0.0, m_editor->transcriber()->progress()) * 0.5);
    });
}

// ------------------------------------------------------------------ properties

void MetadataGenerator::setTitle(const QString &t)
{
    if (t == m_title)
        return;
    m_title = t;
    Q_EMIT resultChanged();
}

void MetadataGenerator::setDescription(const QString &d)
{
    if (d == m_description)
        return;
    m_description = d;
    Q_EMIT resultChanged();
}

void MetadataGenerator::setTags(const QString &t)
{
    if (t == m_tags)
        return;
    m_tags = t;
    Q_EMIT resultChanged();
}

void MetadataGenerator::setThumbnailText(const QString &t)
{
    if (t == m_thumbText)
        return;
    m_thumbText = t;
    Q_EMIT thumbnailChanged();
    scheduleCompose();
}

void MetadataGenerator::setThumbnailStyle(const QString &s)
{
    if (s == m_style || !Thumbnail::styleIds().contains(s))
        return;
    m_style = s;
    Q_EMIT thumbnailChanged();
    scheduleCompose();
}

QVariantList MetadataGenerator::styles() const
{
    return {QVariantMap{{QStringLiteral("id"), QStringLiteral("bold")}, {QStringLiteral("name"), tr("Bold")}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("banner")}, {QStringLiteral("name"), tr("Banner")}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("center")}, {QStringLiteral("name"), tr("Centered")}}};
}

void MetadataGenerator::setSelectedFrame(int i)
{
    if (i == m_selected || i < 0 || i >= m_images.size())
        return;
    m_selected = i;
    Q_EMIT thumbnailChanged();
    scheduleCompose();
}

QString MetadataGenerator::thumbnail() const
{
    return m_composedPath.isEmpty() ? QString() : QUrl::fromLocalFile(m_composedPath).toString();
}

// ------------------------------------------------------------------ run

QString MetadataGenerator::workDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/metadata");
}

void MetadataGenerator::setStage(const QString &stage, double progress)
{
    m_stage = stage;
    m_progress = progress;
    Q_EMIT stateChanged();
}

QString MetadataGenerator::resolveLanguage() const
{
    QString lang = m_language;
    if (lang.isEmpty() || lang == QLatin1String("auto"))
        lang = m_editor->transcriber()->language();
    if ((lang.isEmpty() || lang == QLatin1String("auto")) && m_input.transcript.isEmpty() && QLocale().name().startsWith(QLatin1String("pt")))
        lang = QStringLiteral("pt");
    return lang == QLatin1String("auto") ? QString() : lang;
}

void MetadataGenerator::generate()
{
    if (m_running)
        return;
    if (m_editor->document().timeline.duration() < 2) {
        fail(tr("Put something on the timeline first."));
        return;
    }
    ++m_session;
    QDir(workDir()).removeRecursively();
    QDir().mkpath(workDir());
    m_composedPath.clear();
    m_composed = QImage();
    m_images.clear();
    m_frames.clear();
    m_framePositions.clear();
    m_selected = 0;
    m_error.clear();
    m_notice.clear();
    m_language.clear();
    m_running = true;
    Q_EMIT framesChanged();
    Q_EMIT thumbnailChanged();
    setStage(tr("Reading what is said in the video…"), 0.02);

    m_input = m_editor->metadataInput();
    if (m_input.transcript.isEmpty()) {
        QString path;
        qint64 start = 0, length = 0;
        if (m_editor->speechSource(&m_mediaId, &path, &start, &length)) {
            Transcriber *t = m_editor->transcriber();
            t->refresh();
            if (t->running()) {
                m_notice = tr("The transcriber is busy with another task: the text uses the project name and what is on screen.");
            } else if (!t->available()) {
                m_notice = t->problem();
            } else {
                m_waitingTranscript = true;
                setStage(tr("Transcribing the speech…"), 0.02);
                t->transcribe(path, start, length, QStringLiteral("metadata"));
                return;
            }
        } else {
            m_notice = tr("Nothing with speech on the timeline: the text uses the project name and what is on screen.");
        }
    }
    askModel();
}

void MetadataGenerator::cancel()
{
    if (!m_running)
        return;
    ++m_session;
    m_running = false;
    if (m_waitingTranscript) {
        m_waitingTranscript = false;
        m_editor->transcriber()->cancel();
    }
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
    m_error = tr("Cancelled");
    setStage(QString(), -1);
}

void MetadataGenerator::fail(const QString &message)
{
    m_running = false;
    m_error = message;
    setStage(QString(), -1);
    m_editor->setStatus(message);
}

void MetadataGenerator::askModel()
{
    m_input.language = resolveLanguage();

    // Settings › AI: the provider and model the whole system uses
    QSettings ai(QStringLiteral("lingmoos"), QStringLiteral("ai"));
    auto value = [&ai](const char *key) {
        const QString plain = ai.value(QLatin1String(key)).toString();
        return plain.isEmpty() ? ai.value(QStringLiteral("ai/") + QLatin1String(key)).toString() : plain;
    };
    const QString provider = value("provider");
    const QString model = value("model");
    if ((!provider.isEmpty() && provider != QLatin1String("ollama")) || model.isEmpty()) {
        fallbackText(tr("No Ollama model chosen in Settings › AI: this is a simple version made from the transcript."));
        return;
    }
    m_llmModel = model;
    setStage(tr("Writing the title and description with %1…").arg(model), 0.5);

    QNetworkRequest req(ollamaUrl(QStringLiteral("/api/chat")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(180 * 1000);
    const int session = m_session;
    m_reply = m_network->post(req, Metadata::llmRequest(model, m_input));
    connect(m_reply, &QNetworkReply::finished, this, [this, session, model] {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        if (!r)
            return;
        r->deleteLater();
        if (session != m_session || !m_running)
            return;
        if (r->error() != QNetworkReply::NoError) {
            fallbackText(tr("The AI model %1 did not answer (%2): this is a simple version made from the transcript.").arg(model, r->errorString()));
            return;
        }
        const Metadata::Result res = Metadata::parseLlmReply(r->readAll());
        if (!res.ok) {
            fallbackText(tr("The AI model %1 gave an answer that could not be read: this is a simple version made from the transcript.").arg(model));
            return;
        }
        applyText(res, true);
    });
}

void MetadataGenerator::fallbackText(const QString &why)
{
    if (!why.isEmpty())
        m_notice = m_notice.isEmpty() ? why : m_notice + QLatin1Char(' ') + why;
    m_llmModel.clear();
    applyText(Metadata::heuristic(m_input), false);
}

void MetadataGenerator::applyText(Metadata::Result r, bool fromLlm)
{
    const Metadata::Result plain = Metadata::heuristic(m_input);
    if (r.description.isEmpty())
        r.description = plain.description;
    if (r.tags.isEmpty())
        r.tags = plain.tags;
    if (r.thumbnailText.isEmpty())
        r.thumbnailText = plain.thumbnailText;
    m_usedLlm = fromLlm;
    m_title = r.title.isEmpty() ? m_editor->projectName() : r.title;
    m_description = Metadata::composeDescription(r, m_input.chapters, tr("Intro"));
    m_tags = r.tags.join(QStringLiteral(", "));
    m_thumbText = r.thumbnailText;
    m_hasResult = true;
    Q_EMIT resultChanged();
    Q_EMIT thumbnailChanged();
    startFrames();
}

// ------------------------------------------------------------------ frames

void MetadataGenerator::startFrames()
{
    const int total = m_editor->document().timeline.duration();
    m_positions.clear();
    for (int i = 0; i < CandidateCount; ++i) {
        const int frame = std::clamp(int(total * (0.05 + 0.90 * (i + 0.5) / CandidateCount)), 0, std::max(0, total - 1));
        if (!m_positions.contains(frame))
            m_positions << frame;
    }
    m_images.clear();
    m_scores.clear();
    m_index = 0;
    setStage(tr("Choosing frames for the thumbnail…"), 0.6);
    const int session = m_session;
    QTimer::singleShot(0, this, [this, session] {
        if (session == m_session)
            nextFrame();
    });
}

void MetadataGenerator::nextFrame()
{
    if (!m_running)
        return;
    if (m_index >= m_positions.size()) {
        finishFrames();
        return;
    }
    const QImage img = m_editor->stillAt(m_positions.at(m_index), 1280);
    m_images << img;
    m_scores << Thumbnail::frameScore(img);
    ++m_index;
    setStage(tr("Choosing frames for the thumbnail…"), 0.6 + 0.38 * m_index / m_positions.size());
    const int session = m_session;
    QTimer::singleShot(0, this, [this, session] {
        if (session == m_session)
            nextFrame();
    });
}

void MetadataGenerator::finishFrames()
{
    const bool any = std::any_of(m_images.cbegin(), m_images.cend(), [](const QImage &i) { return !i.isNull(); });
    if (!any) {
        m_images.clear();
        m_notice += (m_notice.isEmpty() ? QString() : QStringLiteral(" ")) + tr("No frame could be read from the timeline, so there is no thumbnail.");
        finish();
        return;
    }
    const QList<int> picks = Thumbnail::pickDiverse(m_scores, KeptCount, 2);
    const double fps = std::max(1.0, m_editor->fps());
    QList<QImage> kept;
    m_frames.clear();
    m_framePositions.clear();
    for (int k = 0; k < picks.size(); ++k) {
        const int i = picks.at(k);
        if (m_images.at(i).isNull())
            continue;
        const QString path = QDir(workDir()).filePath(QStringLiteral("frame-%1.jpg").arg(kept.size()));
        m_images.at(i).scaledToWidth(std::min(480, m_images.at(i).width()), Qt::SmoothTransformation).save(path, "JPG", 85);
        m_frames.append(QVariantMap{{QStringLiteral("url"), QUrl::fromLocalFile(path).toString()},
                                    {QStringLiteral("frame"), m_positions.at(i)},
                                    {QStringLiteral("time"), Metadata::timestamp(int(std::lround(m_positions.at(i) / fps)))}});
        m_framePositions << m_positions.at(i);
        kept << m_images.at(i);
    }
    m_images = kept;
    m_selected = 0;
    Q_EMIT framesChanged();
    composeNow();
    finish();
}

void MetadataGenerator::finish()
{
    m_running = false;
    setStage(QString(), 1);
    m_editor->setStatus(tr("Title, description and thumbnail are ready (Export page)"));
}

// ------------------------------------------------------------------ thumbnail

void MetadataGenerator::scheduleCompose()
{
    if (!m_images.isEmpty())
        m_composeTimer.start();
}

void MetadataGenerator::composeNow()
{
    m_composeTimer.stop();
    if (m_images.isEmpty() || m_selected >= m_images.size())
        return;
    Thumbnail::Options o;
    o.text = m_thumbText;
    o.style = m_style;
    const bool vertical = m_editor->projectHeight() > m_editor->projectWidth();
    o.size = vertical ? QSize(720, 1280) : QSize(1280, 720);
    m_composed = Thumbnail::compose(m_images.at(m_selected), o);
    const QString old = m_composedPath;
    m_composedPath = QDir(workDir()).filePath(QStringLiteral("thumbnail-%1.jpg").arg(++m_serial));
    QDir().mkpath(workDir());
    if (!m_composed.save(m_composedPath, "JPG", 90))
        m_composedPath.clear();
    if (!old.isEmpty())
        QFile::remove(old);
    Q_EMIT thumbnailChanged();
}

QString MetadataGenerator::baseName() const
{
    return QFileInfo(m_editor->suggestedExportPath(QStringLiteral("mp4-h264"))).completeBaseName();
}

QString MetadataGenerator::suggestedThumbnailPath() const
{
    const QFileInfo video(m_editor->suggestedExportPath(QStringLiteral("mp4-h264")));
    return video.dir().filePath(baseName() + QStringLiteral("-thumbnail.jpg"));
}

QString MetadataGenerator::suggestedTextPath() const
{
    const QFileInfo video(m_editor->suggestedExportPath(QStringLiteral("mp4-h264")));
    return video.dir().filePath(baseName() + QStringLiteral("-description.txt"));
}

bool MetadataGenerator::saveThumbnail(const QUrl &file)
{
    if (m_composed.isNull())
        return false;
    QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".jpg");
    const bool png = QFileInfo(path).suffix().compare(QLatin1String("png"), Qt::CaseInsensitive) == 0;
    bool ok = false;
    if (png) {
        ok = m_composed.save(path, "PNG");
    } else {
        // YouTube takes thumbnails up to 2 MB: lower the quality until it fits
        for (int quality : {95, 90, 82, 72, 60}) {
            ok = m_composed.save(path, "JPG", quality);
            if (!ok || QFileInfo(path).size() < 2 * 1024 * 1024)
                break;
        }
    }
    m_editor->setStatus(ok ? tr("Thumbnail saved to %1").arg(path) : tr("Could not save the thumbnail to %1").arg(path));
    return ok;
}

bool MetadataGenerator::saveTexts(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path.isEmpty())
        return false;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_editor->setStatus(tr("Could not save %1").arg(path));
        return false;
    }
    QString text = m_title + QStringLiteral("\n\n") + m_description;
    if (!m_tags.trimmed().isEmpty())
        text += QStringLiteral("\n\n") + tr("Tags: %1").arg(m_tags);
    f.write(text.toUtf8());
    f.write("\n");
    const bool ok = f.commit();
    m_editor->setStatus(ok ? tr("Saved to %1").arg(path) : tr("Could not save %1").arg(path));
    return ok;
}

void MetadataGenerator::copyToClipboard(const QString &text) const
{
    if (QClipboard *c = QGuiApplication::clipboard())
        c->setText(text);
    m_editor->setStatus(tr("Copied"));
}
