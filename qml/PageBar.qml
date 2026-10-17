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

// Page navigation along the bottom of the window: large monochrome icons
// with labels, the current page lit in the Lingmo accent. Status on the left,
// project format and the project manager on the right.
Rectangle {
    id: bar

    property var pages: []          // [{ id, label, icon, shortcut }]
    property int currentIndex: 0
    property bool dimmed: false     // no page is current (start screen, cuts)
    signal activated(int index)
    signal homeRequested()
    signal formatClicked()

    implicitHeight: 58
    color: StudioTheme.window

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: StudioTheme.divider
    }

    // left: app and status
    RowLayout {
        anchors.left: parent.left
        anchors.leftMargin: StudioTheme.pad + 4
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, (bar.width - pageRow.width) / 2 - StudioTheme.pad * 3)
        spacing: 8
        Image {
            source: "qrc:/data/lingmo-studio.svg"
            sourceSize: Qt.size(20, 20)
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Text {
                text: "Lingmo Studio"
                color: StudioTheme.text
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontBody
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: editor.busyTask.length > 0 ? editor.busyTask : editor.status
                color: editor.busyTask.length > 0 ? StudioTheme.accentText : StudioTheme.textSecondary
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontSmall
                elide: Text.ElideRight
            }
        }
    }

    // centre: the pages
    Row {
        id: pageRow
        anchors.centerIn: parent
        spacing: 4

        Repeater {
            model: bar.pages
            delegate: T_PageButton { }
        }
    }

    component T_PageButton: AbstractButton {
        id: pageButton
        required property int index
        required property var modelData
        readonly property bool current: index === bar.currentIndex && !bar.dimmed
        width: Math.max(68, label.implicitWidth + 24)
        height: 50
        hoverEnabled: true
        onClicked: bar.activated(index)

        ToolTip.visible: hovered
        ToolTip.text: StudioTheme.tip(modelData.label, modelData.shortcut)
        ToolTip.delay: 600

        background: Rectangle {
            radius: StudioTheme.radius
            color: pageButton.down ? StudioTheme.pressed : pageButton.hovered ? StudioTheme.raised : "transparent"
            Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
            // accent bar under the current page
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: pageButton.current ? 22 : 0
                height: 2
                radius: 1
                color: StudioTheme.accent
                Behavior on width { NumberAnimation { duration: StudioTheme.normal; easing.type: Easing.OutCubic } }
            }
        }
        contentItem: Column {
            spacing: 3
            topPadding: 5
            StudioIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: pageButton.modelData.icon
                size: 22
                color: pageButton.current ? StudioTheme.accent : pageButton.hovered ? StudioTheme.text : StudioTheme.textSecondary
            }
            Text {
                id: label
                anchors.horizontalCenter: parent.horizontalCenter
                text: pageButton.modelData.label
                color: pageButton.current ? StudioTheme.text : pageButton.hovered ? StudioTheme.text : StudioTheme.textSecondary
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontSmall
                font.weight: pageButton.current ? Font.DemiBold : Font.Medium
                Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
            }
        }
    }

    // right: format, GPU, project manager
    RowLayout {
        anchors.right: parent.right
        anchors.rightMargin: StudioTheme.pad
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        ColumnLayout {
            spacing: 0
            Text {
                Layout.alignment: Qt.AlignRight
                text: editor.projectWidth + "×" + editor.projectHeight + " · " + Number(editor.fps).toLocaleString(Qt.locale(), 'f', editor.fps % 1 ? 2 : 0) + " fps"
                color: formatMouse.containsMouse ? StudioTheme.text : StudioTheme.textSecondary
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontSmall
                font.features: { "tnum": 1 }
                MouseArea {
                    id: formatMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.formatClicked()
                }
                ToolTip.visible: formatMouse.containsMouse
                ToolTip.text: qsTr("Project settings")
                ToolTip.delay: 600
            }
            Text {
                Layout.alignment: Qt.AlignRight
                text: editor.hwDetected ? (editor.hwEncoders.length > 0 ? qsTr("GPU encode: %1").arg(editor.hwEncoders.join(", ")) : qsTr("GPU encode: none")) : qsTr("Checking GPU encoders…")
                color: StudioTheme.textDisabled
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontTiny
                elide: Text.ElideRight
                Layout.maximumWidth: Math.max(80, (bar.width - pageRow.width) / 2 - 90)
            }
        }
        StudioButton {
            iconName: "home"
            iconSize: 18
            size: 34
            tip: qsTr("Project manager")
            shortcut: "Shift+1"
            onClicked: bar.homeRequested()
        }
    }
}
