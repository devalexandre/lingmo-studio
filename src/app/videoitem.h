/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Shows the player's latest frame (aspect fit) as a scene graph texture.
// With `compare` on, the left part (up to `split`) shows the frame without the
// selected clip's color grade (Color page before/after).

#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickItem>

class Player;
class Editor;

class VideoItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QObject *editor READ editor WRITE setEditor NOTIFY editorChanged)
    Q_PROPERTY(bool compare READ compare WRITE setCompare NOTIFY compareChanged)
    Q_PROPERTY(double split READ split WRITE setSplit NOTIFY splitChanged)
    Q_PROPERTY(QRectF videoRect READ videoRect NOTIFY videoRectChanged)

public:
    explicit VideoItem(QQuickItem *parent = nullptr);

    QObject *editor() const;
    void setEditor(QObject *editor);
    bool compare() const { return m_compare; }
    void setCompare(bool on);
    double split() const { return m_split; }
    void setSplit(double s);
    QRectF videoRect() const { return m_videoRect; }

Q_SIGNALS:
    void editorChanged();
    void compareChanged();
    void splitChanged();
    void videoRectChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void frameReady();
    void updateBefore();

    QPointer<Editor> m_editor;
    QImage m_image;
    QImage m_before;
    bool m_compare = false;
    double m_split = 0.5;
    bool m_dirty = false;
    QRectF m_videoRect;
};
