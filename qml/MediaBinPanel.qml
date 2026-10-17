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

// Imported media: thumbnails, duration and codecs, proxies. Click shows the
// clip in the source monitor; drag it onto a track; double-click appends it.
Item {
    id: bin
    signal importRequested()

    Menu {
        id: itemMenu
        property string mediaId: ""
        property string proxyState: "none"
        property bool video: false
        MenuItem { text: qsTr("Append to timeline"); onTriggered: editor.appendMedia(itemMenu.mediaId) }
        MenuItem { text: qsTr("Open in source monitor"); onTriggered: editor.mediaBin.openInSource(itemMenu.mediaId) }
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
        spacing: LingmoUI.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: LingmoUI.Units.smallSpacing
            Layout.rightMargin: LingmoUI.Units.smallSpacing
            LingmoUI.ActionButton {
                text: qsTr("Import")
                iconName: "document-import"
                onClicked: bin.importRequested()
            }
            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("%n item(s)", "", editor.mediaBin.count)
                color: LingmoUI.Theme.secondaryTextColor
                font.pointSize: LingmoUI.Units.fontCaption
            }
            LingmoUI.Spinner {
                running: editor.mediaBin.importing > 0
                visible: running
                size: LingmoUI.Units.iconSizes.small
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Math.max(120, width / Math.max(1, Math.floor(width / 140)))
            cellHeight: cellWidth * 0.56 + 44
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
                width: grid.cellWidth
                height: grid.cellHeight

                LingmoUI.Card {
                    id: card
                    anchors.fill: parent
                    anchors.margins: 3
                    hoverEnabled: true
                    highlighted: editor.player.sourceMediaId === cell.mediaId && editor.player.mode === "source"
                    radius: LingmoUI.Theme.smallRadius

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 2

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: LingmoUI.Theme.tinyRadius
                            color: LingmoUI.Theme.alternateBackgroundColor
                            clip: true
                            Image {
                                id: thumb
                                anchors.fill: parent
                                visible: cell.kind !== "audio"
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                sourceSize.width: 256
                                source: cell.kind !== "audio" ? "image://thumb/" + cell.mediaId + "/" + (cell.kind === "image" ? 0 : 30) : ""
                            }
                            Image {
                                anchors.fill: parent
                                anchors.margins: 6
                                visible: cell.kind === "audio"
                                asynchronous: true
                                sourceSize: Qt.size(240, 60)
                                source: cell.kind === "audio" ? "image://wave/" + cell.mediaId + "/0/100000000/" + String(LingmoUI.Theme.greenColor).substring(1, 7) : ""
                            }
                            Label {
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 3
                                visible: cell.durationText.length > 0
                                text: cell.durationText
                                color: LingmoUI.Theme.textColor
                                font.pointSize: LingmoUI.Units.fontCaption
                                padding: 2
                                background: Rectangle { color: LingmoUI.Theme.backgroundColor; opacity: 0.8; radius: 3 }
                            }
                            LingmoUI.Badge {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: 3
                                text: cell.proxyState === "ready" ? qsTr("proxy") : cell.proxyState === "running" ? Math.round(cell.proxyProgress * 100) + "%" : ""
                                tone: LingmoUI.Theme.orangeColor
                            }
                            LingmoUI.Badge {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 3
                                dot: true
                                text: cell.used ? "1" : ""
                                tone: LingmoUI.Theme.greenColor
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: cell.name
                            elide: Text.ElideMiddle
                            font.pointSize: LingmoUI.Units.fontCaption
                            color: LingmoUI.Theme.textColor
                        }
                        Label {
                            Layout.fillWidth: true
                            text: cell.info
                            elide: Text.ElideRight
                            font.pointSize: LingmoUI.Units.fontCaption - 1
                            color: LingmoUI.Theme.secondaryTextColor
                        }
                    }
                }

                MouseArea {
                    id: dragArea
                    anchors.fill: parent
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
                    onClicked: function(m) { if (m.button === Qt.LeftButton) editor.mediaBin.openInSource(cell.mediaId) }
                    onDoubleClicked: editor.appendMedia(cell.mediaId)
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

    LingmoUI.EmptyState {
        anchors.centerIn: parent
        width: parent.width - LingmoUI.Units.largeSpacing * 2
        visible: editor.mediaBin.count === 0 && editor.mediaBin.importing === 0
        iconName: "folder-videos"
        iconSize: LingmoUI.Units.iconSizes.large
        title: qsTr("No media yet")
        text: qsTr("Drop videos, music and pictures here, or import them.")
        actionText: qsTr("Import media")
        onActionTriggered: bin.importRequested()
    }
}
