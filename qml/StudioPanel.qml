/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A workspace panel: charcoal surface, 8 px radius, 1 px border, and a
// compact header (title on the left, actions on the right) above a hairline.
Rectangle {
    id: panel

    property string title: ""
    property string subtitle: ""
    property string icon: ""
    default property alias content: body.data
    property alias actions: actionsRow.data
    property alias leading: leadingRow.data
    property bool showHeader: true
    readonly property int headerHeight: header.visible ? header.height : 0

    color: StudioTheme.panel
    radius: StudioTheme.radius
    border.width: 1
    border.color: StudioTheme.border
    clip: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        RowLayout {
            id: header
            visible: panel.showHeader && (panel.title.length > 0 || actionsRow.children.length > 0 || leadingRow.children.length > 0)
            Layout.fillWidth: true
            Layout.preferredHeight: StudioTheme.headerHeight
            Layout.leftMargin: StudioTheme.pad
            Layout.rightMargin: 4
            spacing: 6

            StudioIcon {
                visible: panel.icon.length > 0
                name: panel.icon
                size: 14
                color: StudioTheme.textSecondary
            }
            Text {
                visible: panel.title.length > 0
                text: panel.title
                color: StudioTheme.text
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontBody
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.maximumWidth: Math.max(60, panel.width * 0.5)
            }
            Text {
                visible: panel.subtitle.length > 0
                text: panel.subtitle
                color: StudioTheme.textSecondary
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontSmall
                elide: Text.ElideRight
                Layout.maximumWidth: Math.max(40, panel.width * 0.3)
            }
            RowLayout {
                id: leadingRow
                spacing: 2
            }
            Item { Layout.fillWidth: true }
            RowLayout {
                id: actionsRow
                spacing: 2
            }
        }
        Rectangle {
            visible: header.visible
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: StudioTheme.divider
        }

        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
        }
    }
}
