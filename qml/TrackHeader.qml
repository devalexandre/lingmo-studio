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
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// Name and switches of one track (model roles from TimelineModel).
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
    signal contextMenuRequested()

    Rectangle {
        anchors.fill: parent
        anchors.rightMargin: 1
        color: header.kind === "audio" ? Qt.rgba(LingmoUI.Theme.greenColor.r, LingmoUI.Theme.greenColor.g, LingmoUI.Theme.greenColor.b, 0.08)
             : header.kind === "subtitle" ? Qt.rgba(LingmoUI.Theme.pinkColor.r, LingmoUI.Theme.pinkColor.g, LingmoUI.Theme.pinkColor.b, 0.08)
             : Qt.rgba(LingmoUI.Theme.blueColor.r, LingmoUI.Theme.blueColor.g, LingmoUI.Theme.blueColor.b, 0.08)
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: LingmoUI.Theme.dividerColor
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: header.contextMenuRequested()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: LingmoUI.Units.smallSpacing
        anchors.rightMargin: 2
        spacing: 0

        TextField {
            id: nameField
            Layout.fillWidth: true
            Layout.minimumWidth: 28
            text: header.name
            font.pointSize: LingmoUI.Units.fontCaption
            font.bold: true
            readOnly: !activeFocus
            background: null
            onEditingFinished: { editor.setTrackProperty(header.trackIndex, "name", text); focus = false }
        }
        LingmoUI.IconButton {
            visible: header.kind !== "audio"
            iconName: header.hidden ? "view-hidden" : "view-visible"
            tipText: header.hidden ? qsTr("Show track") : qsTr("Hide track")
            size: 22
            iconSize: 14
            onClicked: editor.setTrackProperty(header.trackIndex, "hidden", !header.hidden)
        }
        LingmoUI.IconButton {
            visible: header.kind !== "subtitle"
            iconName: header.muted ? "audio-volume-muted" : "audio-volume-high"
            tipText: header.muted ? qsTr("Unmute") : qsTr("Mute")
            size: 22
            iconSize: 14
            checkable: true
            checked: header.muted
            onClicked: editor.setTrackProperty(header.trackIndex, "muted", !header.muted)
        }
        LingmoUI.IconButton {
            visible: header.kind === "audio" && !header.compact
            iconName: "audio-headphones"
            tipText: qsTr("Solo")
            size: 22
            iconSize: 14
            checkable: true
            checked: header.solo
            onClicked: editor.setTrackProperty(header.trackIndex, "solo", !header.solo)
        }
        LingmoUI.IconButton {
            iconName: header.locked ? "object-locked" : "object-unlocked"
            tipText: header.locked ? qsTr("Unlock") : qsTr("Lock")
            size: 22
            iconSize: 14
            checkable: true
            checked: header.locked
            onClicked: editor.setTrackProperty(header.trackIndex, "locked", !header.locked)
        }
    }
}
