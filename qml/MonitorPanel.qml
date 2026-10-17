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
import Lingmo.Studio 1.0

// Viewer: one monitor that shows either the source clip (from the media bin)
// or the timeline (program). Frame-accurate scrubbing, J/K/L, in/out marks.
StudioPanel {
    id: monitor

    property bool compare: false
    property bool showSourceControls: true
    readonly property var player: editor.player
    readonly property QtObject studioEditor: editor
    readonly property bool sourceMode: player.mode === "source"

    title: sourceMode ? qsTr("Source") : (editor.sequencePath.length > 1 ? editor.sequencePath[editor.sequencePath.length - 1] : qsTr("Timeline"))

    actions: [
        LingmoUI.SegmentedControl {
            visible: monitor.showSourceControls
            model: [qsTr("Source"), qsTr("Timeline")]
            currentIndex: monitor.sourceMode ? 0 : 1
            onActivated: function(index) { player.setMode(index === 0 ? "source" : "timeline") }
            enabled: player.sourceMediaId.length > 0 || monitor.sourceMode
        },
        ComboBox {
            id: previewRes
            implicitWidth: 92
            model: ["1080p", "720p", "540p", "360p"]
            readonly property var heights: [1080, 720, 540, 360]
            currentIndex: Math.max(0, heights.indexOf(player.previewHeight))
            onActivated: function(i) { player.previewHeight = heights[i] }
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Preview resolution")
        }
    ]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: LingmoUI.Units.smallSpacing
        spacing: LingmoUI.Units.smallSpacing

        // Picture
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: LingmoUI.Theme.smallRadius
            color: Qt.rgba(0, 0, 0, 1) // video surround is always black
            clip: true

            VideoView {
                id: video
                anchors.fill: parent
                editor: monitor.studioEditor
                compare: monitor.compare && !monitor.sourceMode
                split: splitHandle.x / Math.max(1, width)
            }
            // before/after divider
            Rectangle {
                id: splitHandle
                visible: video.compare
                x: video.videoRect.x + video.videoRect.width / 2
                y: video.videoRect.y
                width: 2
                height: video.videoRect.height
                color: LingmoUI.Theme.highlightColor
                Label {
                    anchors.right: parent.left
                    anchors.rightMargin: LingmoUI.Units.smallSpacing
                    anchors.top: parent.top
                    anchors.topMargin: LingmoUI.Units.smallSpacing
                    text: qsTr("Before")
                    color: LingmoUI.Theme.highlightedTextColor
                    font.pointSize: LingmoUI.Units.fontCaption
                }
                Label {
                    anchors.left: parent.right
                    anchors.leftMargin: LingmoUI.Units.smallSpacing
                    anchors.top: parent.top
                    anchors.topMargin: LingmoUI.Units.smallSpacing
                    text: qsTr("After")
                    color: LingmoUI.Theme.highlightedTextColor
                    font.pointSize: LingmoUI.Units.fontCaption
                }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -8
                    cursorShape: Qt.SplitHCursor
                    drag.target: splitHandle
                    drag.axis: Drag.XAxis
                    drag.minimumX: video.videoRect.x
                    drag.maximumX: video.videoRect.x + video.videoRect.width
                }
            }
            Label {
                anchors.centerIn: parent
                visible: editor.timeline.duration <= 1 && !monitor.sourceMode
                text: qsTr("Drop clips on the timeline to see them here")
                color: LingmoUI.Theme.secondaryTextColor
                font.pointSize: LingmoUI.Units.fontBody
            }
            // Audio meter (master)
            LevelMeter {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.margins: LingmoUI.Units.smallSpacing
                width: 10
                levelLeft: player.levelLeft
                levelRight: player.levelRight
            }
        }

        // Scrub bar with in/out range
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 18
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 4
                radius: 2
                color: LingmoUI.Theme.alternateBackgroundColor
            }
            Rectangle {
                readonly property int inF: monitor.sourceMode ? player.sourceIn : player.markIn
                readonly property int outF: monitor.sourceMode ? player.sourceOut : player.markOut
                visible: inF >= 0 || outF >= 0
                anchors.verticalCenter: parent.verticalCenter
                x: Math.max(0, inF) / Math.max(1, player.duration) * parent.width
                width: ((outF >= 0 ? outF : player.duration) - Math.max(0, inF)) / Math.max(1, player.duration) * parent.width
                height: 6
                radius: 3
                color: LingmoUI.Theme.highlightColor
                opacity: 0.6
            }
            Rectangle {
                x: player.position / Math.max(1, player.duration - 1) * (parent.width - width)
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: 16
                radius: 1.5
                color: LingmoUI.Theme.textColor
            }
            MouseArea {
                anchors.fill: parent
                function seekTo(x) { player.seek(Math.round(Math.max(0, Math.min(1, x / width)) * (player.duration - 1))) }
                onPressed: function(m) { player.pause(); seekTo(m.x) }
                onPositionChanged: function(m) { if (pressed) seekTo(m.x) }
            }
        }

        // Transport
        RowLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                text: editor.timecode(player.position)
                font.pointSize: LingmoUI.Units.fontSubheading
                font.features: { "tnum": 1 }
                color: LingmoUI.Theme.textColor
                Layout.minimumWidth: implicitWidth
            }
            Label {
                text: "/ " + editor.timecode(player.duration)
                font.pointSize: LingmoUI.Units.fontCaption
                font.features: { "tnum": 1 }
                color: LingmoUI.Theme.secondaryTextColor
            }
            Item { Layout.fillWidth: true }
            LingmoUI.IconButton { iconName: "go-first"; tipText: qsTr("Start (Home)"); onClicked: player.seek(0) }
            LingmoUI.IconButton { iconName: "media-skip-backward"; tipText: qsTr("Previous edit (Up)"); onClicked: player.seek(editor.nextEdit(player.position, false)) }
            LingmoUI.IconButton { iconName: "go-previous"; tipText: qsTr("Previous frame (Left)"); onClicked: player.step(-1) }
            LingmoUI.IconButton { iconName: "media-seek-backward"; tipText: qsTr("Play backwards (J)"); onClicked: player.shuttle(-1); checkable: true; checked: player.speed < 0 }
            LingmoUI.IconButton {
                iconName: player.playing ? "media-playback-pause" : "media-playback-start"
                tipText: player.playing ? qsTr("Pause (Space)") : qsTr("Play (Space)")
                size: LingmoUI.Units.controlHeight
                iconSize: LingmoUI.Units.iconSizes.smallMedium
                round: true
                flat: false
                onClicked: player.togglePlay()
            }
            LingmoUI.IconButton { iconName: "media-seek-forward"; tipText: qsTr("Faster forward (L)"); onClicked: player.shuttle(1); checkable: true; checked: player.speed > 1 }
            LingmoUI.IconButton { iconName: "go-next"; tipText: qsTr("Next frame (Right)"); onClicked: player.step(1) }
            LingmoUI.IconButton { iconName: "media-skip-forward"; tipText: qsTr("Next edit (Down)"); onClicked: player.seek(editor.nextEdit(player.position, true)) }
            Item { Layout.fillWidth: true }
            Label {
                visible: Math.abs(player.speed) > 1
                text: player.speed + "×"
                color: LingmoUI.Theme.highlightColor
                font.pointSize: LingmoUI.Units.fontCaption
            }
            LingmoUI.IconButton { iconName: "format-text-direction-ltr"; tipText: qsTr("Mark in (I)"); onClicked: player.markInHere() }
            LingmoUI.IconButton { iconName: "format-text-direction-rtl"; tipText: qsTr("Mark out (O)"); onClicked: player.markOutHere() }
            LingmoUI.IconButton { iconName: "edit-clear"; tipText: qsTr("Clear in/out"); onClicked: player.clearMarks() }
            LingmoUI.ActionButton {
                visible: monitor.sourceMode && monitor.showSourceControls
                text: qsTr("Insert")
                iconName: "edit-paste"
                tipText: qsTr("Insert the marked range at the playhead, pushing clips right")
                onClicked: monitor.placeSource(true)
            }
            LingmoUI.ActionButton {
                visible: monitor.sourceMode && monitor.showSourceControls
                text: qsTr("Overwrite")
                primary: true
                tipText: qsTr("Place the marked range at the playhead, replacing what is there")
                onClicked: monitor.placeSource(false)
            }
            Slider {
                Layout.preferredWidth: 70
                from: 0
                to: 1
                value: player.volume
                onMoved: player.volume = value
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Preview volume")
            }
        }
    }

    function placeSource(ripple) {
        const id = player.sourceMediaId
        const inF = player.sourceIn >= 0 ? player.sourceIn : 0
        const outF = player.sourceOut >= 0 ? player.sourceOut : player.duration - 1
        const at = player.timelinePosition
        player.setMode("timeline")
        editor.addMediaToTimeline(id, -1, at, inF, outF, ripple)
    }
}
