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

// Cut page: for fast edits. Media pool and one viewer on top, big one-click
// edits in the middle, and a single timeline seen twice: the whole program
// above, a close-up that follows the playhead below.
SplitView {
    id: page
    orientation: Qt.Vertical
    handle: PanelHandle { }

    SplitView {
        SplitView.fillHeight: true
        SplitView.minimumHeight: 240
        orientation: Qt.Horizontal
        handle: PanelHandle { }

        MediaBinPanel {
            SplitView.preferredWidth: 380
            SplitView.minimumWidth: 240
            thumbWidth: 130
            onImportRequested: root.importMedia()
        }
        MonitorPanel {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 420
        }
    }

    ColumnLayout {
        SplitView.preferredHeight: 420
        SplitView.minimumHeight: 280
        spacing: StudioTheme.gap

        // one-click edits
        StudioPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            RowLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6
                BigAction {
                    text: qsTr("Append")
                    iconName: "append"
                    hint: "Shift+F12"
                    tip: qsTr("Add the source clip (or its in/out range) to the end of the timeline")
                    enabled: editor.player.sourceMediaId.length > 0
                    onClicked: root.editFromSource("append")
                }
                BigAction {
                    text: qsTr("Insert")
                    iconName: "insert"
                    hint: "F9"
                    tip: qsTr("Insert at the playhead, pushing later clips right")
                    enabled: editor.player.sourceMediaId.length > 0
                    onClicked: root.editFromSource("insert")
                }
                BigAction {
                    text: qsTr("Place on top")
                    iconName: "place-top"
                    hint: "F12"
                    tip: qsTr("Put the clip on the track above at the playhead")
                    enabled: editor.player.sourceMediaId.length > 0
                    onClicked: root.editFromSource("top")
                }
                BigAction {
                    text: qsTr("Smart trim")
                    iconName: "smart-trim"
                    tip: qsTr("Trim the nearer end of the clip under the playhead to the playhead and close the gap")
                    enabled: editor.timeline.duration > 1
                    onClicked: root.smartTrim()
                }
                BigAction {
                    text: qsTr("Split")
                    iconName: "split"
                    hint: "S"
                    tip: qsTr("Split at playhead")
                    enabled: editor.timeline.duration > 1
                    onClicked: editor.splitAtPlayhead()
                }
                Item { Layout.fillWidth: true }
                BigAction {
                    text: qsTr("Captions")
                    iconName: "captions"
                    tip: qsTr("Automatic captions synced to the speech")
                    onClicked: root.openCaptions("")
                }
                BigAction {
                    text: qsTr("Generate cuts")
                    iconName: "shorts"
                    primary: true
                    tip: qsTr("Shorts, Reels and TikToks from a long video")
                    onClicked: root.openShorts(editor.player.sourceMediaId)
                }
            }
        }

        // the whole program
        TimelinePanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 126
            mode: "overview"
            showToolbar: false
        }
        // close-up around the playhead
        TimelinePanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            mode: "zoomed"
            compact: true
            showToolbar: false
        }
    }
}
