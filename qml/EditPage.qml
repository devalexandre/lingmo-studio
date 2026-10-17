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
import QtQuick.Dialogs
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// Edit page: media/library | monitor | inspector/history over the timeline.
SplitView {
    id: page
    orientation: Qt.Vertical

    FileDialog {
        id: importDialog
        title: qsTr("Import media")
        fileMode: FileDialog.OpenFiles
        nameFilters: editor.mediaNameFilters()
        onAccepted: editor.importUrls(selectedFiles)
    }

    SplitView {
        SplitView.fillHeight: true
        SplitView.minimumHeight: 260
        orientation: Qt.Horizontal

        StudioPanel {
            id: left
            SplitView.preferredWidth: 340
            SplitView.minimumWidth: 220
            property int tab: 0
            actions: [
                LingmoUI.SegmentedControl {
                    model: [qsTr("Media"), qsTr("Library"), qsTr("Search")]
                    currentIndex: left.tab
                    onActivated: function(i) { left.tab = i }
                }
            ]
            StackLayout {
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.smallSpacing
                currentIndex: left.tab
                MediaBinPanel { onImportRequested: importDialog.open() }
                LibraryPanel { }
                SearchPanel { }
            }
        }

        MonitorPanel {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 360
        }

        StudioPanel {
            id: right
            SplitView.preferredWidth: 340
            SplitView.minimumWidth: 240
            property int tab: 0
            actions: [
                LingmoUI.SegmentedControl {
                    model: [qsTr("Inspector"), qsTr("History")]
                    currentIndex: right.tab
                    onActivated: function(i) { right.tab = i }
                }
            ]
            StackLayout {
                anchors.fill: parent
                anchors.topMargin: LingmoUI.Units.smallSpacing
                currentIndex: right.tab
                Inspector { }
                HistoryPanel { }
            }
        }
    }

    TimelinePanel {
        SplitView.preferredHeight: 330
        SplitView.minimumHeight: 160
    }
}
