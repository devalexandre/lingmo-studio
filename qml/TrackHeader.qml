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

// Track header: a coloured tag with the track name (V1, A1, S1; double-click
// to rename), then lock, visibility and mute/solo switches.
Item {
    id: header

    required property int index
    required property string name
    required property string kind
    required property bool muted
    required property bool hidden
    required property bool locked
    required property bool solo
    property int trackIndex: 0
    property bool compact: false
    property bool minimal: false
    signal contextMenuRequested()

    readonly property color tagColor: kind === "audio" ? StudioTheme.trackAudio : kind === "subtitle" ? StudioTheme.trackCaption : StudioTheme.trackVideo

    Rectangle {
        anchors.fill: parent
        color: header.index % 2 ? StudioTheme.laneOdd : StudioTheme.laneEven
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: StudioTheme.divider
    }
    // coloured track tag
    Rectangle {
        x: 0
        y: 1
        width: 3
        height: parent.height - 3
        color: header.tagColor
        opacity: header.hidden || header.muted ? 0.4 : 1
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: header.contextMenuRequested()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 4
        spacing: 2

        // name chip
        Rectangle {
            Layout.preferredWidth: Math.max(28, nameField.implicitWidth + 10)
            Layout.maximumWidth: header.minimal ? 34 : 64
            Layout.preferredHeight: Math.min(18, header.height - 4)
            radius: 3
            color: StudioTheme.alpha(header.tagColor, 0.22)
            TextInput {
                id: nameField
                anchors.fill: parent
                anchors.leftMargin: 5
                anchors.rightMargin: 5
                verticalAlignment: TextInput.AlignVCenter
                text: header.name
                color: Qt.lighter(header.tagColor, 1.35)
                font.family: StudioTheme.font
                font.pixelSize: header.minimal ? StudioTheme.fontTiny : StudioTheme.fontSmall
                font.weight: Font.DemiBold
                readOnly: !activeFocus
                clip: true
                selectByMouse: true
                onEditingFinished: { editor.setTrackProperty(header.trackIndex, "name", text); focus = false }
                MouseArea {
                    anchors.fill: parent
                    enabled: !nameField.activeFocus
                    onDoubleClicked: { nameField.forceActiveFocus(); nameField.selectAll() }
                }
                ToolTip.visible: nameMouse.hovered && !nameField.activeFocus
                ToolTip.text: qsTr("Double-click to rename")
                ToolTip.delay: 800
                HoverHandler { id: nameMouse }
            }
        }
        Item { Layout.fillWidth: true }

        Row {
            visible: !header.minimal && header.height >= 22
            spacing: 1
            StudioButton {
                iconName: header.locked ? "lock" : "unlock"
                size: 22
                iconSize: 13
                tip: header.locked ? qsTr("Unlock track") : qsTr("Lock track")
                checkable: true
                checked: header.locked
                checkedColor: StudioTheme.warning
                onClicked: editor.setTrackProperty(header.trackIndex, "locked", !header.locked)
            }
            StudioButton {
                visible: header.kind !== "audio"
                iconName: header.hidden ? "eye-off" : "eye"
                size: 22
                iconSize: 13
                tip: header.hidden ? qsTr("Show track") : qsTr("Hide track")
                onClicked: editor.setTrackProperty(header.trackIndex, "hidden", !header.hidden)
            }
            StudioButton {
                visible: header.kind === "audio"
                text: "S"
                size: 22
                width: 22
                fontSize: StudioTheme.fontSmall
                bold: true
                tip: qsTr("Solo")
                checkable: true
                checked: header.solo
                checkedStyle: "fill"
                checkedColor: StudioTheme.solo
                onClicked: editor.setTrackProperty(header.trackIndex, "solo", !header.solo)
            }
            StudioButton {
                visible: header.kind !== "subtitle"
                text: "M"
                size: 22
                width: 22
                fontSize: StudioTheme.fontSmall
                bold: true
                tip: header.muted ? qsTr("Unmute") : qsTr("Mute")
                checkable: true
                checked: header.muted
                checkedStyle: "fill"
                checkedColor: StudioTheme.mute
                onClicked: editor.setTrackProperty(header.trackIndex, "muted", !header.muted)
            }
        }
    }
}
