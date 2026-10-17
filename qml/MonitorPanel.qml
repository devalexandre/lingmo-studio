/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

import QtQuick

// A single viewer that shows whatever the player plays (source clip or
// timeline), for pages with one viewer (Cut, Effects, Color, Deliver).
Viewer {
    property bool showSourceControls: true

    role: "follow"
    showModeSwitch: showSourceControls
    showEditButtons: showSourceControls
}
