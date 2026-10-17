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

// A titled editor panel: a LingmoUI.Card with a compact header row.
LingmoUI.Card {
    id: panel

    property string title: ""
    default property alias content: body.data
    property alias actions: actionsRow.data
    readonly property int headerHeight: header.visible ? header.height : 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            id: header
            visible: panel.title.length > 0 || actionsRow.children.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: LingmoUI.Units.smallControlHeight + LingmoUI.Units.smallSpacing
            Layout.leftMargin: LingmoUI.Units.largeSpacing
            Layout.rightMargin: LingmoUI.Units.smallSpacing
            spacing: LingmoUI.Units.smallSpacing

            Label {
                text: panel.title
                font.pointSize: LingmoUI.Units.fontBody
                font.bold: true
                color: LingmoUI.Theme.textColor
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            RowLayout {
                id: actionsRow
                spacing: 2
            }
        }

        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
        }
    }
}
