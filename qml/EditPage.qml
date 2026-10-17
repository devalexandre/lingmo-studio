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

// Edit page: panel toggles on top; Media Pool / Effects Library / Edit
// Index on the left, source and timeline viewers side by side, Inspector on
// the right; the timeline across the bottom with an optional mixer.
ColumnLayout {
    id: page
    spacing: StudioTheme.gap

    property bool showPool: true
    property bool showFx: false
    property bool showIndex: false
    property bool showInspector: true
    property bool showMixer: false

    // ---------------------------------------------------- toggle rows
    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 28
        spacing: 2
        StudioButton { iconName: "media-pool"; text: qsTr("Media Pool"); size: 26; fontSize: StudioTheme.fontSmall; checkable: true; checked: page.showPool; onClicked: page.showPool = !page.showPool }
        StudioButton { iconName: "fx-library"; text: qsTr("Effects Library"); size: 26; fontSize: StudioTheme.fontSmall; checkable: true; checked: page.showFx; onClicked: page.showFx = !page.showFx }
        StudioButton { iconName: "edit-index"; text: qsTr("Edit Index"); size: 26; fontSize: StudioTheme.fontSmall; checkable: true; checked: page.showIndex; onClicked: page.showIndex = !page.showIndex }
        Item { Layout.fillWidth: true }
        StudioButton { iconName: "mixer"; text: qsTr("Mixer"); size: 26; fontSize: StudioTheme.fontSmall; checkable: true; checked: page.showMixer; onClicked: page.showMixer = !page.showMixer }
        StudioButton { iconName: "inspector"; text: qsTr("Inspector"); size: 26; fontSize: StudioTheme.fontSmall; checkable: true; checked: page.showInspector; onClicked: page.showInspector = !page.showInspector }
    }

    SplitView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        orientation: Qt.Vertical
        handle: PanelHandle { }

        SplitView {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 260
            orientation: Qt.Horizontal
            handle: PanelHandle { }

            MediaBinPanel {
                visible: page.showPool
                SplitView.preferredWidth: 330
                SplitView.minimumWidth: 220
                onImportRequested: root.importMedia()
            }
            StudioPanel {
                visible: page.showFx
                title: qsTr("Effects Library")
                SplitView.preferredWidth: 280
                SplitView.minimumWidth: 220
                LibraryPanel {
                    anchors.fill: parent
                    anchors.margins: 6
                }
            }
            EditIndex {
                visible: page.showIndex
                SplitView.preferredWidth: 320
                SplitView.minimumWidth: 240
            }

            // source | timeline viewers
            Item {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 480
                RowLayout {
                    anchors.fill: parent
                    spacing: StudioTheme.gap
                    Viewer {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        role: "source"
                        dual: true
                        showEditButtons: true
                    }
                    Viewer {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        role: "timeline"
                        dual: true
                    }
                }
            }

            StudioPanel {
                visible: page.showInspector
                title: qsTr("Inspector")
                SplitView.preferredWidth: 340
                SplitView.minimumWidth: 260
                Inspector { anchors.fill: parent }
            }
        }

        SplitView {
            SplitView.preferredHeight: 340
            SplitView.minimumHeight: 170
            orientation: Qt.Horizontal
            handle: PanelHandle { }

            TimelinePanel {
                SplitView.fillWidth: true
            }
            StudioPanel {
                visible: page.showMixer
                title: qsTr("Mixer")
                SplitView.preferredWidth: 380
                SplitView.minimumWidth: 200
                Mixer {
                    anchors.fill: parent
                    anchors.margins: 6
                    compact: true
                }
            }
        }
    }
}
