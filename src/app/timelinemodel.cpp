/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "timelinemodel.h"
#include "editor.h"

using namespace Studio;

TimelineModel::TimelineModel(Editor *editor)
    : QAbstractListModel(editor)
    , m_editor(editor)
{
}

static QString kindName(TrackKind k)
{
    return k == TrackKind::Audio ? QStringLiteral("audio") : k == TrackKind::Subtitle ? QStringLiteral("subtitle") : QStringLiteral("video");
}

QVariantList TimelineModel::clipList(const Track &t) const
{
    QVariantList list;
    for (const Clip &c : t.clips) {
        QString name = c.name;
        QString mediaKind;
        bool hasAudio = false;
        if (c.kind == QLatin1String("media")) {
            if (const MediaItem *m = m_editor->mediaItem(c.mediaId)) {
                if (name.isEmpty())
                    name = m->name;
                mediaKind = m->kind;
                hasAudio = m->hasAudio;
            }
        } else if (c.kind == QLatin1String("title")) {
            for (const Effect &e : c.effects)
                if (e.type == QLatin1String("text") && name.isEmpty())
                    name = e.params.value(QStringLiteral("text")).toString().simplified();
            mediaKind = QStringLiteral("title");
        } else if (c.kind == QLatin1String("color")) {
            mediaKind = QStringLiteral("color");
            if (name.isEmpty())
                name = c.genParams.value(QStringLiteral("color")).toString();
        } else if (c.kind == QLatin1String("compound")) {
            mediaKind = QStringLiteral("compound");
            for (const Sequence &s : m_editor->document().sequences)
                if (s.id == c.mediaId && name.isEmpty())
                    name = s.name;
        }
        QVariantMap m{{QStringLiteral("id"), c.id},
                      {QStringLiteral("kind"), c.kind},
                      {QStringLiteral("mediaKind"), mediaKind},
                      {QStringLiteral("mediaId"), c.mediaId},
                      {QStringLiteral("name"), name},
                      {QStringLiteral("position"), c.position},
                      {QStringLiteral("duration"), c.duration()},
                      {QStringLiteral("in"), c.in},
                      {QStringLiteral("out"), c.out},
                      {QStringLiteral("speed"), c.speedRamp.isEmpty() ? c.speed : 0.0},
                      {QStringLiteral("ramp"), !c.speedRamp.isEmpty()},
                      {QStringLiteral("fadeIn"), c.fadeIn},
                      {QStringLiteral("fadeOut"), c.fadeOut},
                      {QStringLiteral("transitionIn"), c.transitionIn},
                      {QStringLiteral("transition"), c.transition},
                      {QStringLiteral("effects"), c.effects.size()},
                      {QStringLiteral("audioMuted"), c.audioMuted},
                      {QStringLiteral("hasAudio"), hasAudio && !c.audioMuted},
                      {QStringLiteral("linked"), !c.linkId.isEmpty()},
                      {QStringLiteral("freeze"), c.freezeFrame >= 0},
                      {QStringLiteral("color"), c.genParams.value(QStringLiteral("color"))}};
        list.append(m);
    }
    return list;
}

void TimelineModel::setTimeline(const Timeline &timeline)
{
    bool sameTracks = timeline.tracks.size() == m_timeline.tracks.size();
    for (int i = 0; sameTracks && i < timeline.tracks.size(); ++i)
        sameTracks = timeline.tracks.at(i).id == m_timeline.tracks.at(i).id;
    if (!sameTracks) {
        beginResetModel();
        m_timeline = timeline;
        m_clipCache.clear();
        for (const Track &t : m_timeline.tracks)
            m_clipCache.append(clipList(t));
        endResetModel();
    } else {
        const Timeline old = m_timeline;
        m_timeline = timeline;
        for (int i = 0; i < timeline.tracks.size(); ++i) {
            const QVariantList clips = clipList(timeline.tracks.at(i));
            if (!(old.tracks.at(i) == timeline.tracks.at(i)) || clips != m_clipCache.at(i)) {
                m_clipCache[i] = clips;
                Q_EMIT dataChanged(index(i), index(i));
            }
        }
    }
    Q_EMIT changed();
}

int TimelineModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_timeline.tracks.size();
}

QVariant TimelineModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_timeline.tracks.size())
        return {};
    const Track &t = m_timeline.tracks.at(index.row());
    switch (role) {
    case TrackIdRole:
        return t.id;
    case NameRole:
        return t.name;
    case KindRole:
        return kindName(t.kind);
    case MutedRole:
        return t.muted;
    case HiddenRole:
        return t.hidden;
    case LockedRole:
        return t.locked;
    case SoloRole:
        return t.solo;
    case VolumeRole:
        return t.volumeDb;
    case PanRole:
        return t.pan;
    case ClipsRole:
        return m_clipCache.value(index.row());
    }
    return {};
}

QHash<int, QByteArray> TimelineModel::roleNames() const
{
    return {{TrackIdRole, "trackId"}, {NameRole, "name"},     {KindRole, "kind"},     {MutedRole, "muted"},
            {HiddenRole, "hidden"},   {LockedRole, "locked"}, {SoloRole, "solo"},     {VolumeRole, "volume"},
            {PanRole, "pan"},         {ClipsRole, "clips"}};
}

QVariantList TimelineModel::markers() const
{
    QVariantList list;
    for (const Marker &m : m_timeline.markers)
        list.append(QVariantMap{{QStringLiteral("frame"), m.frame}, {QStringLiteral("name"), m.name}, {QStringLiteral("color"), m.color}});
    return list;
}

QString TimelineModel::trackKind(int row) const
{
    return row >= 0 && row < m_timeline.tracks.size() ? kindName(m_timeline.tracks.at(row).kind) : QString();
}

QVariantList TimelineModel::clipsOf(int row) const
{
    return m_clipCache.value(row);
}
