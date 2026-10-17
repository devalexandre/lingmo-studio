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

// Effects page: effect/transition/title libraries, the viewer, the effect
// stack of the selected clip and a keyframe curve editor.
SplitView {
    orientation: Qt.Vertical

    SplitView {
        SplitView.fillHeight: true
        SplitView.minimumHeight: 260
        orientation: Qt.Horizontal

        StudioPanel {
            title: qsTr("Library")
            SplitView.preferredWidth: 300
            SplitView.minimumWidth: 220
            LibraryPanel {
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.smallSpacing
            }
        }
        MonitorPanel {
            SplitView.fillWidth: true
            showSourceControls: false
        }
        StudioPanel {
            title: qsTr("Effect stack")
            SplitView.preferredWidth: 360
            SplitView.minimumWidth: 260
            Inspector {
                anchors.fill: parent
                anchors.topMargin: LingmoUI.Units.smallSpacing
                categories: ["transform", "stylize", "key", "text"]
            }
        }
    }

    SplitView {
        SplitView.preferredHeight: 300
        SplitView.minimumHeight: 180
        orientation: Qt.Horizontal

        StudioPanel {
            SplitView.preferredWidth: 520
            KeyframeEditor {
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.smallSpacing
            }
        }
        TimelinePanel {
            SplitView.fillWidth: true
            showToolbar: false
            compact: true
        }
    }
}
