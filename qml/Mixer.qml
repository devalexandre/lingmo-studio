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

// Mixer: one vertical channel strip per track (tag, pan knob, mute/solo,
// meter and fader, dB readout) and the master bus.
// The player measures the master mix only; a track's meter shows the master
// level while that track has a clip under the playhead and is audible.
Item {
    id: mixer

    property bool compact: false
    // strips widen to use the room when there are few tracks
    readonly property int audibleTracks: { editor.timeline.trackCount; let n = 0; for (let t = 0; t < editor.timeline.trackCount; ++t) if (editor.timeline.trackKind(t) !== "subtitle") ++n; return Math.max(1, n) }
    readonly property int stripWidth: compact ? 62 : Math.max(84, Math.min(132, Math.floor((width - masterStrip.width - 10) / audibleTracks) - 4))
    readonly property var player: editor.player
    readonly property int playhead: player.timelinePosition
    readonly property bool anySolo: {
        editor.timeline.trackCount
        for (let t = 0; t < editor.timeline.trackCount; ++t)
            if (editor.timeline.data(editor.timeline.index(t, 0), 263 /* SoloRole */))
                return true
        return false
    }

    function dbToFader(db) { return Math.max(0, Math.min(1, (db + 60) / 72)) }
    function faderToDb(v) { return v * 72 - 60 }

    ListView {
        id: strips
        anchors.fill: parent
        anchors.rightMargin: masterStrip.width + 6
        orientation: ListView.Horizontal
        spacing: 4
        clip: true
        model: editor.timeline
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar {}

        delegate: Rectangle {
            id: strip
            required property int index
            required property string name
            required property string kind
            required property bool muted
            required property bool solo
            required property real volume
            required property real pan
            readonly property color tag: kind === "audio" ? StudioTheme.trackAudio : StudioTheme.trackVideo
            readonly property bool audible: !muted && (!mixer.anySolo || solo)
            readonly property bool active: { mixer.playhead; return audible && editor.clipAt(strip.index, mixer.playhead).length > 0 }
            readonly property real gain: Math.pow(10, volume / 20)
            visible: kind !== "subtitle"
            width: visible ? mixer.stripWidth : 0
            height: strips.height
            radius: StudioTheme.smallRadius + 2
            color: StudioTheme.raised
            border.width: 1
            border.color: StudioTheme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 5
                spacing: 5

                // track tag
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 18
                    radius: 3
                    color: StudioTheme.alpha(strip.tag, 0.25)
                    Text {
                        anchors.centerIn: parent
                        text: strip.name
                        color: Qt.lighter(strip.tag, 1.35)
                        font.family: StudioTheme.font
                        font.pixelSize: StudioTheme.fontSmall
                        font.weight: Font.DemiBold
                    }
                }
                Knob {
                    Layout.alignment: Qt.AlignHCenter
                    size: mixer.compact ? 30 : 38
                    value: strip.pan
                    onMoved: function(v) { editor.setTrackProperty(strip.index, "pan", Math.abs(v) < 0.04 ? 0 : v) }
                    onReset: editor.setTrackProperty(strip.index, "pan", 0)
                }
                Row {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 3
                    StudioButton {
                        text: "M"
                        width: 22
                        size: 20
                        fontSize: StudioTheme.fontSmall
                        bold: true
                        flat: false
                        tip: strip.muted ? qsTr("Unmute") : qsTr("Mute")
                        checkable: true
                        checked: strip.muted
                        checkedStyle: "fill"
                        checkedColor: StudioTheme.mute
                        onClicked: editor.setTrackProperty(strip.index, "muted", !strip.muted)
                    }
                    StudioButton {
                        text: "S"
                        width: 22
                        size: 20
                        fontSize: StudioTheme.fontSmall
                        bold: true
                        flat: false
                        tip: qsTr("Solo")
                        checkable: true
                        checked: strip.solo
                        checkedStyle: "fill"
                        checkedColor: StudioTheme.solo
                        onClicked: editor.setTrackProperty(strip.index, "solo", !strip.solo)
                    }
                }
                // meter + fader
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 4
                    LevelMeter {
                        Layout.fillHeight: true
                        Layout.preferredWidth: 10
                        levelLeft: strip.active ? Math.min(1, mixer.player.levelLeft * strip.gain) : 0
                        levelRight: strip.active ? Math.min(1, mixer.player.levelRight * strip.gain) : 0
                    }
                    Fader {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        value: mixer.dbToFader(strip.volume)
                        showScale: !mixer.compact
                        onMoved: function(v) {
                            const db = mixer.faderToDb(v)
                            editor.setTrackProperty(strip.index, "volume", Math.abs(db) < 0.4 ? 0 : db)
                        }
                        onReset: editor.setTrackProperty(strip.index, "volume", 0)
                    }
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: (strip.volume > 0 ? "+" : "") + strip.volume.toFixed(1)
                    color: StudioTheme.textSecondary
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontTiny
                    font.features: { "tnum": 1 }
                }
            }
        }
    }

    // master bus
    Rectangle {
        id: masterStrip
        anchors.right: parent.right
        width: mixer.compact ? 70 : 96
        height: parent.height
        radius: StudioTheme.smallRadius + 2
        color: StudioTheme.raised
        border.width: 1
        border.color: StudioTheme.alpha(StudioTheme.accent, 0.6)

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 5
            spacing: 5
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 18
                radius: 3
                color: StudioTheme.accentSoft
                Text {
                    anchors.centerIn: parent
                    text: qsTr("Master")
                    color: StudioTheme.accentText
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontSmall
                    font.weight: Font.DemiBold
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 4
                LevelMeter {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 16
                    levelLeft: mixer.player.levelLeft
                    levelRight: mixer.player.levelRight
                }
                Fader {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    value: mixer.player.volume
                    showScale: false
                    onMoved: function(v) { mixer.player.volume = v }
                    onReset: mixer.player.volume = 1
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Monitor level (not exported)")
                    ToolTip.delay: 600
                }
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Monitor")
                color: StudioTheme.textSecondary
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontTiny
            }
        }
    }
}
