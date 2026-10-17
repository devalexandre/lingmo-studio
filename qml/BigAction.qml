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
import QtQuick.Templates as T

// A large one-click action (Cut page, start screen): icon over a label,
// with the shortcut in small type.
T.AbstractButton {
    id: control

    property string iconName: ""
    property string hint: ""
    property string tip: ""
    property bool primary: false

    implicitWidth: Math.max(96, label.implicitWidth + 28)
    implicitHeight: 58
    hoverEnabled: true

    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: StudioTheme.tip(tip, hint)
    ToolTip.delay: 500

    background: Rectangle {
        radius: StudioTheme.radius
        color: control.primary ? (control.down ? Qt.darker(StudioTheme.accent, 1.15) : control.hovered ? StudioTheme.accentHover : StudioTheme.accent)
                               : (control.down ? StudioTheme.pressed : control.hovered ? StudioTheme.raisedHover : StudioTheme.raised)
        border.width: control.primary ? 0 : 1
        border.color: control.hovered ? StudioTheme.alpha(StudioTheme.accent, 0.6) : StudioTheme.border
        Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
        Behavior on border.color { ColorAnimation { duration: StudioTheme.fast } }
    }
    contentItem: Item {
        opacity: control.enabled ? 1 : 0.4
        Column {
            anchors.centerIn: parent
            spacing: 4
            StudioIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: control.iconName
                size: 22
                color: control.primary ? "#FFFFFF" : control.hovered ? StudioTheme.text : StudioTheme.textSecondary
                scale: control.down ? 0.9 : 1
                Behavior on scale { NumberAnimation { duration: StudioTheme.fast } }
            }
            Text {
                id: label
                anchors.horizontalCenter: parent.horizontalCenter
                text: control.text
                color: control.primary ? "#FFFFFF" : StudioTheme.text
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontSmall
                font.weight: Font.DemiBold
            }
        }
    }
}
