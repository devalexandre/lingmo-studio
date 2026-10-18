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

// Which local speech-to-text engine and model will run, on which device and how
// fast; a warning when the model is too heavy for this computer (with the
// recommended one and GPU support one click away), a per-run model choice, or
// how to install an engine. Defaults come from Settings › AI › Transcription.
ColumnLayout {
    id: status
    readonly property var transcriber: editor.transcriber
    // Show the per-run model choice
    property bool modelChoice: true
    // Narrow places (Gerar cortes' side panel) stack the controls
    readonly property bool narrow: width < 520
    property string pendingModel: ""

    spacing: LingmoUI.Units.smallSpacing

    function languageName(code) {
        const names = { "pt": qsTr("Portuguese"), "en": qsTr("English"), "es": qsTr("Spanish"), "auto": qsTr("detected automatically") }
        return names[code] || code
    }

    Connections {
        target: status.transcriber
        function onDownloadChanged() {
            // a model chosen in the list but not downloaded yet: use it once it is there
            if (!status.transcriber.downloading && status.pendingModel !== "") {
                const name = status.pendingModel
                status.pendingModel = ""
                if (status.transcriber.downloadError === "")
                    status.transcriber.modelOverride = name
            }
        }
    }

    // ---- ready: model · device · speed
    LingmoUI.Card {
        id: readyCard
        visible: status.transcriber.available
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: LingmoUI.Units.largeSpacing
            spacing: LingmoUI.Units.smallSpacing
            RowLayout {
                Layout.fillWidth: true
                spacing: LingmoUI.Units.largeSpacing
                LingmoUI.IconItem {
                    source: status.transcriber.gpu ? "video-display" : "audio-input-microphone"
                    implicitWidth: LingmoUI.Units.iconSizes.smallMedium
                    implicitHeight: LingmoUI.Units.iconSizes.smallMedium
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        text: status.transcriber.summary !== "" ? status.transcriber.summary
                                                                : qsTr("Local transcription with %1").arg(status.transcriber.engine)
                        font.pointSize: LingmoUI.Units.fontBody
                        font.bold: true
                        color: status.transcriber.unsuitable ? LingmoUI.Theme.orangeColor : LingmoUI.Theme.textColor
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("%1 · language: %2 · nothing leaves this computer")
                              .arg(status.transcriber.engine).arg(status.languageName(status.transcriber.language))
                        font.pointSize: LingmoUI.Units.fontCaption
                        color: LingmoUI.Theme.secondaryTextColor
                        wrapMode: Text.WordWrap
                    }
                }
                LingmoUI.Badge {
                    text: status.transcriber.gpu ? qsTr("GPU") : qsTr("CPU")
                    tone: status.transcriber.gpu ? LingmoUI.Theme.greenColor : LingmoUI.Theme.secondaryTextColor
                    visible: status.transcriber.engine === "whisper.cpp"
                }
                LingmoUI.IconButton {
                    iconName: "configure"
                    tipText: qsTr("Choose the model in Settings")
                    onClicked: editor.openTranscriptionSettings()
                }
            }

            // per-run model
            GridLayout {
                Layout.fillWidth: true
                visible: status.modelChoice && status.transcriber.engine === "whisper.cpp"
                columns: status.narrow ? 1 : 2
                columnSpacing: LingmoUI.Units.largeSpacing
                rowSpacing: 2
                Label {
                    text: qsTr("Model for this run")
                    font.pointSize: LingmoUI.Units.fontCaption
                    color: LingmoUI.Theme.secondaryTextColor
                }
                ComboBox {
                    id: modelBox
                    Layout.fillWidth: true
                    enabled: !status.transcriber.running && !status.transcriber.downloading
                    readonly property var choices: [{ name: "", label: qsTr("As in Settings"), installed: true }]
                                                   .concat(status.transcriber.modelChoices)
                    model: choices
                    textRole: "label"
                    valueRole: "name"
                    currentIndex: Math.max(0, choices.findIndex(c => c.name === status.transcriber.modelOverride))
                    onActivated: function(i) {
                        const c = choices[i]
                        if (c.installed) {
                            status.transcriber.modelOverride = c.name
                        } else {
                            status.pendingModel = c.name
                            status.transcriber.downloadModel(c.name)
                        }
                    }
                }
            }
        }
    }

    // ---- the model is too heavy here: switch to the recommended one
    LingmoUI.Banner {
        Layout.fillWidth: true
        visible: status.transcriber.available && status.transcriber.unsuitable
        type: LingmoUI.Banner.Warning
        closable: false
        title: visible ? qsTr("%1 is slow on this computer").arg(status.transcriber.runModel) : ""
        text: visible ? (qsTr("It would run at %1 on the %2. Recommended here: %3.")
                         .arg(status.transcriber.speedText).arg(status.transcriber.device).arg(status.transcriber.recommendedSummary)
                         + (status.transcriber.gpuHint !== "" ? " " + status.transcriber.gpuHint + "." : ""))
                      : ""
        LingmoUI.ActionButton {
            text: status.transcriber.recommendedInstalled ? qsTr("Use %1").arg(status.transcriber.recommendedModel)
                                                          : qsTr("Download and use %1").arg(status.transcriber.recommendedModel)
            primary: true
            enabled: !status.transcriber.downloading && !status.transcriber.running
            onClicked: status.transcriber.useRecommended()
        }
        LingmoUI.ActionButton {
            visible: status.transcriber.gpuPackage !== ""
            text: qsTr("Install GPU support")
            busy: status.transcriber.installing
            enabled: !status.transcriber.installing
            onClicked: status.transcriber.installGpuSupport()
        }
        LingmoUI.ActionButton { text: qsTr("Settings"); quiet: true; onClicked: editor.openTranscriptionSettings() }
    }

    // ---- a GPU without its backend (when the model itself is fine)
    LingmoUI.Banner {
        Layout.fillWidth: true
        visible: status.transcriber.available && !status.transcriber.unsuitable
                 && (status.transcriber.gpuPackage !== "" || status.transcriber.installMessage !== "")
        type: status.transcriber.installFailed ? LingmoUI.Banner.Error : LingmoUI.Banner.Info
        iconName: "video-display"
        closable: false
        title: visible ? status.transcriber.gpuHint : ""
        text: visible ? (status.transcriber.installMessage !== "" ? status.transcriber.installMessage
                         : qsTr("Transcription runs on the processor now.")) : ""
        LingmoUI.ActionButton {
            visible: status.transcriber.gpuPackage !== ""
            text: status.transcriber.installing ? qsTr("Installing…") : qsTr("Install GPU support")
            busy: status.transcriber.installing
            enabled: !status.transcriber.installing
            onClicked: status.transcriber.installGpuSupport()
        }
    }

    Label {
        Layout.fillWidth: true
        visible: status.transcriber.unsuitable && status.transcriber.installMessage !== ""
        text: status.transcriber.installMessage
        wrapMode: Text.WordWrap
        font.pointSize: LingmoUI.Units.fontCaption
        color: status.transcriber.installFailed ? LingmoUI.Theme.redColor : LingmoUI.Theme.secondaryTextColor
    }

    // ---- model download
    ColumnLayout {
        Layout.fillWidth: true
        visible: status.transcriber.downloading || status.transcriber.downloadError !== ""
        spacing: 2
        RowLayout {
            Layout.fillWidth: true
            visible: status.transcriber.downloading
            Label {
                Layout.fillWidth: true
                text: qsTr("Downloading %1 · %2").arg(status.transcriber.downloadName).arg(status.transcriber.downloadDetail)
                font.pointSize: LingmoUI.Units.fontCaption
                color: LingmoUI.Theme.secondaryTextColor
                elide: Text.ElideRight
            }
            LingmoUI.ActionButton { text: qsTr("Cancel"); quiet: true; onClicked: status.transcriber.cancelDownload() }
        }
        ProgressBar {
            Layout.fillWidth: true
            visible: status.transcriber.downloading
            from: 0; to: 1
            value: Math.max(0, status.transcriber.downloadProgress)
            indeterminate: status.transcriber.downloadProgress < 0
        }
        Label {
            Layout.fillWidth: true
            visible: !status.transcriber.downloading && status.transcriber.downloadError !== ""
            text: status.transcriber.downloadError
            wrapMode: Text.WordWrap
            font.pointSize: LingmoUI.Units.fontCaption
            color: LingmoUI.Theme.redColor
        }
    }

    // ---- nothing to transcribe with yet
    LingmoUI.Banner {
        id: missing
        visible: !status.transcriber.available
        Layout.fillWidth: true
        type: LingmoUI.Banner.Warning
        closable: false
        title: visible ? (status.transcriber.engineInstalled ? qsTr("Download a speech model") : qsTr("Install a transcription engine")) : ""
        text: visible ? (status.transcriber.engineInstalled
                         ? (status.transcriber.recommendedModel !== ""
                            ? qsTr("whisper.cpp is installed. %1 suits this computer (%2).").arg(status.transcriber.recommendedModel).arg(status.transcriber.recommendedSummary)
                            : qsTr("whisper.cpp is installed. Download a model (base or small are good for Portuguese) in Settings › AI › Transcription and captions."))
                         : qsTr("Captions are made on this computer with whisper.cpp. Install it with “sudo pacman -S whisper-cpp”, then download a model in Settings › AI › Transcription and captions. faster-whisper (whisper-ctranslate2, from pipx or the AUR) also works."))
                      : ""
        LingmoUI.ActionButton {
            visible: status.transcriber.engineInstalled && status.transcriber.recommendedModel !== ""
            text: qsTr("Download %1").arg(status.transcriber.recommendedModel)
            primary: true
            enabled: !status.transcriber.downloading
            onClicked: status.transcriber.useRecommended()
        }
        LingmoUI.ActionButton { text: qsTr("Check again"); onClicked: status.transcriber.refresh() }
        LingmoUI.ActionButton { text: qsTr("Open Settings"); onClicked: editor.openTranscriptionSettings() }
    }
}
