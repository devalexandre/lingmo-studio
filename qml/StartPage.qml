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

// Start: new project from a template or a size preset, recent projects.
Item {
    id: start
    signal done()
    signal openRequested()

    RowLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - LingmoUI.Units.largeSpacing * 4, 1100)
        height: Math.min(parent.height - LingmoUI.Units.largeSpacing * 2, 620)
        spacing: LingmoUI.Units.largeSpacing * 2

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: LingmoUI.Units.largeSpacing

            LingmoUI.PageHeader {
                Layout.fillWidth: true
                title: qsTr("New project")
                subtitle: qsTr("Start from a template; sizes can change later in Project settings.")
                iconName: "video-x-generic"
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: LingmoUI.Units.smallSpacing
                rowSpacing: LingmoUI.Units.smallSpacing
                Repeater {
                    model: editor.templates
                    LingmoUI.Card {
                        Layout.fillWidth: true
                        implicitHeight: 74
                        clickable: true
                        liftOnHover: true
                        onClicked: { editor.newFromTemplate(modelData.id); start.done() }
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: LingmoUI.Units.largeSpacing
                            spacing: 2
                            Label { text: modelData.name; font.pointSize: LingmoUI.Units.fontSubheading; font.bold: true; color: LingmoUI.Theme.textColor }
                            Label { text: modelData.description; font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.secondaryTextColor; elide: Text.ElideRight; Layout.fillWidth: true }
                        }
                    }
                }
            }
            Label { text: qsTr("Empty project"); font.pointSize: LingmoUI.Units.fontBody; font.bold: true; color: LingmoUI.Theme.textColor }
            Flow {
                Layout.fillWidth: true
                spacing: LingmoUI.Units.smallSpacing
                Repeater {
                    model: [
                        { id: "1080p30", name: qsTr("1080p 30") }, { id: "1080p60", name: qsTr("1080p 60") }, { id: "1080p24", name: qsTr("1080p 24") },
                        { id: "4k30", name: qsTr("4K 30") }, { id: "4k60", name: qsTr("4K 60") }, { id: "720p30", name: qsTr("720p 30") },
                        { id: "vertical1080", name: qsTr("Vertical 9:16") }, { id: "square1080", name: qsTr("Square 1:1") }
                    ]
                    LingmoUI.ActionButton {
                        text: modelData.name
                        onClicked: { editor.newProject(modelData.id); start.done() }
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }

        LingmoUI.Card {
            Layout.preferredWidth: 380
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.largeSpacing
                spacing: LingmoUI.Units.smallSpacing
                RowLayout {
                    Label { text: qsTr("Recent projects"); font.pointSize: LingmoUI.Units.fontSubheading; font.bold: true; color: LingmoUI.Theme.textColor; Layout.fillWidth: true }
                    LingmoUI.ActionButton { text: qsTr("Open…"); iconName: "document-open"; onClicked: start.openRequested() }
                }
                ListView {
                    id: recent
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: editor.recentProjects
                    spacing: 2
                    delegate: LingmoUI.SideBarItem {
                        width: recent.width
                        title: modelData.substring(modelData.lastIndexOf("/") + 1)
                        iconName: "video-x-generic"
                        onClicked: { if (editor.openPath(modelData)) start.done() }
                        ToolTip.visible: hovered
                        ToolTip.text: modelData
                    }
                }
                LingmoUI.EmptyState {
                    Layout.alignment: Qt.AlignHCenter
                    visible: recent.count === 0
                    iconName: "document-open-recent"
                    iconSize: LingmoUI.Units.iconSizes.large
                    title: qsTr("No recent projects")
                    text: qsTr("Projects are MLT files, so Shotcut and melt can open them too.")
                }
                LingmoUI.ActionButton {
                    Layout.fillWidth: true
                    text: qsTr("Continue without a template")
                    subtle: true
                    onClicked: start.done()
                }
            }
        }
    }
}
