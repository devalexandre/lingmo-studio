/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "mediabin.h"
#include "editor.h"
#include "mediaprobe.h"
#include "player.h"

#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QtConcurrent>

using namespace Studio;

MediaBin::MediaBin(Editor *editor)
    : QAbstractListModel(editor)
    , m_editor(editor)
{
}

MediaBin::~MediaBin()
{
    if (m_proxyProcess) {
        m_proxyProcess->disconnect(this);
        m_proxyProcess->kill();
        m_proxyProcess->waitForFinished(2000);
    }
}

void MediaBin::reset()
{
    beginResetModel();
    endResetModel();
    Q_EMIT countChanged();
}

int MediaBin::count() const
{
    return m_editor->media().size();
}

int MediaBin::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

bool MediaBin::isUsed(const QString &mediaId) const
{
    auto inTimeline = [&](const Timeline &tl) {
        for (const Track &t : tl.tracks)
            for (const Clip &c : t.clips)
                if (c.kind == QLatin1String("media") && c.mediaId == mediaId)
                    return true;
        return false;
    };
    if (inTimeline(m_editor->document().timeline))
        return true;
    for (const Sequence &s : m_editor->document().sequences)
        if (inTimeline(s.timeline))
            return true;
    return false;
}

QVariant MediaBin::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= count())
        return {};
    const MediaItem &m = m_editor->media().at(index.row());
    switch (role) {
    case IdRole:
        return m.id;
    case NameRole:
        return m.name;
    case PathRole:
        return m.path;
    case KindRole:
        return m.kind;
    case LengthRole:
        return m.length;
    case DurationTextRole:
        return m.kind == QLatin1String("image") ? QString() : m_editor->formatDuration(m.length);
    case InfoRole: {
        QStringList parts;
        if (m.width > 0)
            parts << QStringLiteral("%1×%2").arg(m.width).arg(m.height);
        if (m.fps > 0)
            parts << QStringLiteral("%1 fps").arg(QString::number(m.fps, 'g', 4));
        if (!m.videoCodec.isEmpty())
            parts << m.videoCodec.toUpper();
        if (!m.audioCodec.isEmpty())
            parts << m.audioCodec.toUpper() + (m.audioChannels ? QStringLiteral(" %1ch").arg(m.audioChannels) : QString());
        return parts.join(QStringLiteral(" · "));
    }
    case ProxyStateRole:
        if (m_proxyRunning == m.id || m_proxyQueue.contains(m.id))
            return QStringLiteral("running");
        return !m.proxyPath.isEmpty() && QFile::exists(m.proxyPath) ? QStringLiteral("ready") : QStringLiteral("none");
    case ProxyProgressRole:
        return m_proxyProgress.value(m.id);
    case HasAudioRole:
        return m.hasAudio;
    case HasVideoRole:
        return m.hasVideo;
    case TranscriptRole:
        return !m.transcript.isEmpty();
    case UsedRole:
        return isUsed(m.id);
    }
    return {};
}

QHash<int, QByteArray> MediaBin::roleNames() const
{
    return {{IdRole, "mediaId"},           {NameRole, "name"},     {PathRole, "path"},
            {KindRole, "kind"},            {LengthRole, "length"}, {DurationTextRole, "durationText"},
            {InfoRole, "info"},            {ProxyStateRole, "proxyState"}, {ProxyProgressRole, "proxyProgress"},
            {HasAudioRole, "hasAudio"},    {HasVideoRole, "hasVideo"}, {TranscriptRole, "hasTranscript"},
            {UsedRole, "used"}};
}

QString MediaBin::idAt(int row) const
{
    return row >= 0 && row < count() ? m_editor->media().at(row).id : QString();
}

void MediaBin::importFiles(const QStringList &paths, const Placed &placed)
{
    const ProjectSettings settings = m_editor->settings();
    for (const QString &path : paths) {
        // Already in the bin: just place it again
        bool known = false;
        for (const MediaItem &m : m_editor->media()) {
            if (m.path == QFileInfo(path).absoluteFilePath()) {
                known = true;
                if (placed)
                    placed(m);
            }
        }
        if (known)
            continue;
        ++m_importing;
        Q_EMIT importingChanged();
        auto *watcher = new QFutureWatcher<QPair<bool, MediaItem>>(this);
        connect(watcher, &QFutureWatcher<QPair<bool, MediaItem>>::finished, this, [this, watcher, path, placed] {
            const auto result = watcher->result();
            watcher->deleteLater();
            --m_importing;
            Q_EMIT importingChanged();
            if (!result.first) {
                Q_EMIT importFailed(QFileInfo(path).fileName(), result.second.name);
                return;
            }
            const int row = count();
            beginInsertRows({}, row, row);
            m_editor->addMedia(result.second);
            endInsertRows();
            Q_EMIT countChanged();
            if (placed)
                placed(result.second);
            if (m_editor->autoProxy() && result.second.needsProxy())
                generateProxy(result.second.id);
        });
        watcher->setFuture(QtConcurrent::run([path, settings] {
            MediaItem m;
            m.id = newId(QStringLiteral("m"));
            QString error;
            if (!MediaProbe::probe(path, settings, &m, &error)) {
                m.name = error;
                return QPair<bool, MediaItem>(false, m);
            }
            return QPair<bool, MediaItem>(true, m);
        }));
    }
}

void MediaBin::changedRow(const QString &id)
{
    for (int i = 0; i < count(); ++i)
        if (m_editor->media().at(i).id == id)
            Q_EMIT dataChanged(index(i), index(i));
}

QString MediaBin::proxyDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/proxies");
    QDir().mkpath(dir);
    return dir;
}

void MediaBin::generateProxy(const QString &mediaId)
{
    const MediaItem *m = m_editor->mediaItem(mediaId);
    if (!m || m->kind != QLatin1String("video") || m_proxyQueue.contains(mediaId) || m_proxyRunning == mediaId)
        return;
    m_proxyQueue.append(mediaId);
    m_proxyProgress.insert(mediaId, 0.0);
    changedRow(mediaId);
    startNextProxy();
}

void MediaBin::startNextProxy()
{
    if (!m_proxyRunning.isEmpty() || m_proxyQueue.isEmpty())
        return;
    m_proxyRunning = m_proxyQueue.takeFirst();
    const MediaItem *m = m_editor->mediaItem(m_proxyRunning);
    if (!m) {
        m_proxyRunning.clear();
        startNextProxy();
        return;
    }
    const QString out = MediaProbe::proxyPathFor(m->path, proxyDir());
    const QString partial = out + QStringLiteral(".part.mp4");
    const double totalUs = m->length / std::max(1.0, m_editor->fps()) * 1e6;
    auto *p = new QProcess(this);
    m_proxyProcess = p;
    const QString id = m_proxyRunning;
    connect(p, &QProcess::readyReadStandardOutput, this, [this, p, id, totalUs] {
        while (p->canReadLine()) {
            const QByteArray line = p->readLine().trimmed();
            if (line.startsWith("out_time_us=")) {
                const double us = line.mid(12).toDouble();
                m_proxyProgress.insert(id, std::clamp(us / totalUs, 0.0, 1.0));
                changedRow(id);
            }
        }
    });
    connect(p, &QProcess::finished, this, [this, p, id, out, partial](int code, QProcess::ExitStatus status) {
        p->deleteLater();
        m_proxyRunning.clear();
        m_proxyProgress.remove(id);
        if (status == QProcess::NormalExit && code == 0 && QFile::exists(partial)) {
            QFile::remove(out);
            QFile::rename(partial, out);
            if (const MediaItem *m = m_editor->mediaItem(id)) {
                MediaItem copy = *m;
                copy.proxyPath = out;
                m_editor->updateMedia(copy);
            }
            m_editor->setStatus(tr("Proxy ready"));
        } else {
            QFile::remove(partial);
        }
        changedRow(id);
        startNextProxy();
    });
    p->start(QStringLiteral("ffmpeg"), MediaProbe::proxyArguments(m->path, partial));
    changedRow(id);
}

void MediaBin::cancelProxy(const QString &mediaId)
{
    m_proxyQueue.removeAll(mediaId);
    if (m_proxyRunning == mediaId && m_proxyProcess)
        m_proxyProcess->kill();
    m_proxyProgress.remove(mediaId);
    changedRow(mediaId);
}

void MediaBin::deleteProxy(const QString &mediaId)
{
    cancelProxy(mediaId);
    if (const MediaItem *m = m_editor->mediaItem(mediaId)) {
        MediaItem copy = *m;
        QFile::remove(copy.proxyPath);
        copy.proxyPath.clear();
        m_editor->updateMedia(copy);
        changedRow(mediaId);
    }
}

bool MediaBin::remove(const QString &mediaId)
{
    if (isUsed(mediaId)) {
        m_editor->setStatus(tr("The clip is used in the timeline"));
        return false;
    }
    cancelProxy(mediaId);
    for (int i = 0; i < count(); ++i) {
        if (m_editor->media().at(i).id == mediaId) {
            beginRemoveRows({}, i, i);
            m_editor->removeMediaItem(mediaId);
            endRemoveRows();
            Q_EMIT countChanged();
            return true;
        }
    }
    return false;
}

void MediaBin::openInSource(const QString &mediaId)
{
    const MediaItem *m = m_editor->mediaItem(mediaId);
    if (!m)
        return;
    const QString path = m_editor->useProxies() && !m->proxyPath.isEmpty() && QFile::exists(m->proxyPath) ? m->proxyPath : m->path;
    m_editor->player()->openSource(m->id, path, m->length);
}
