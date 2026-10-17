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

// SplitView handle: the panel gap, with a thin accent line while hovered or
// dragged. Use as `handle: PanelHandle { }`.
Item {
    id: handle
    readonly property bool vertical: parent ? parent.orientation === Qt.Vertical : false
    implicitWidth: StudioTheme.gap
    implicitHeight: StudioTheme.gap

    Rectangle {
        anchors.centerIn: parent
        width: handle.vertical ? Math.min(parent.width, 48) : 2
        height: handle.vertical ? 2 : Math.min(parent.height, 48)
        radius: 1
        color: handle.SplitHandle.pressed ? StudioTheme.accent : handle.SplitHandle.hovered ? StudioTheme.textSecondary : "transparent"
        Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
    }
}
