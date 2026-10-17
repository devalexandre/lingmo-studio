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

// Audio page (Fairlight-like): the timeline with waveforms on top, the
// mixer below (channel strips with meters, pan, mute/solo and faders),
// and the selected clip's audio effects and loudness tools on the right.
SplitView {
    id: page
    orientation: Qt.Vertical
    handle: PanelHandle { }
    readonly property var sel: editor.selectedClip
    readonly property bool hasClip: sel && sel.id !== undefined

    TimelinePanel {
        SplitView.preferredHeight: 330
        SplitView.minimumHeight: 160
        showToolbar: false
        showOverview: false
        compact: true
    }

    SplitView {
        SplitView.fillHeight: true
        SplitView.minimumHeight: 260
        orientation: Qt.Horizontal
        handle: PanelHandle { }

        StudioPanel {
            title: qsTr("Mixer")
            icon: "mixer"
            SplitView.fillWidth: true
            actions: [
                StudioButton {
                    text: editor.recorder.recording ? qsTr("Stop (%1 s)").arg(editor.recorder.seconds.toFixed(1)) : qsTr("Record voice-over")
                    iconName: editor.recorder.recording ? "stop" : "record"
                    iconSize: 12
                    size: 24
                    fontSize: StudioTheme.fontSmall
                    danger: editor.recorder.recording
                    flat: false
                    enabled: editor.recorder.available
                    tip: editor.recorder.available ? qsTr("Records the default microphone at the playhead") : qsTr("ffmpeg has no PulseAudio input here")
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
            Mixer {
                anchors.fill: parent
                anchors.margins: 6
            }
        }

        StudioPanel {
            title: qsTr("Clip audio")
            subtitle: page.hasClip ? (page.sel.name || "") : ""
            SplitView.preferredWidth: 380
            SplitView.minimumWidth: 280
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    enabled: page.hasClip
                    StudioButton { text: qsTr("EQ"); flat: false; size: 26; fontSize: StudioTheme.fontSmall; onClicked: editor.addEffect(page.sel.id, "eq") }
                    StudioButton { text: qsTr("Compressor"); flat: false; size: 26; fontSize: StudioTheme.fontSmall; onClicked: editor.addEffect(page.sel.id, "compressor") }
                    StudioButton { text: qsTr("Noise reduction"); flat: false; size: 26; fontSize: StudioTheme.fontSmall; onClicked: editor.addEffect(page.sel.id, "denoise") }
                    StudioButton { text: qsTr("Pan"); flat: false; size: 26; fontSize: StudioTheme.fontSmall; onClicked: editor.addEffect(page.sel.id, "pan") }
                    StudioButton { text: qsTr("Volume keyframes"); flat: false; size: 26; fontSize: StudioTheme.fontSmall; onClicked: editor.addEffect(page.sel.id, "volume") }
                    StudioButton { text: qsTr("Cut silence"); flat: false; size: 26; fontSize: StudioTheme.fontSmall; onClicked: editor.splitOnSilence(page.sel.id, -35, 0.6, true) }
                    StudioButton { text: qsTr("Normalize −14 LUFS"); primary: true; size: 26; fontSize: StudioTheme.fontSmall; onClicked: editor.normalizeLoudness(page.sel.id, -14) }
                }
                Inspector {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    categories: ["audio"]
                    showClipSection: false
                    showEffectsHeader: false
                }
            }
        }
    }
}
