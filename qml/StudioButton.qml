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

// The studio's button: flat icon (and optional label) on charcoal, with a
// hover wash, an accent state when checked, and a tooltip that names the
// shortcut. `primary` fills it with the accent (Export, Render).
T.AbstractButton {
    id: control

    property string iconName: ""       // own icon name (data/icons)
    property string themeIcon: ""      // or a theme icon
    property string tip: ""
    property string shortcut: ""
    property bool primary: false
    property bool danger: false
    property bool flat: true
    property int iconSize: 16
    property int size: StudioTheme.controlHeight
    // colour of the icon (and label) when checked; accent by default
    property color checkedColor: StudioTheme.accentText
    // checked look: "tint" (accent wash) or "fill" (solid tone, for M/S)
    property string checkedStyle: "tint"
    property int fontSize: StudioTheme.fontBody
    property bool bold: false

    readonly property bool hasIcon: iconName.length > 0 || themeIcon.length > 0
    readonly property bool hasText: text.length > 0
    readonly property color foreground: {
        if (!enabled)
            return StudioTheme.textDisabled
        if (primary || danger)
            return "#FFFFFF"
        if (checked)
            return checkedStyle === "fill" ? "#16171A" : checkedColor
        return hovered || down ? StudioTheme.text : (flat ? StudioTheme.textSecondary : StudioTheme.text)
    }

    implicitHeight: size
    implicitWidth: hasText ? row.implicitWidth + (hasIcon ? 18 : 22) : size
    hoverEnabled: true
    focusPolicy: Qt.TabFocus

    Accessible.role: Accessible.Button
    Accessible.name: tip.length > 0 ? tip : text

    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: StudioTheme.tip(tip, shortcut)
    ToolTip.delay: 450

    background: Rectangle {
        radius: StudioTheme.smallRadius + 2
        color: {
            if (control.primary)
                return control.down ? Qt.darker(StudioTheme.accent, 1.15) : control.hovered ? StudioTheme.accentHover : StudioTheme.accent
            if (control.danger)
                return control.down ? Qt.darker(StudioTheme.danger, 1.15) : control.hovered ? Qt.lighter(StudioTheme.danger, 1.1) : StudioTheme.danger
            if (control.checked && control.checkedStyle === "fill")
                return control.checkedColor
            if (control.checked)
                return control.down ? StudioTheme.alpha(StudioTheme.accent, 0.30) : StudioTheme.accentSoft
            if (control.down)
                return StudioTheme.pressed
            if (control.hovered)
                return StudioTheme.raisedHover
            return control.flat ? "transparent" : StudioTheme.raised
        }
        border.width: control.visualFocus ? 1 : (!control.flat && !control.primary && !control.danger && !control.checked ? 1 : 0)
        border.color: control.visualFocus ? StudioTheme.accent : StudioTheme.border
        Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
    }

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 6
            StudioIcon {
                visible: control.hasIcon
                anchors.verticalCenter: parent.verticalCenter
                name: control.iconName
                themeName: control.themeIcon
                size: control.iconSize
                color: control.foreground
                scale: control.down ? 0.9 : 1
                Behavior on scale { NumberAnimation { duration: StudioTheme.fast; easing.type: Easing.OutCubic } }
            }
            Text {
                visible: control.hasText
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                color: control.foreground
                font.family: StudioTheme.font
                font.pixelSize: control.fontSize
                font.weight: control.bold || control.primary ? Font.DemiBold : Font.Medium
                Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
            }
        }
    }
}
