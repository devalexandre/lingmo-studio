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

// Edit Index: every event of the timeline (clips and markers) in record
// order, and the undo history. Click an event to go to it.
StudioPanel {
    id: editIndex

    property bool showHistory: false
    property var events: []

    title: qsTr("Edit Index")
    leading: [
        Row {
            spacing: 2
            Layout.leftMargin: 4
            StudioButton { text: qsTr("Events"); size: 22; fontSize: StudioTheme.fontSmall; checkable: true; checked: !editIndex.showHistory; onClicked: editIndex.showHistory = false }
            StudioButton { text: qsTr("History"); size: 22; fontSize: StudioTheme.fontSmall; checkable: true; checked: editIndex.showHistory; onClicked: editIndex.showHistory = true }
        }
    ]

    function rebuild() {
        const tl = editor.timeline
        const list = []
        for (let t = 0; t < tl.trackCount; ++t) {
            const clips = tl.clipsOf(t)
            const track = tl.data(tl.index(t, 0), 258) || ""
            for (let i = 0; i < clips.length; ++i)
                list.push({ kind: tl.trackKind(t), track: track, id: clips[i].id, name: clips[i].name || "", start: clips[i].position,
                            end: clips[i].position + clips[i].duration, fx: clips[i].effects })
        }
        const markers = tl.markers
        for (let m = 0; m < markers.length; ++m)
            list.push({ kind: "marker", track: "◆", id: "", name: markers[m].name || qsTr("Marker"), start: markers[m].frame, end: markers[m].frame, fx: 0 })
        list.sort(function(a, b) { return a.start - b.start || a.track.localeCompare(b.track) })
        events = list
    }
    Component.onCompleted: rebuild()
    Connections {
        target: editor.timeline
        function onChanged() { Qt.callLater(editIndex.rebuild) }
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.margins: 4
        visible: !editIndex.showHistory
        clip: true
        model: editIndex.events
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        header: Rectangle {
            width: list.width
            height: 22
            color: "transparent"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 6
                Repeater {
                    model: [{ t: "#", w: 22 }, { t: qsTr("Track"), w: 34 }, { t: qsTr("Name"), w: -1 }, { t: qsTr("Record in"), w: 76 }]
                    Text {
                        Layout.preferredWidth: modelData.w > 0 ? modelData.w : -1
                        Layout.fillWidth: modelData.w < 0
                        text: modelData.t
                        color: StudioTheme.textDisabled
                        font.family: StudioTheme.font
                        font.pixelSize: StudioTheme.fontTiny
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
        delegate: Rectangle {
            id: row
            required property int index
            required property var modelData
            readonly property bool current: modelData.id.length > 0 && editor.selection.indexOf(modelData.id) >= 0
            width: list.width
            height: 26
            radius: 4
            color: current ? StudioTheme.accentSoft : rowMouse.containsMouse ? StudioTheme.raised : "transparent"
            Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 6
                Text {
                    Layout.preferredWidth: 22
                    text: row.index + 1
                    color: StudioTheme.textDisabled
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontTiny
                    font.features: { "tnum": 1 }
                }
                Rectangle {
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 16
                    radius: 3
                    readonly property color tone: row.modelData.kind === "audio" ? StudioTheme.trackAudio : row.modelData.kind === "subtitle" ? StudioTheme.trackCaption
                                                : row.modelData.kind === "marker" ? StudioTheme.warning : StudioTheme.trackVideo
                    color: StudioTheme.alpha(tone, 0.22)
                    Text {
                        anchors.centerIn: parent
                        text: row.modelData.track
                        color: Qt.lighter(parent.tone, 1.3)
                        font.family: StudioTheme.font
                        font.pixelSize: StudioTheme.fontTiny
                        font.weight: Font.DemiBold
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: row.modelData.name + (row.modelData.fx > 0 ? "  · fx" : "")
                    color: StudioTheme.text
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontSmall
                    elide: Text.ElideRight
                }
                Text {
                    Layout.preferredWidth: 76
                    text: editor.timecode(row.modelData.start)
                    color: StudioTheme.textSecondary
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontTiny
                    font.features: { "tnum": 1 }
                }
            }
            MouseArea {
                id: rowMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    if (editor.player.mode !== "timeline")
                        editor.player.setMode("timeline")
                    editor.player.seek(row.modelData.start)
                    if (row.modelData.id.length)
                        editor.select(row.modelData.id)
                }
            }
        }
    }

    HistoryPanel {
        anchors.fill: parent
        anchors.margins: 4
        visible: editIndex.showHistory
    }
}
