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

// Which local speech-to-text engine captions use, or how to install one.
// Engine, model and language are chosen in Settings › AI › Transcription.
Item {
    id: status
    readonly property var transcriber: editor.transcriber
    implicitHeight: transcriber.available ? readyCard.implicitHeight : missing.implicitHeight

    function languageName(code) {
        const names = { "pt": qsTr("Portuguese"), "en": qsTr("English"), "es": qsTr("Spanish"), "auto": qsTr("detected automatically") }
        return names[code] || code
    }

    LingmoUI.Card {
        id: readyCard
        visible: status.transcriber.available
        anchors.left: parent.left
        anchors.right: parent.right
        implicitHeight: readyRow.implicitHeight + LingmoUI.Units.largeSpacing * 1.5
        RowLayout {
            id: readyRow
            anchors.fill: parent
            anchors.leftMargin: readyCard.padding
            anchors.rightMargin: LingmoUI.Units.smallSpacing
            spacing: LingmoUI.Units.largeSpacing
            LingmoUI.IconItem {
                source: "audio-input-microphone"
                implicitWidth: LingmoUI.Units.iconSizes.smallMedium
                implicitHeight: LingmoUI.Units.iconSizes.smallMedium
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Local transcription with %1").arg(status.transcriber.engine)
                    font.pointSize: LingmoUI.Units.fontBody
                    font.bold: true
                    color: LingmoUI.Theme.textColor
                    wrapMode: Text.WordWrap
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Model %1 · language: %2 · nothing leaves this computer").arg(status.transcriber.model).arg(status.languageName(status.transcriber.language))
                    font.pointSize: LingmoUI.Units.fontCaption
                    color: LingmoUI.Theme.secondaryTextColor
                    wrapMode: Text.WordWrap
                }
            }
            LingmoUI.Badge { text: qsTr("Word timing"); subtle: true; visible: status.width > 520 }
            LingmoUI.IconButton {
                iconName: "configure"
                tipText: qsTr("Choose the model in Settings")
                onClicked: editor.openTranscriptionSettings()
            }
        }
    }

    LingmoUI.Banner {
        id: missing
        visible: !status.transcriber.available
        anchors.left: parent.left
        anchors.right: parent.right
        type: LingmoUI.Banner.Warning
        closable: false
        title: visible ? (status.transcriber.engineInstalled ? qsTr("Download a speech model") : qsTr("Install a transcription engine")) : ""
        text: visible ? (status.transcriber.engineInstalled
                         ? qsTr("whisper.cpp is installed. Download a model (base or small are good for Portuguese) in Settings › AI › Transcription and captions.")
                         : qsTr("Captions are made on this computer with whisper.cpp. Install it with “sudo pacman -S whisper-cpp”, then download a model in Settings › AI › Transcription and captions. faster-whisper (whisper-ctranslate2, from pipx or the AUR) also works."))
                      : ""
        LingmoUI.ActionButton { text: qsTr("Check again"); onClicked: status.transcriber.refresh() }
        LingmoUI.ActionButton { text: qsTr("Open Settings"); primary: true; onClicked: editor.openTranscriptionSettings() }
    }
}
