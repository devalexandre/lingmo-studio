/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "videoitem.h"
#include "editor.h"
#include "player.h"

#include <QQuickWindow>
#include <QSGImageNode>

VideoItem::VideoItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

QObject *VideoItem::editor() const
{
    return m_editor;
}

void VideoItem::setEditor(QObject *editor)
{
    auto *e = qobject_cast<Editor *>(editor);
    if (e == m_editor)
        return;
    if (m_editor)
        disconnect(m_editor->player(), nullptr, this, nullptr);
    m_editor = e;
    if (m_editor) {
        connect(m_editor->player(), &Player::frameReady, this, &VideoItem::frameReady);
        connect(m_editor, &Editor::documentChanged, this, &VideoItem::updateBefore);
        connect(m_editor, &Editor::selectionChanged, this, &VideoItem::updateBefore);
        frameReady();
    }
    Q_EMIT editorChanged();
}

void VideoItem::setCompare(bool on)
{
    if (on == m_compare)
        return;
    m_compare = on;
    updateBefore();
    Q_EMIT compareChanged();
}

void VideoItem::setSplit(double s)
{
    s = std::clamp(s, 0.0, 1.0);
    if (qFuzzyCompare(s, m_split))
        return;
    m_split = s;
    update();
    Q_EMIT splitChanged();
}

void VideoItem::setContentAspect(double a)
{
    if (qFuzzyCompare(a, m_contentAspect))
        return;
    m_contentAspect = a;
    update();
    Q_EMIT contentAspectChanged();
}

void VideoItem::setFill(bool on)
{
    if (on == m_fill)
        return;
    m_fill = on;
    update();
    Q_EMIT fillChanged();
}

void VideoItem::frameReady()
{
    if (!m_editor)
        return;
    m_image = m_editor->player()->currentImage();
    m_dirty = true;
    if (m_compare && !m_editor->player()->playing())
        updateBefore();
    update();
}

void VideoItem::updateBefore()
{
    if (!m_compare || !m_editor) {
        m_before = QImage();
        update();
        return;
    }
    const int w = m_image.isNull() ? 960 : m_image.width();
    m_before = m_editor->beforeAfterFrame(true, w);
    m_dirty = true;
    update();
}

void VideoItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    update();
}

QSGNode *VideoItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    if (m_image.isNull() || !window()) {
        delete old;
        return nullptr;
    }
    auto *root = old ? old : new QSGNode;
    if (root->childCount() == 0) {
        for (int i = 0; i < 2; ++i) {
            QSGImageNode *n = window()->createImageNode();
            n->setOwnsTexture(true);
            n->setFiltering(QSGTexture::Linear);
            QImage blank(1, 1, QImage::Format_ARGB32_Premultiplied);
            blank.fill(Qt::transparent);
            n->setTexture(window()->createTextureFromImage(blank));
            root->appendChildNode(n);
        }
    }
    auto *after = static_cast<QSGImageNode *>(root->childAtIndex(0));
    auto *before = static_cast<QSGImageNode *>(root->childAtIndex(1));
    if (m_dirty) {
        after->setTexture(window()->createTextureFromImage(m_image));
        if (!m_before.isNull())
            before->setTexture(window()->createTextureFromImage(m_before));
        m_dirty = false;
    }
    // Aspect fit
    const QSizeF img = m_image.size();
    const double scale = std::min(width() / img.width(), height() / img.height());
    const QSizeF sz(img.width() * scale, img.height() * scale);
    const QRectF rect(QPointF((width() - sz.width()) / 2, (height() - sz.height()) / 2), sz);
    if (rect != m_videoRect) {
        m_videoRect = rect;
        QMetaObject::invokeMethod(this, &VideoItem::videoRectChanged, Qt::QueuedConnection);
    }
    const bool showBefore = m_compare && !m_before.isNull();
    if (showBefore) {
        const double cut = rect.width() * m_split;
        before->setRect(QRectF(rect.x(), rect.y(), cut, rect.height()));
        before->setSourceRect(QRectF(0, 0, m_before.width() * m_split, m_before.height()));
        after->setRect(QRectF(rect.x() + cut, rect.y(), rect.width() - cut, rect.height()));
        after->setSourceRect(QRectF(img.width() * m_split, 0, img.width() * (1 - m_split), img.height()));
    } else if (m_fill && width() > 0 && height() > 0) {
        // the picture inside the frame, then its centre in the item's shape
        QRectF content(QPointF(0, 0), img);
        if (m_contentAspect > 0) {
            const double frameAspect = img.width() / img.height();
            if (m_contentAspect > frameAspect)
                content.setHeight(img.width() / m_contentAspect);
            else
                content.setWidth(img.height() * m_contentAspect);
            content.moveCenter(QPointF(img.width() / 2, img.height() / 2));
        }
        const double itemAspect = width() / height();
        QRectF crop = content;
        if (content.width() / content.height() > itemAspect)
            crop.setWidth(content.height() * itemAspect);
        else
            crop.setHeight(content.width() / itemAspect);
        crop.moveCenter(content.center());
        after->setRect(QRectF(0, 0, width(), height()));
        after->setSourceRect(crop);
        before->setRect(QRectF());
    } else {
        after->setRect(rect);
        after->setSourceRect(QRectF(QPointF(0, 0), img));
        before->setRect(QRectF());
    }
    return root;
}
