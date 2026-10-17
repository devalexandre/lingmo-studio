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

// Media Pool: imported media as thumbnails (grid) or rows (list), with
// spoken-word search. Click shows the clip in the source viewer; drag it
// onto a track; double-click appends it.
StudioPanel {
    id: bin

    property bool listMode: false
    property bool searching: false
    property int thumbWidth: 150
    signal importRequested()
    signal chosen(var info)

    title: qsTr("Media Pool")
    subtitle: editor.mediaBin.count > 0 ? qsTr("%n item(s)", "", editor.mediaBin.count) : ""

    actions: [
        LingmoUI.Spinner {
            running: editor.mediaBin.importing > 0
            visible: running
            size: 14
        },
        StudioButton { iconName: "search"; tip: qsTr("Find a moment by its words"); checkable: true; checked: bin.searching; onClicked: bin.searching = !bin.searching },
        StudioButton { iconName: "grid"; tip: qsTr("Thumbnails"); checkable: true; checked: !bin.listMode; onClicked: bin.listMode = false },
        StudioButton { iconName: "list"; tip: qsTr("List"); checkable: true; checked: bin.listMode; onClicked: bin.listMode = true },
        StudioButton { iconName: "import"; tip: qsTr("Import media"); shortcut: "Ctrl+I"; onClicked: bin.importRequested() }
    ]

    function choose(cell) {
        editor.mediaBin.openInSource(cell.mediaId)
        bin.chosen({ mediaId: cell.mediaId, name: cell.name, kind: cell.kind, durationText: cell.durationText,
                     info: cell.info, proxyState: cell.proxyState, used: cell.used })
    }

    Menu {
        id: itemMenu
        property string mediaId: ""
        property string proxyState: "none"
        property bool video: false
        MenuItem { text: qsTr("Append to timeline"); onTriggered: editor.appendMedia(itemMenu.mediaId) }
        MenuItem { text: qsTr("Open in source monitor"); onTriggered: editor.mediaBin.openInSource(itemMenu.mediaId) }
        MenuItem { text: qsTr("Generate cuts for Shorts/Reels…"); enabled: itemMenu.video; onTriggered: root.openShorts(itemMenu.mediaId) }
        MenuSeparator {}
        MenuItem {
            text: itemMenu.proxyState === "ready" ? qsTr("Delete proxy") : itemMenu.proxyState === "running" ? qsTr("Cancel proxy") : qsTr("Make proxy")
            enabled: itemMenu.video
            onTriggered: {
                if (itemMenu.proxyState === "ready")
                    editor.mediaBin.deleteProxy(itemMenu.mediaId)
                else if (itemMenu.proxyState === "running")
                    editor.mediaBin.cancelProxy(itemMenu.mediaId)
                else
                    editor.mediaBin.generateProxy(itemMenu.mediaId)
            }
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Remove from project"); onTriggered: editor.mediaBin.remove(itemMenu.mediaId) }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        SearchPanel {
            visible: bin.searching
            Layout.fillWidth: true
            Layout.preferredHeight: parent.height * 0.5
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            readonly property int columns: bin.listMode ? 1 : Math.max(1, Math.floor(width / bin.thumbWidth))
            cellWidth: width / columns
            cellHeight: bin.listMode ? 44 : cellWidth * 9 / 16 + 34
            model: editor.mediaBin
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                id: cell
                required property int index
                required property string mediaId
                required property string name
                required property string kind
                required property string durationText
                required property string info
                required property string proxyState
                required property real proxyProgress
                required property bool used
                readonly property bool current: editor.player.sourceMediaId === cell.mediaId
                width: grid.cellWidth
                height: grid.cellHeight

                Rectangle {
                    id: card
                    anchors.fill: parent
                    anchors.margins: 3
                    radius: StudioTheme.radius - 2
                    color: dragArea.containsMouse ? StudioTheme.raisedHover : (cell.current ? StudioTheme.raised : "transparent")
                    border.width: cell.current ? 1 : 0
                    border.color: StudioTheme.accent
                    Behavior on color { ColorAnimation { duration: StudioTheme.fast } }

                    // thumbnail
                    Rectangle {
                        id: thumbBox
                        x: 4
                        y: 4
                        width: bin.listMode ? 56 : parent.width - 8
                        height: bin.listMode ? parent.height - 8 : width * 9 / 16
                        radius: StudioTheme.smallRadius
                        color: StudioTheme.well
                        clip: true
                        Image {
                            anchors.fill: parent
                            visible: cell.kind !== "audio"
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.width: 256
                            source: cell.kind !== "audio" ? "image://thumb/" + cell.mediaId + "/" + (cell.kind === "image" ? 0 : 30) : ""
                        }
                        Rectangle {
                            anchors.fill: parent
                            visible: cell.kind === "audio"
                            color: StudioTheme.audioClip
                            Image {
                                anchors.fill: parent
                                anchors.margins: 4
                                asynchronous: true
                                sourceSize: Qt.size(240, 60)
                                source: cell.kind === "audio" ? "image://wave/" + cell.mediaId + "/0/100000000/" + String(StudioTheme.audioWave).substring(1, 7) : ""
                            }
                        }
                        // duration
                        Rectangle {
                            visible: !bin.listMode && cell.durationText.length > 0
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 4
                            width: durText.implicitWidth + 8
                            height: 16
                            radius: 3
                            color: StudioTheme.alpha(StudioTheme.window, 0.82)
                            Text {
                                id: durText
                                anchors.centerIn: parent
                                text: cell.durationText
                                color: StudioTheme.text
                                font.family: StudioTheme.font
                                font.pixelSize: StudioTheme.fontTiny
                                font.features: { "tnum": 1 }
                            }
                        }
                        // proxy
                        Rectangle {
                            visible: cell.proxyState === "ready" || cell.proxyState === "running"
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.margins: 4
                            width: proxyText.implicitWidth + 8
                            height: 15
                            radius: 3
                            color: StudioTheme.warning
                            Text {
                                id: proxyText
                                anchors.centerIn: parent
                                text: cell.proxyState === "ready" ? qsTr("proxy") : Math.round(cell.proxyProgress * 100) + "%"
                                color: "#16171A"
                                font.family: StudioTheme.font
                                font.pixelSize: StudioTheme.fontTiny
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                    // in use on the timeline
                    Rectangle {
                        visible: cell.used
                        x: thumbBox.x + thumbBox.width - 10
                        y: thumbBox.y + 4
                        width: 6
                        height: 6
                        radius: 3
                        color: StudioTheme.success
                    }
                    ColumnLayout {
                        anchors.left: bin.listMode ? thumbBox.right : parent.left
                        anchors.right: parent.right
                        anchors.top: bin.listMode ? parent.top : thumbBox.bottom
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: bin.listMode ? 8 : 6
                        anchors.rightMargin: 6
                        anchors.topMargin: bin.listMode ? 5 : 3
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: cell.name
                            elide: Text.ElideMiddle
                            color: cell.current ? StudioTheme.text : StudioTheme.text
                            font.family: StudioTheme.font
                            font.pixelSize: StudioTheme.fontSmall
                            font.weight: cell.current ? Font.DemiBold : Font.Normal
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: bin.listMode || grid.cellWidth > 170
                            text: bin.listMode ? cell.durationText + " · " + cell.info : cell.info
                            elide: Text.ElideRight
                            color: StudioTheme.textSecondary
                            font.family: StudioTheme.font
                            font.pixelSize: StudioTheme.fontTiny
                        }
                        Item { Layout.fillHeight: true }
                    }
                }

                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    drag.target: dragProxy
                    onPressed: function(m) {
                        if (m.button === Qt.RightButton) {
                            itemMenu.mediaId = cell.mediaId
                            itemMenu.proxyState = cell.proxyState
                            itemMenu.video = cell.kind === "video"
                            itemMenu.popup()
                        }
                    }
                    onClicked: function(m) { if (m.button === Qt.LeftButton) bin.choose(cell) }
                    onDoubleClicked: editor.appendMedia(cell.mediaId)
                    ToolTip.visible: containsMouse && !pressed
                    ToolTip.text: cell.name + "\n" + cell.info
                    ToolTip.delay: 900
                }
                Item {
                    id: dragProxy
                    width: 1
                    height: 1
                    Drag.active: dragArea.drag.active
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction
                    Drag.mimeData: { "studio/media": cell.mediaId }
                    Drag.onDragFinished: { dragProxy.x = 0; dragProxy.y = 0 }
                }
            }
        }
    }

    // empty: a drop zone
    Rectangle {
        anchors.fill: parent
        anchors.margins: 10
        visible: editor.mediaBin.count === 0 && editor.mediaBin.importing === 0
        radius: StudioTheme.radius
        color: "transparent"
        border.width: 1
        border.color: StudioTheme.border
        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width - 32
            spacing: 8
            StudioIcon { Layout.alignment: Qt.AlignHCenter; name: "import"; size: 32; color: StudioTheme.textSecondary }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("No media yet")
                color: StudioTheme.text
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontLarge
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("Drop videos, music and pictures here, or import them.")
                color: StudioTheme.textSecondary
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontBody
            }
            StudioButton {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Import media")
                iconName: "import"
                flat: false
                onClicked: bin.importRequested()
            }
        }
    }
}
