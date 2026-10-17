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

// A collapsible inspector section: chevron, title and header controls on a
// raised strip; the body folds away with a short animation.
ColumnLayout {
    id: section

    property string title: ""
    property bool expanded: true
    property bool dim: false
    property color accent: "transparent"   // a thin tone mark (effect category)
    default property alias content: body.data
    property alias tools: toolsRow.data

    spacing: 0

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 30
        radius: StudioTheme.smallRadius + 2
        color: headMouse.containsMouse ? StudioTheme.raisedHover : StudioTheme.raised
        Behavior on color { ColorAnimation { duration: StudioTheme.fast } }

        MouseArea {
            id: headMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: section.expanded = !section.expanded
        }
        Rectangle {
            visible: section.accent.a > 0
            x: 0
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: 14
            radius: 1.5
            color: section.accent
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 4
            spacing: 4
            StudioIcon {
                name: "chevron-right"
                size: 12
                color: StudioTheme.textSecondary
                rotation: section.expanded ? 90 : 0
                Behavior on rotation { NumberAnimation { duration: StudioTheme.normal; easing.type: Easing.OutCubic } }
            }
            Text {
                Layout.fillWidth: true
                text: section.title
                color: section.dim ? StudioTheme.textSecondary : StudioTheme.text
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontBody
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            RowLayout {
                id: toolsRow
                spacing: 1
            }
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: section.expanded ? body.implicitHeight + 14 : 0
        clip: true
        Behavior on Layout.preferredHeight { NumberAnimation { duration: StudioTheme.normal; easing.type: Easing.OutCubic } }
        ColumnLayout {
            id: body
            x: 8
            y: 8
            width: parent.width - 16
            spacing: 6
            opacity: section.expanded ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: StudioTheme.fast } }
        }
    }
}
