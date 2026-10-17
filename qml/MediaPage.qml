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

// Media page: bring media in and look at it. Import on the left, the source
// viewer in the middle, the clip's details on the right, and a large Media
// Pool below.
SplitView {
    id: page
    orientation: Qt.Vertical
    handle: PanelHandle { }

    property var info: ({})
    readonly property bool hasInfo: info.mediaId !== undefined && info.mediaId === editor.player.sourceMediaId

    FileDialog {
        id: srtOpen
        title: qsTr("Import subtitles")
        nameFilters: [qsTr("Subtitles (*.srt)")]
        onAccepted: editor.importSubtitles(selectedFile)
    }

    SplitView {
        SplitView.fillHeight: true
        SplitView.minimumHeight: 280
        orientation: Qt.Horizontal
        handle: PanelHandle { }

        StudioPanel {
            title: qsTr("Import")
            SplitView.preferredWidth: 300
            SplitView.minimumWidth: 240

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: StudioTheme.pad
                spacing: 8

                // drop zone
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 130
                    radius: StudioTheme.radius
                    color: dropMouse.containsMouse ? StudioTheme.raised : StudioTheme.well
                    border.width: 1
                    border.color: dropMouse.containsMouse ? StudioTheme.accent : StudioTheme.border
                    Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
                    Behavior on border.color { ColorAnimation { duration: StudioTheme.fast } }
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: parent.width - 24
                        spacing: 6
                        StudioIcon { Layout.alignment: Qt.AlignHCenter; name: "import"; size: 26; color: dropMouse.containsMouse ? StudioTheme.accent : StudioTheme.textSecondary }
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: qsTr("Drop videos, music and pictures here, or click to choose files")
                            color: StudioTheme.textSecondary
                            font.family: StudioTheme.font
                            font.pixelSize: StudioTheme.fontSmall
                        }
                    }
                    MouseArea {
                        id: dropMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.importMedia()
                    }
                }
                StudioButton { Layout.fillWidth: true; text: qsTr("Import media…"); iconName: "import"; flat: false; shortcut: "Ctrl+I"; tip: qsTr("Import media"); onClicked: root.importMedia() }
                StudioButton { Layout.fillWidth: true; text: qsTr("Import subtitles (SRT)…"); iconName: "captions"; flat: false; onClicked: srtOpen.open() }
                StudioButton { Layout.fillWidth: true; text: qsTr("Open video and generate cuts"); iconName: "shorts"; primary: true; onClicked: root.openShorts(page.hasInfo && page.info.kind === "video" ? page.info.mediaId : "") }

                Item { Layout.fillHeight: true }

                // project at a glance
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    radius: StudioTheme.radius
                    color: StudioTheme.raised
                    GridLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        columns: 2
                        rowSpacing: 2
                        columnSpacing: 10
                        Repeater {
                            model: [
                                { k: qsTr("Media"), v: editor.mediaBin.count },
                                { k: qsTr("Length"), v: editor.formatDuration(editor.timeline.duration) },
                                { k: qsTr("Format"), v: editor.projectWidth + "×" + editor.projectHeight },
                                { k: qsTr("Frame rate"), v: Number(editor.fps).toLocaleString(Qt.locale(), 'f', editor.fps % 1 ? 2 : 0) + " fps" }
                            ]
                            Text {
                                Layout.fillWidth: true
                                text: modelData.k + "  <font color='" + StudioTheme.text + "'>" + modelData.v + "</font>"
                                textFormat: Text.StyledText
                                color: StudioTheme.textSecondary
                                font.family: StudioTheme.font
                                font.pixelSize: StudioTheme.fontSmall
                            }
                        }
                    }
                }
            }
        }

        Viewer {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 420
            role: "source"
            showEditButtons: true
        }

        StudioPanel {
            title: qsTr("Metadata")
            icon: "info"
            SplitView.preferredWidth: 300
            SplitView.minimumWidth: 220

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: StudioTheme.pad
                spacing: 10
                visible: page.hasInfo

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: width * 9 / 16
                    radius: StudioTheme.radius
                    color: StudioTheme.well
                    clip: true
                    Image {
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        source: page.hasInfo && page.info.kind !== "audio" ? "image://thumb/" + page.info.mediaId + "/" + (page.info.kind === "image" ? 0 : 30) : ""
                    }
                    StudioIcon { anchors.centerIn: parent; visible: page.info.kind === "audio"; name: "music"; size: 32; color: StudioTheme.trackAudio }
                }
                Text {
                    Layout.fillWidth: true
                    text: page.info.name || ""
                    color: StudioTheme.text
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontLarge
                    font.weight: Font.DemiBold
                    wrapMode: Text.WrapAnywhere
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 6
                    Repeater {
                        model: [
                            { k: qsTr("Type"), v: ({ video: qsTr("Video"), audio: qsTr("Audio"), image: qsTr("Image") })[page.info.kind] || "" },
                            { k: qsTr("Length"), v: page.info.durationText || "—" },
                            { k: qsTr("Details"), v: page.info.info || "" },
                            { k: qsTr("Proxy"), v: page.info.proxyState === "ready" ? qsTr("Ready") : page.info.proxyState === "running" ? qsTr("Making…") : qsTr("None") },
                            { k: qsTr("On the timeline"), v: page.info.used ? qsTr("Yes") : qsTr("No") }
                        ]
                        delegate: Item {
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            implicitHeight: Math.max(kText.implicitHeight, vText.implicitHeight)
                            Text {
                                id: kText
                                width: 96
                                text: modelData.k
                                color: StudioTheme.textSecondary
                                font.family: StudioTheme.font
                                font.pixelSize: StudioTheme.fontSmall
                            }
                            Text {
                                id: vText
                                x: 100
                                width: parent.width - 100
                                text: modelData.v
                                color: StudioTheme.text
                                wrapMode: Text.WordWrap
                                font.family: StudioTheme.font
                                font.pixelSize: StudioTheme.fontSmall
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                StudioButton {
                    Layout.fillWidth: true
                    text: qsTr("Append to timeline")
                    iconName: "append"
                    flat: false
                    onClicked: editor.appendMedia(page.info.mediaId)
                }
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - 40
                visible: !page.hasInfo
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("Select a clip in the media pool to see its details")
                color: StudioTheme.textSecondary
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontBody
            }
        }
    }

    MediaBinPanel {
        SplitView.preferredHeight: 330
        SplitView.minimumHeight: 160
        thumbWidth: 190
        onImportRequested: root.importMedia()
        onChosen: function(info) { page.info = info }
    }
}
