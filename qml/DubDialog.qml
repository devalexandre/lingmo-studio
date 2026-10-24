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
import QtQuick.Dialogs
import QtQuick.Layouts

// Export › dub the video into other languages in the speaker's own voice.
// One audio file per language, as long as the video, for YouTube Studio ›
// Languages › Add dub.
Dialog {
    id: dialog
    anchors.centerIn: Overlay.overlay
    modal: true
    width: Math.min(Overlay.overlay ? Overlay.overlay.width - 80 : 900, 920)
    height: Math.min(Overlay.overlay ? Overlay.overlay.height - 60 : 760, 780)
    title: qsTr("Dub in another language")
    padding: 14

    readonly property var dub: editor.dubber

    function toggle(code) {
        const list = []
        for (let k = 0; k < dub.targets.length; ++k)
            list.push(dub.targets[k])
        const i = list.indexOf(code)
        if (i >= 0)
            list.splice(i, 1)
        else
            list.push(code)
        dub.targets = list
    }

    FileDialog {
        id: voiceDialog
        title: qsTr("Choose a recording of the voice")
        nameFilters: [qsTr("Audio and video (*.wav *.mp3 *.m4a *.flac *.ogg *.mp4 *.mov *.mkv *.webm)")]
        onAccepted: dialog.dub.referencePath = selectedFile.toString()
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Save the dubs in")
        onAccepted: dialog.dub.outputFolder = selectedFolder.toString()
    }

    component Heading: Text {
        color: StudioTheme.textSecondary
        font.family: StudioTheme.font
        font.pixelSize: StudioTheme.fontSmall
        font.weight: Font.DemiBold
    }
    component Body: Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: StudioTheme.textSecondary
        font.family: StudioTheme.font
        font.pixelSize: StudioTheme.fontSmall
    }

    contentItem: ScrollView {
        id: scroll
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroll.availableWidth - 12
            spacing: 10

            // ---------------------------------------------------- the engine
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: engineCol.implicitHeight + 20
                radius: StudioTheme.radius
                color: StudioTheme.raised
                border.width: 1
                border.color: dialog.dub.engineReady ? StudioTheme.border : StudioTheme.warning
                ColumnLayout {
                    id: engineCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Heading { text: qsTr("Voice engine") }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: dialog.dub.engineSummary
                            color: dialog.dub.engineReady ? StudioTheme.success : StudioTheme.text
                            font.family: StudioTheme.font
                            font.pixelSize: StudioTheme.fontSmall
                            elide: Text.ElideRight
                        }
                    }
                    Body {
                        visible: !dialog.dub.engineReady && !dialog.dub.installing && !dialog.dub.engineChecking
                        text: qsTr("Cloning a voice needs Chatterbox (open source, MIT licence) with PyTorch and a 3 GB voice model: about 9 GB on disk, installed once into your home folder. Everything then runs on this computer.")
                    }
                    Body {
                        visible: dialog.dub.engineReady && !dialog.dub.onGpu
                        text: qsTr("No usable graphics card was found, so the voice runs on the CPU: about 12 times slower than the speech lasts. It works, but a card with 6 GB of video memory is far faster.")
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: dialog.dub.installing
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            text: dialog.dub.installMessage
                            wrapMode: Text.WordWrap
                            color: StudioTheme.text
                            font.family: StudioTheme.font
                            font.pixelSize: StudioTheme.fontSmall
                        }
                        ProgressBar { Layout.fillWidth: true; indeterminate: true }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !dialog.dub.installing && dialog.dub.installMessage.length > 0 && !dialog.dub.engineReady
                        text: dialog.dub.installMessage
                        wrapMode: Text.WordWrap
                        color: dialog.dub.installFailed ? StudioTheme.danger : StudioTheme.textSecondary
                        font.family: StudioTheme.font
                        font.pixelSize: StudioTheme.fontSmall
                    }
                    RowLayout {
                        visible: !dialog.dub.engineReady && !dialog.dub.engineChecking
                        StudioButton {
                            text: dialog.dub.installing ? qsTr("Cancel installation") : (dialog.dub.installFailed ? qsTr("Try again") : qsTr("Install the voice engine"))
                            primary: !dialog.dub.installing
                            flat: dialog.dub.installing ? false : true
                            onClicked: dialog.dub.installing ? dialog.dub.cancelInstall() : dialog.dub.installEngine()
                        }
                        StudioButton {
                            visible: !dialog.dub.installing
                            text: qsTr("Check again")
                            flat: false
                            onClicked: dialog.dub.checkEngine()
                        }
                    }
                }
            }

            // ---------------------------------------------------- languages
            Heading { text: qsTr("Languages (%1 chosen)").arg(dialog.dub.targets.length) }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: dialog.dub.languages
                    StudioButton {
                        required property var modelData
                        visible: modelData.code !== dialog.dub.sourceLanguage
                        text: modelData.native === modelData.name ? modelData.name : modelData.name + " · " + modelData.native
                        flat: false
                        checkable: true
                        checked: dialog.dub.targets.indexOf(modelData.code) >= 0
                        enabled: !dialog.dub.running
                        size: 26
                        fontSize: StudioTheme.fontSmall
                        onClicked: dialog.toggle(modelData.code)
                    }
                }
            }

            // ---------------------------------------------------- voice
            Heading { text: qsTr("Voice to clone"); Layout.topMargin: 4 }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    text: dialog.dub.referencePath.length > 0 ? dialog.dub.referenceInfo
                          : qsTr("Automatic: the cleanest, most continuous 10 to 15 seconds of speech in the video")
                    elide: Text.ElideMiddle
                    color: StudioTheme.text
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontSmall
                }
                StudioButton {
                    text: qsTr("Choose a recording…")
                    flat: false
                    size: 24
                    fontSize: StudioTheme.fontSmall
                    enabled: !dialog.dub.running
                    onClicked: voiceDialog.open()
                }
                StudioButton {
                    visible: dialog.dub.referencePath.length > 0
                    text: qsTr("Automatic")
                    flat: false
                    size: 24
                    fontSize: StudioTheme.fontSmall
                    enabled: !dialog.dub.running
                    onClicked: dialog.dub.referencePath = ""
                }
            }
            Body { text: qsTr("A quiet recording of one person, without music behind, gives the best clone.") }

            // ---------------------------------------------------- output
            Heading { text: qsTr("Save in"); Layout.topMargin: 4 }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    text: dialog.dub.outputFolder
                    elide: Text.ElideMiddle
                    color: StudioTheme.text
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontSmall
                }
                StudioButton {
                    iconName: "folder"
                    tip: qsTr("Choose a folder…")
                    flat: false
                    enabled: !dialog.dub.running
                    onClicked: folderDialog.open()
                }
            }
            Body { visible: dialog.dub.estimate.length > 0; text: dialog.dub.estimate }

            // ---------------------------------------------------- progress
            ColumnLayout {
                Layout.fillWidth: true
                visible: dialog.dub.running
                spacing: 3
                Text {
                    Layout.fillWidth: true
                    text: dialog.dub.stage
                    elide: Text.ElideRight
                    color: StudioTheme.text
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontSmall
                }
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0; to: 1
                    value: Math.max(0, dialog.dub.progress)
                    indeterminate: dialog.dub.progress < 0
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: !dialog.dub.running && (dialog.dub.error.length > 0 || dialog.dub.notice.length > 0)
                implicitHeight: noticeText.implicitHeight + 16
                radius: StudioTheme.radius
                color: StudioTheme.alpha(dialog.dub.error.length > 0 ? StudioTheme.danger : StudioTheme.warning, 0.12)
                border.width: 1
                border.color: StudioTheme.alpha(dialog.dub.error.length > 0 ? StudioTheme.danger : StudioTheme.warning, 0.5)
                Text {
                    id: noticeText
                    anchors.fill: parent
                    anchors.margins: 8
                    wrapMode: Text.WordWrap
                    text: (dialog.dub.error.length > 0 ? dialog.dub.error + (dialog.dub.notice.length > 0 ? "\n" : "") : "") + dialog.dub.notice
                    color: StudioTheme.text
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontSmall
                }
            }

            // ---------------------------------------------------- results
            Heading { visible: dialog.dub.results.length > 0; text: qsTr("Ready"); Layout.topMargin: 4 }
            Repeater {
                model: dialog.dub.results
                Rectangle {
                    id: row
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: rowLayout.implicitHeight + 16
                    radius: StudioTheme.radius
                    color: StudioTheme.raised
                    border.width: 1
                    border.color: StudioTheme.success
                    RowLayout {
                        id: rowLayout
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: row.modelData.name + " · " + row.modelData.native
                                color: StudioTheme.text
                                font.family: StudioTheme.font
                                font.pixelSize: StudioTheme.fontBody
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                text: row.modelData.path
                                elide: Text.ElideMiddle
                                color: StudioTheme.textSecondary
                                font.family: StudioTheme.font
                                font.pixelSize: StudioTheme.fontTiny
                            }
                        }
                        StudioButton {
                            text: qsTr("Add to timeline")
                            flat: false
                            size: 24
                            fontSize: StudioTheme.fontSmall
                            tip: qsTr("Puts the dub on a new audio track so you can hear it with the picture")
                            onClicked: dialog.dub.addToTimeline(row.modelData.code)
                        }
                    }
                }
            }
            Body {
                visible: dialog.dub.results.length > 0
                text: qsTr("To publish: YouTube Studio › Content › your video › Languages › Add language › Add dub, and upload the MP3 (or the WAV). YouTube may ask you to say that the audio is AI-generated; check its current rules. Chatterbox marks its audio with an inaudible watermark.")
            }
        }
    }

    footer: RowLayout {
        spacing: 8
        Text {
            Layout.leftMargin: 14
            Layout.fillWidth: true
            text: qsTr("The text is translated by the AI model chosen in Settings › AI. Nothing leaves this computer.")
            elide: Text.ElideRight
            color: StudioTheme.textSecondary
            font.family: StudioTheme.font
            font.pixelSize: StudioTheme.fontSmall
        }
        StudioButton {
            visible: dialog.dub.results.length > 0 && !dialog.dub.running
            text: qsTr("Open folder")
            iconName: "folder"
            flat: false
            Layout.bottomMargin: 12
            onClicked: dialog.dub.openFolder()
        }
        StudioButton {
            text: dialog.dub.running ? qsTr("Cancel") : qsTr("Close")
            flat: false
            Layout.bottomMargin: 12
            onClicked: dialog.dub.running ? dialog.dub.cancel() : dialog.close()
        }
        StudioButton {
            text: dialog.dub.results.length > 0 ? qsTr("Dub again") : qsTr("Dub")
            iconName: "mic"
            primary: true
            Layout.rightMargin: 14
            Layout.bottomMargin: 12
            enabled: !dialog.dub.running && dialog.dub.engineReady && dialog.dub.targets.length > 0 && editor.timeline.duration > 1
            onClicked: dialog.dub.start()
        }
    }
}
