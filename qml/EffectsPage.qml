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

// Effects page (Fusion-like): library, viewer and inspector on top; the
// selected clip's effect chain as nodes and the keyframe curves below; the
// program's clips along the bottom to pick what to work on.
SplitView {
    id: page
    orientation: Qt.Vertical
    handle: PanelHandle { }

    property int focusNode: -1
    readonly property string selectedId: editor.selectedClip && editor.selectedClip.id !== undefined ? editor.selectedClip.id : ""
    onSelectedIdChanged: focusNode = -1

    SplitView {
        SplitView.fillHeight: true
        SplitView.minimumHeight: 240
        orientation: Qt.Horizontal
        handle: PanelHandle { }

        StudioPanel {
            title: qsTr("Effects Library")
            icon: "fx-library"
            SplitView.preferredWidth: 280
            SplitView.minimumWidth: 220
            LibraryPanel {
                anchors.fill: parent
                anchors.margins: 6
            }
        }
        MonitorPanel {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 400
            showSourceControls: false
        }
        StudioPanel {
            title: qsTr("Inspector")
            icon: "inspector"
            SplitView.preferredWidth: 360
            SplitView.minimumWidth: 260
            Inspector {
                anchors.fill: parent
                categories: ["transform", "stylize", "key", "text"]
                focusEffect: page.focusNode
            }
        }
    }

    SplitView {
        SplitView.preferredHeight: 250
        SplitView.minimumHeight: 170
        orientation: Qt.Horizontal
        handle: PanelHandle { }

        StudioPanel {
            title: qsTr("Nodes")
            icon: "page-effects"
            SplitView.fillWidth: true
            NodeGraph {
                anchors.fill: parent
                anchors.margins: 6
                focusIndex: page.focusNode
                onFocusRequested: function(index) { page.focusNode = index }
            }
        }
        StudioPanel {
            title: qsTr("Keyframes")
            icon: "keyframe"
            SplitView.preferredWidth: 520
            SplitView.minimumWidth: 320
            KeyframeEditor {
                anchors.fill: parent
                anchors.margins: 8
            }
        }
    }

    ClipStrip {
        SplitView.preferredHeight: 104
        SplitView.minimumHeight: 104
        SplitView.maximumHeight: 104
    }
}
