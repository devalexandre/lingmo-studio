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
import Lingmo.Studio 1.0
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// "Gerar cortes": pick a long video and the platforms, get scored highlight
// cards, play/trim/select them, and turn them into vertical sequences.
Item {
    id: page
    signal back()

    readonly property var shorts: editor.shorts
    readonly property var studioEditor: editor
    property string playingId: ""
    readonly property var mediaList: editor.mediaBin.count >= 0 && visible ? shorts.mediaChoices() : []

    onVisibleChanged: {
        if (!visible) {
            playingId = ""
            shorts.stopPreview()
        } else if (shorts.mediaId === "" && mediaList.length > 0) {
            shorts.mediaId = mediaList[mediaList.length - 1].id
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("Choose a long video")
        fileMode: FileDialog.OpenFile
        nameFilters: editor.mediaNameFilters()
        onAccepted: editor.importUrls([selectedFile])
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Export the cuts to")
        onAccepted: page.shorts.exportCuts(selectedFolder)
    }
    Connections {
        target: editor.mediaBin
        function onCountChanged() {
            const list = page.shorts.mediaChoices()
            if (list.length > 0 && page.visible)
                page.shorts.mediaId = list[list.length - 1].id // the one just imported
        }
    }
    Connections {
        target: editor.player
        function onPlayingChanged() { if (!editor.player.playing) page.playingId = "" }
    }

    RowLayout {
        anchors.fill: parent
        spacing: LingmoUI.Units.smallSpacing

        // ------------------------------------------------ setup
        LingmoUI.Card {
            Layout.preferredWidth: 380
            Layout.fillHeight: true

            ScrollView {
                id: setupScroll
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.largeSpacing
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    width: setupScroll.availableWidth
                    spacing: LingmoUI.Units.smallSpacing

                    LingmoUI.PageHeader {
                        Layout.fillWidth: true
                        title: qsTr("Generate cuts")
                        subtitle: qsTr("Shorts, Reels and TikToks from a long video")
                        backVisible: true
                        onBack: page.back()
                    }

                    Label { text: qsTr("Video"); font.pointSize: LingmoUI.Units.fontBody; font.bold: true; color: LingmoUI.Theme.textColor }
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            id: mediaBox
                            Layout.fillWidth: true
                            model: page.mediaList
                            textRole: "name"
                            valueRole: "id"
                            displayText: page.mediaList.length ? currentText : qsTr("No video in the project")
                            currentIndex: indexOfValue(page.shorts.mediaId)
                            onActivated: page.shorts.mediaId = currentValue
                        }
                        LingmoUI.IconButton { iconName: "document-open"; tipText: qsTr("Import a video"); onClicked: importDialog.open() }
                    }
                    Label {
                        visible: mediaBox.currentIndex >= 0 && page.mediaList.length > 0
                        text: visible ? qsTr("Length %1").arg(page.mediaList[Math.max(0, mediaBox.currentIndex)].duration) : ""
                        font.pointSize: LingmoUI.Units.fontCaption
                        color: LingmoUI.Theme.secondaryTextColor
                    }

                    Label { text: qsTr("Platforms"); font.pointSize: LingmoUI.Units.fontBody; font.bold: true; color: LingmoUI.Theme.textColor; Layout.topMargin: LingmoUI.Units.smallSpacing }
                    Flow {
                        Layout.fillWidth: true
                        spacing: LingmoUI.Units.smallSpacing
                        Repeater {
                            model: page.shorts.platforms
                            LingmoUI.Card {
                                readonly property bool on: page.shorts.selectedPlatforms.indexOf(modelData.id) >= 0
                                width: (setupScroll.availableWidth - LingmoUI.Units.smallSpacing) / 2
                                height: 52
                                clickable: true
                                highlighted: on
                                radius: LingmoUI.Theme.smallRadius
                                onClicked: page.shorts.togglePlatform(modelData.id)
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: LingmoUI.Units.largeSpacing
                                    anchors.rightMargin: LingmoUI.Units.smallSpacing
                                    spacing: 0
                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        font.pointSize: LingmoUI.Units.fontBody
                                        font.bold: parent.parent.on
                                        color: LingmoUI.Theme.textColor
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("%1 · up to %2").arg(modelData.aspect).arg(page.shorts.formatTime(modelData.maxSeconds * 1000))
                                        font.pointSize: LingmoUI.Units.fontCaption
                                        color: LingmoUI.Theme.secondaryTextColor
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("Cuts of %1 to %2 seconds, on sentence boundaries.").arg(page.shorts.minSeconds).arg(page.shorts.maxSeconds)
                        font.pointSize: LingmoUI.Units.fontCaption
                        color: LingmoUI.Theme.secondaryTextColor
                    }
                    LingmoUI.Banner {
                        Layout.fillWidth: true
                        type: LingmoUI.Banner.Info
                        closable: false
                        visible: page.shorts.mixedAspects
                        text: visible ? qsTr("These platforms use different shapes. Cuts are edited in the first one's shape; the export reframes each file for its platform.") : ""
                    }

                    Label { text: qsTr("Options"); font.pointSize: LingmoUI.Units.fontBody; font.bold: true; color: LingmoUI.Theme.textColor; Layout.topMargin: LingmoUI.Units.smallSpacing }
                    RowLayout {
                        Label { text: qsTr("How many cuts"); Layout.fillWidth: true; font.pointSize: LingmoUI.Units.fontBody; color: LingmoUI.Theme.textColor }
                        SpinBox { from: 1; to: 20; value: page.shorts.count; onValueModified: page.shorts.count = value }
                    }
                    Switch {
                        Layout.fillWidth: true
                        text: qsTr("Captions synced to the speech")
                        checked: page.shorts.captions
                        onToggled: page.shorts.captions = checked
                    }
                    ComboBox {
                        Layout.fillWidth: true
                        visible: page.shorts.captions
                        model: editor.captionStyles
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: indexOfValue(page.shorts.captionStyle)
                        onActivated: page.shorts.captionStyle = currentValue
                    }
                    Switch {
                        Layout.fillWidth: true
                        text: qsTr("Reframe following faces")
                        checked: page.shorts.reframe
                        onToggled: page.shorts.reframe = checked
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: page.shorts.reframe && !page.shorts.opencvAvailable
                        wrapMode: Text.WordWrap
                        text: qsTr("Face tracking needs OpenCV for Python (python-opencv). Without it the crop stays centered; move it with Position X keyframes.")
                        font.pointSize: LingmoUI.Units.fontCaption
                        color: LingmoUI.Theme.secondaryTextColor
                    }

                    TranscriptionStatus { Layout.fillWidth: true; Layout.topMargin: LingmoUI.Units.smallSpacing }

                    LingmoUI.ActionButton {
                        Layout.fillWidth: true
                        Layout.topMargin: LingmoUI.Units.smallSpacing
                        text: page.shorts.candidates.length ? qsTr("Find cuts again") : qsTr("Find the best moments")
                        iconName: "edit-find"
                        primary: true
                        busy: page.shorts.running
                        enabled: editor.transcriber.available && page.shorts.mediaId !== "" && page.shorts.selectedPlatforms.length > 0
                        onClicked: page.shorts.analyze()
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: page.shorts.running
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: page.shorts.stage
                            font.pointSize: LingmoUI.Units.fontCaption
                            color: LingmoUI.Theme.secondaryTextColor
                            elide: Text.ElideRight
                        }
                        RowLayout {
                            ProgressBar {
                                Layout.fillWidth: true
                                from: 0; to: 1
                                value: Math.max(0, page.shorts.progress)
                                indeterminate: page.shorts.progress < 0
                            }
                            LingmoUI.ActionButton { text: qsTr("Cancel"); onClicked: page.shorts.cancel() }
                        }
                    }
                    LingmoUI.Banner {
                        Layout.fillWidth: true
                        type: LingmoUI.Banner.Error
                        visible: page.shorts.error.length > 0 && !page.shorts.running
                        text: visible ? page.shorts.error : ""
                    }
                }
            }
        }

        // ------------------------------------------------ results
        LingmoUI.Card {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.largeSpacing
                spacing: LingmoUI.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: page.shorts.candidates.length ? qsTr("Suggested cuts") : qsTr("Cuts")
                        font.pointSize: LingmoUI.Units.fontHeading
                        font.bold: true
                        color: LingmoUI.Theme.textColor
                    }
                    LingmoUI.Badge {
                        visible: page.shorts.candidates.length > 0
                        text: page.shorts.usedLlm ? qsTr("Scored by %1 (local)").arg(page.shorts.llmModel) : qsTr("Scored by sound, speech and scenes")
                        subtle: true
                    }
                }

                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: LingmoUI.Units.smallSpacing
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    model: page.shorts.candidates
                    delegate: CandidateCard {
                        width: list.width - LingmoUI.Units.smallSpacing
                        cut: modelData
                        playing: page.playingId === modelData.id && editor.player.playing
                        onPlayRequested: {
                            if (playing) {
                                editor.player.pause()
                            } else {
                                page.playingId = modelData.id
                                page.shorts.preview(modelData.id)
                            }
                        }
                    }
                    LingmoUI.WheelHandler { target: list }

                    LingmoUI.EmptyState {
                        anchors.centerIn: parent
                        width: parent.width - LingmoUI.Units.largeSpacing * 4
                        visible: list.count === 0 && !page.shorts.running
                        iconName: "view-media-playlist"
                        title: page.mediaList.length ? qsTr("Ready to find the best moments") : qsTr("Bring a long video")
                        text: page.mediaList.length
                              ? qsTr("Lingmo Studio transcribes the video on this computer, splits it on sentences and scores each moment. You pick, trim and turn them into vertical clips.")
                              : qsTr("Import an interview, a class, a podcast or a live stream. Everything runs on this computer.")
                        actionText: page.mediaList.length ? "" : qsTr("Import video…")
                        onActionTriggered: importDialog.open()
                    }
                    LingmoUI.Spinner {
                        anchors.centerIn: parent
                        running: page.shorts.running && list.count === 0
                        visible: running
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: page.shorts.candidates.length > 0 || editor.shortSequences.length > 0
                    Label {
                        Layout.fillWidth: true
                        text: page.shorts.tracking ? qsTr("Following faces for the vertical crop…")
                                                   : qsTr("%n cut(s) selected", "", page.shorts.selectedCount)
                        font.pointSize: LingmoUI.Units.fontCaption
                        color: LingmoUI.Theme.secondaryTextColor
                    }
                    LingmoUI.ActionButton {
                        text: qsTr("Export all cuts…")
                        iconName: "document-export"
                        enabled: editor.shortSequences.length > 0
                        onClicked: folderDialog.open()
                    }
                    LingmoUI.ActionButton {
                        text: qsTr("Create %n cut(s)", "", page.shorts.selectedCount)
                        iconName: "edit-cut"
                        primary: true
                        enabled: page.shorts.selectedCount > 0 && !page.shorts.running
                        onClicked: {
                            if (page.shorts.createCuts() > 0)
                                page.back()
                        }
                    }
                }
            }
        }

        // ------------------------------------------------ preview (vertical, like the result)
        LingmoUI.Card {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            visible: page.width > 1180
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.largeSpacing
                spacing: LingmoUI.Units.smallSpacing
                Label { text: qsTr("Preview"); font.pointSize: LingmoUI.Units.fontBody; font.bold: true; color: LingmoUI.Theme.textColor }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: width * 16 / 9
                    radius: LingmoUI.Theme.smallRadius
                    color: Qt.rgba(0, 0, 0, 1) // video surround is always black
                    clip: true
                    VideoView {
                        anchors.fill: parent
                        editor: page.studioEditor
                        visible: page.playingId !== ""
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - LingmoUI.Units.largeSpacing * 2
                        visible: page.playingId === ""
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Press play on a cut to watch it here")
                        font.pointSize: LingmoUI.Units.fontCaption
                        color: LingmoUI.Theme.secondaryTextColor
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
