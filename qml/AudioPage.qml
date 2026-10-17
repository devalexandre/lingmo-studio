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

// Audio page: per-track mixer (fader, pan, mute, solo), master meter,
// voice-over recording, clip audio effects and loudness tools.
SplitView {
    id: page
    orientation: Qt.Vertical
    readonly property var sel: editor.selectedClip
    readonly property bool hasClip: sel && sel.id !== undefined

    SplitView {
        SplitView.fillHeight: true
        SplitView.minimumHeight: 280
        orientation: Qt.Horizontal

        StudioPanel {
            title: qsTr("Mixer")
            SplitView.fillWidth: true
            actions: [
                LingmoUI.ActionButton {
                    text: editor.recorder.recording ? qsTr("Stop (%1 s)").arg(editor.recorder.seconds.toFixed(1)) : qsTr("Record voice-over")
                    iconName: editor.recorder.recording ? "media-playback-stop" : "media-record"
                    destructive: editor.recorder.recording
                    enabled: editor.recorder.available
                    tipText: editor.recorder.available ? qsTr("Records the default microphone at the playhead") : qsTr("ffmpeg has no PulseAudio input here")
                    onClicked: {
                        if (editor.recorder.recording) {
                            editor.recorder.stop()
                            editor.player.pause()
                        } else if (editor.recorder.start(editor.projectPath.length ? editor.projectPath.substring(0, editor.projectPath.lastIndexOf("/")) + "/Recordings" : "")) {
                            editor.player.play()
                        }
                    }
                }
            ]

            ListView {
                id: strips
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.smallSpacing
                orientation: ListView.Horizontal
                spacing: LingmoUI.Units.smallSpacing
                clip: true
                model: editor.timeline
                ScrollBar.horizontal: ScrollBar {}
                footer: Item {
                    width: master.width + LingmoUI.Units.smallSpacing
                    height: strips.height
                    LingmoUI.Card {
                        id: master
                        x: LingmoUI.Units.smallSpacing
                        width: 110
                        height: parent.height
                        highlighted: true
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: LingmoUI.Units.smallSpacing
                            Label { text: qsTr("Master"); font.bold: true; font.pointSize: LingmoUI.Units.fontBody; color: LingmoUI.Theme.textColor; Layout.alignment: Qt.AlignHCenter }
                            LevelMeter {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.fillHeight: true
                                width: 24
                                levelLeft: editor.player.levelLeft
                                levelRight: editor.player.levelRight
                            }
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("Monitor")
                                color: LingmoUI.Theme.secondaryTextColor
                                font.pointSize: LingmoUI.Units.fontCaption
                            }
                            Slider {
                                Layout.fillWidth: true
                                from: 0; to: 1
                                value: editor.player.volume
                                onMoved: editor.player.volume = value
                            }
                        }
                    }
                }
                delegate: LingmoUI.Card {
                    id: strip
                    required property int index
                    required property string name
                    required property string kind
                    required property bool muted
                    required property bool solo
                    required property real volume
                    required property real pan
                    visible: kind !== "subtitle"
                    width: visible ? 96 : 0
                    height: strips.height
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: LingmoUI.Units.smallSpacing
                        spacing: 4
                        Label {
                            text: strip.name
                            font.bold: true
                            font.pointSize: LingmoUI.Units.fontBody
                            color: strip.kind === "audio" ? LingmoUI.Theme.greenColor : LingmoUI.Theme.blueColor
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Slider {
                            id: panSlider
                            Layout.fillWidth: true
                            from: -1; to: 1
                            value: strip.pan
                            onMoved: editor.setTrackProperty(strip.index, "pan", Math.abs(value) < 0.04 ? 0 : value)
                            ToolTip.visible: hovered || pressed
                            ToolTip.text: Math.abs(value) < 0.04 ? qsTr("Center") : (value < 0 ? qsTr("L %1").arg(Math.round(-value * 100)) : qsTr("R %1").arg(Math.round(value * 100)))
                        }
                        Slider {
                            Layout.fillHeight: true
                            Layout.alignment: Qt.AlignHCenter
                            orientation: Qt.Vertical
                            from: -60; to: 12
                            value: strip.volume
                            onMoved: editor.setTrackProperty(strip.index, "volume", Math.abs(value) < 0.4 ? 0 : value)
                            TapHandler { onDoubleTapped: editor.setTrackProperty(strip.index, "volume", 0) }
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: (strip.volume > 0 ? "+" : "") + strip.volume.toFixed(1) + " dB"
                            font.pointSize: LingmoUI.Units.fontCaption
                            font.features: { "tnum": 1 }
                            color: LingmoUI.Theme.secondaryTextColor
                        }
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 2
                            LingmoUI.IconButton {
                                iconName: strip.muted ? "audio-volume-muted" : "audio-volume-high"
                                tipText: qsTr("Mute")
                                checkable: true
                                checked: strip.muted
                                onClicked: editor.setTrackProperty(strip.index, "muted", !strip.muted)
                            }
                            LingmoUI.IconButton {
                                visible: strip.kind === "audio"
                                iconName: "audio-headphones"
                                tipText: qsTr("Solo")
                                checkable: true
                                checked: strip.solo
                                onClicked: editor.setTrackProperty(strip.index, "solo", !strip.solo)
                            }
                        }
                    }
                }
            }
        }

        StudioPanel {
            title: qsTr("Clip audio")
            SplitView.preferredWidth: 380
            SplitView.minimumWidth: 280
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.smallSpacing
                spacing: LingmoUI.Units.smallSpacing
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    enabled: page.hasClip
                    LingmoUI.ActionButton { text: qsTr("EQ"); onClicked: editor.addEffect(page.sel.id, "eq") }
                    LingmoUI.ActionButton { text: qsTr("Compressor"); onClicked: editor.addEffect(page.sel.id, "compressor") }
                    LingmoUI.ActionButton { text: qsTr("Noise reduction"); onClicked: editor.addEffect(page.sel.id, "denoise") }
                    LingmoUI.ActionButton { text: qsTr("Pan"); onClicked: editor.addEffect(page.sel.id, "pan") }
                    LingmoUI.ActionButton { text: qsTr("Volume keyframes"); onClicked: editor.addEffect(page.sel.id, "volume") }
                    LingmoUI.ActionButton { text: qsTr("Normalize −14 LUFS"); primary: true; onClicked: editor.normalizeLoudness(page.sel.id, -14) }
                    LingmoUI.ActionButton { text: qsTr("Cut silence"); onClicked: editor.splitOnSilence(page.sel.id, -35, 0.6, true) }
                }
                Inspector {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    categories: ["audio"]
                    showClipSection: false
                }
            }
        }
    }

    TimelinePanel {
        SplitView.preferredHeight: 300
        SplitView.minimumHeight: 160
        showToolbar: false
        compact: true
    }
}
