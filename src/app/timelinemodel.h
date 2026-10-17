/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Read-only view of the current timeline for QML: one row per track (display
// order), each with its clips as a list of maps. Only rows that changed are
// signalled, so dragging a clip on one track does not rebuild the others.

#pragma once

#include "model.h"

#include <QAbstractListModel>

class Editor;

class TimelineModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int duration READ duration NOTIFY changed)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY changed)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY changed)

public:
    enum Roles {
        TrackIdRole = Qt::UserRole + 1,
        NameRole,
        KindRole,
        MutedRole,
        HiddenRole,
        LockedRole,
        SoloRole,
        VolumeRole,
        PanRole,
        ClipsRole,
    };

    explicit TimelineModel(Editor *editor);

    void setTimeline(const Studio::Timeline &timeline);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int duration() const { return m_timeline.duration(); }
    QVariantList markers() const;
    int trackCount() const { return m_timeline.tracks.size(); }
    Q_INVOKABLE QString trackKind(int row) const;
    Q_INVOKABLE QVariantList clipsOf(int row) const;

Q_SIGNALS:
    void changed();

private:
    QVariantList clipList(const Studio::Track &t) const;

    Editor *m_editor;
    Studio::Timeline m_timeline;
    QList<QVariantList> m_clipCache;
};
