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

// The program's picture clips in record order as numbered thumbnails,
// joined like nodes. Click one to select it and move the playhead there.
Rectangle {
    id: strip

    property var clips: []
    readonly property string selectedId: editor.selectedClip && editor.selectedClip.id !== undefined ? editor.selectedClip.id : ""

    implicitHeight: 96
    color: StudioTheme.panel
    radius: StudioTheme.radius
    border.width: 1
    border.color: StudioTheme.border

    function rebuild() {
        const tl = editor.timeline
        const list = []
        for (let t = 0; t < tl.trackCount; ++t) {
            if (tl.trackKind(t) !== "video")
                continue
            const name = tl.data(tl.index(t, 0), 258) || ""
            const cs = tl.clipsOf(t)
            for (let i = 0; i < cs.length; ++i)
                list.push({ id: cs[i].id, name: cs[i].name || "", track: name, position: cs[i].position, duration: cs[i].duration,
                            mediaId: cs[i].mediaId, mediaKind: cs[i].mediaKind, in: cs[i]["in"], effects: cs[i].effects })
        }
        list.sort(function(a, b) { return a.position - b.position || a.track.localeCompare(b.track) })
        clips = list
    }
    Component.onCompleted: rebuild()
    Connections {
        target: editor.timeline
        function onChanged() { Qt.callLater(strip.rebuild) }
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.margins: 8
        orientation: ListView.Horizontal
        spacing: 18
        clip: true
        model: strip.clips
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: Item {
            id: node
            required property int index
            required property var modelData
            readonly property bool current: modelData.id === strip.selectedId
            width: 112
            height: list.height

            // connector to the next clip
            Rectangle {
                visible: node.index < strip.clips.length - 1
                x: parent.width
                y: thumb.y + thumb.height / 2
                width: list.spacing
                height: 2
                color: StudioTheme.border
            }
            Rectangle {
                id: thumb
                width: parent.width
                height: 63
                radius: StudioTheme.smallRadius + 2
                color: StudioTheme.well
                border.width: node.current ? 2 : 1
                border.color: node.current ? StudioTheme.accent : nodeMouse.containsMouse ? StudioTheme.textSecondary : StudioTheme.border
                clip: true
                Behavior on border.color { ColorAnimation { duration: StudioTheme.fast } }
                Image {
                    anchors.fill: parent
                    anchors.margins: parent.border.width
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: 224
                    visible: node.modelData.mediaKind === "video" || node.modelData.mediaKind === "image"
                    source: visible ? "image://thumb/" + node.modelData.mediaId + "/" + (node.modelData.mediaKind === "image" ? 0 : Math.floor((node.modelData.in + node.modelData.duration / 2) / 15) * 15) : ""
                }
                Text {
                    anchors.centerIn: parent
                    width: parent.width - 10
                    visible: node.modelData.mediaKind !== "video" && node.modelData.mediaKind !== "image"
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: node.modelData.name
                    color: StudioTheme.textSecondary
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontSmall
                }
                // clip number
                Rectangle {
                    x: 4
                    y: 4
                    width: num.implicitWidth + 8
                    height: 15
                    radius: 3
                    color: node.current ? StudioTheme.accent : StudioTheme.alpha(StudioTheme.window, 0.8)
                    Text {
                        id: num
                        anchors.centerIn: parent
                        text: (node.index + 1 < 10 ? "0" : "") + (node.index + 1)
                        color: "#FFFFFF"
                        font.family: StudioTheme.font
                        font.pixelSize: StudioTheme.fontTiny
                        font.weight: Font.DemiBold
                        font.features: { "tnum": 1 }
                    }
                }
                // graded / has effects
                Rectangle {
                    visible: node.modelData.effects > 0
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 5
                    width: 8
                    height: 8
                    radius: 4
                    color: "#D8606A"
                }
            }
            Text {
                anchors.top: thumb.bottom
                anchors.topMargin: 3
                width: parent.width
                text: editor.timecode(node.modelData.position) + "  " + node.modelData.track
                color: node.current ? StudioTheme.text : StudioTheme.textSecondary
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontTiny
                font.features: { "tnum": 1 }
                elide: Text.ElideRight
            }
            MouseArea {
                id: nodeMouse
                anchors.fill: thumb
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (editor.player.mode !== "timeline")
                        editor.player.setMode("timeline")
                    editor.player.seek(node.modelData.position)
                    editor.select(node.modelData.id)
                }
                ToolTip.visible: containsMouse
                ToolTip.text: node.modelData.name
                ToolTip.delay: 600
            }
        }
    }
    Text {
        anchors.centerIn: parent
        visible: strip.clips.length === 0
        text: qsTr("The timeline has no picture clips yet")
        color: StudioTheme.textSecondary
        font.family: StudioTheme.font
        font.pixelSize: StudioTheme.fontBody
    }
}
