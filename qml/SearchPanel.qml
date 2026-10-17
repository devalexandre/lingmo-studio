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

// Search clips by what is said in them (local transcripts from whisper) and
// by subtitle text on the timeline.
ColumnLayout {
    spacing: LingmoUI.Units.smallSpacing

    LingmoUI.Banner {
        Layout.fillWidth: true
        type: LingmoUI.Banner.Info
        closable: false
        visible: !editor.whisperAvailable
        text: visible ? qsTr("Spoken-word search needs transcripts. Install whisper.cpp (whisper-cli and a ggml model) or faster-whisper, then use “Automatic subtitles” on a clip. Everything runs on this computer.") : ""
    }
    TextField {
        Layout.fillWidth: true
        Layout.leftMargin: LingmoUI.Units.smallSpacing
        Layout.rightMargin: LingmoUI.Units.smallSpacing
        placeholderText: qsTr("Find a moment by its words…")
        onAccepted: editor.searchTranscripts(text)
        onTextChanged: searchDelay.restart()
        Timer { id: searchDelay; interval: 300; onTriggered: editor.searchTranscripts(parent.text) }
    }
    ListView {
        id: results
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 2
        model: editor.searchResults
        ScrollBar.vertical: ScrollBar {}
        delegate: LingmoUI.Card {
            width: results.width
            implicitHeight: col.implicitHeight + LingmoUI.Units.smallSpacing * 2
            clickable: true
            onClicked: {
                if (modelData.timeline) {
                    editor.player.setMode("timeline")
                    editor.player.seek(modelData.frame)
                } else {
                    editor.mediaBin.openInSource(modelData.mediaId)
                    editor.player.seek(modelData.frame)
                }
            }
            ColumnLayout {
                id: col
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.smallSpacing
                anchors.leftMargin: LingmoUI.Units.largeSpacing
                spacing: 0
                Label {
                    text: modelData.name + " · " + editor.timecode(modelData.frame)
                    color: LingmoUI.Theme.secondaryTextColor
                    font.pointSize: LingmoUI.Units.fontCaption
                }
                Label {
                    Layout.fillWidth: true
                    text: modelData.text
                    wrapMode: Text.WordWrap
                    color: LingmoUI.Theme.textColor
                    font.pointSize: LingmoUI.Units.fontBody
                }
            }
        }
    }
}
