/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

import QtQuick
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// A monochrome icon: one of Studio's own (data/icons, `name`) or a theme icon
// (`themeName`), tinted through IconItem.color.
Item {
    id: icon

    property string name: ""
    property string themeName: ""
    property color color: StudioTheme.text
    property int size: 16

    implicitWidth: size
    implicitHeight: size

    LingmoUI.IconItem {
        id: item
        anchors.centerIn: parent
        width: icon.size
        height: icon.size
        smooth: true
        source: icon.name.length > 0 ? StudioTheme.icon(icon.name) : icon.themeName
    }
    // A Binding rather than `color:` so an older LingmoUI without the
    // property still loads (the glyphs are drawn light already).
    Binding {
        target: item
        property: "color"
        value: icon.color
    }
    Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
}
