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

// Two vertical bars (IEC scaled 0..1) with a peak hold and colour zones.
Row {
    id: meter
    property real levelLeft: 0
    property real levelRight: 0
    spacing: 2

    Repeater {
        model: [meter.levelLeft, meter.levelRight]
        Rectangle {
            id: bar
            width: (meter.width - meter.spacing) / 2
            height: meter.height
            radius: 2
            color: LingmoUI.Theme.alternateBackgroundColor
            property real level: Math.max(0, Math.min(1, modelData))
            property real peak: 0
            onLevelChanged: if (level > peak) peak = level
            Timer {
                interval: 60
                running: bar.peak > 0
                repeat: true
                onTriggered: bar.peak = Math.max(bar.level, bar.peak - 0.02)
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: parent.height * bar.level
                radius: 2
                color: bar.level > 0.9 ? LingmoUI.Theme.redColor : bar.level > 0.75 ? LingmoUI.Theme.orangeColor : LingmoUI.Theme.greenColor
                Behavior on height { NumberAnimation { duration: LingmoUI.Units.veryShortDuration } }
            }
            Rectangle {
                width: parent.width
                height: 2
                y: parent.height * (1 - bar.peak)
                visible: bar.peak > 0.01
                color: LingmoUI.Theme.textColor
            }
        }
    }
}
