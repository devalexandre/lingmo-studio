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

// Undo history: click a step to go back (or forward) to it.
ListView {
    id: list
    clip: true
    spacing: 1
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}
    model: editor.history
    currentIndex: editor.historyIndex
    onCountChanged: positionViewAtEnd()

    delegate: LingmoUI.SideBarItem {
        width: list.width
        title: modelData
        isCurrent: index === editor.historyIndex
        emphasized: index <= editor.historyIndex
        opacity: index <= editor.historyIndex ? 1 : 0.55
        onClicked: editor.setHistoryIndex(index)
    }
}
