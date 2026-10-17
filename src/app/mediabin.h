/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Media bin: imported files, background probing (thread pool) and proxy
// generation (ffmpeg processes, one at a time, cancellable).

#pragma once

#include "model.h"

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QProcess>
#include <functional>

class Editor;

class MediaBin : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int importing READ importing NOTIFY importingChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        PathRole,
        KindRole,
        LengthRole,
        DurationTextRole,
        InfoRole,
        ProxyStateRole,
        ProxyProgressRole,
        HasAudioRole,
        HasVideoRole,
        TranscriptRole,
        UsedRole,
    };

    explicit MediaBin(Editor *editor);
    ~MediaBin() override;

    void reset();
    using Placed = std::function<void(const Studio::MediaItem &)>;
    void importFiles(const QStringList &paths, const Placed &placed = {});

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;
    int importing() const { return m_importing; }

    Q_INVOKABLE QString idAt(int row) const;
    Q_INVOKABLE void generateProxy(const QString &mediaId);
    Q_INVOKABLE void cancelProxy(const QString &mediaId);
    Q_INVOKABLE void deleteProxy(const QString &mediaId);
    Q_INVOKABLE bool remove(const QString &mediaId);
    Q_INVOKABLE void openInSource(const QString &mediaId);
    Q_INVOKABLE bool isUsed(const QString &mediaId) const;

Q_SIGNALS:
    void countChanged();
    void importingChanged();
    void importFailed(const QString &file, const QString &reason);

private:
    void startNextProxy();
    void changedRow(const QString &id);
    QString proxyDir() const;

    Editor *m_editor;
    int m_importing = 0;
    QStringList m_proxyQueue;
    QString m_proxyRunning;
    QPointer<QProcess> m_proxyProcess;
    QHash<QString, double> m_proxyProgress;
};
