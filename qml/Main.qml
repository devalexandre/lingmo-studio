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
import QtQuick.Dialogs
import QtQuick.Layouts
import LingmoUI
import LingmoUI.CompatibleModule 3.0 as LingmoUI

LingmoUI.Window {
    id: root

    width: 1480
    height: 920
    minimumWidth: 1100
    minimumHeight: 700
    visible: true
    title: (editor.dirty ? "• " : "") + editor.projectName + " — Lingmo Studio"

    background.opacity: LingmoUI.Theme.darkMode ? 0.88 : 0.97
    header.height: LingmoUI.Units.windowHeaderHeight
    contentTopMargin: 0

    readonly property var pageIds: ["edit", "color", "effects", "audio", "deliver"]
    property int page: Math.max(0, pageIds.indexOf(startPage))
    property bool showStart: startPage.length === 0 && editor.mediaBin.count === 0 && editor.timeline.duration <= 1
    property bool closeConfirmed: false
    // Edit tools: select, razor, ripple, roll, slip, slide
    property string tool: "select"
    property bool snapping: true
    property bool rippleMode: false

    LingmoUI.WindowBlur {
        view: root
        geometry: Qt.rect(root.x, root.y, root.width, root.height)
        windowRadius: root.windowRadius
        enabled: true
    }

    // Editors are dark by convention (judging color needs a neutral, dim
    // surround); Settings > Appearance can follow the system instead.
    function applyTheme() {
        if (editor.darkUi) {
            LingmoTheme.darkMode = LingmoThemeType.Dark
            LingmoUI.Theme.darkMode = true
        } else {
            LingmoTheme.darkMode = LingmoThemeType.System
            LingmoUI.Theme.darkMode = Qt.binding(function() { return LingmoTheme.dark })
        }
    }
    Component.onCompleted: applyTheme()
    Connections {
        target: editor
        function onDarkUiChanged() { root.applyTheme() }
        function onError(message) { root.showPassiveNotification(message) }
    }

    onClosing: function(close) {
        if (editor.dirty && !closeConfirmed) {
            close.accepted = false
            unsavedDialog.open()
            return
        }
        editor.cleanExit()
    }

    // ------------------------------------------------------------ file dialogs
    FileDialog {
        id: openDialog
        title: qsTr("Open project")
        nameFilters: [qsTr("MLT projects (*.mlt)"), qsTr("All files (*)")]
        onAccepted: editor.openProject(selectedFile)
    }
    FileDialog {
        id: saveDialog
        property bool thenClose: false
        title: qsTr("Save project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mlt"
        nameFilters: [qsTr("MLT projects (*.mlt)")]
        onAccepted: {
            if (editor.saveProjectAs(selectedFile) && thenClose) {
                root.closeConfirmed = true
                root.close()
            }
        }
    }
    FileDialog {
        id: importDialog
        title: qsTr("Import media")
        fileMode: FileDialog.OpenFiles
        nameFilters: editor.mediaNameFilters()
        onAccepted: editor.importUrls(selectedFiles)
    }
    FileDialog {
        id: srtOpenDialog
        title: qsTr("Import subtitles")
        nameFilters: [qsTr("Subtitles (*.srt)")]
        onAccepted: editor.importSubtitles(selectedFile)
    }
    FileDialog {
        id: srtSaveDialog
        title: qsTr("Export subtitles")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "srt"
        nameFilters: [qsTr("Subtitles (*.srt)")]
        onAccepted: editor.exportSubtitles(selectedFile)
    }

    function transitionLabel(type) {
        const names = {
            "dissolve": qsTr("Cross dissolve"), "wipe-left": qsTr("Wipe left"), "wipe-right": qsTr("Wipe right"),
            "wipe-down": qsTr("Wipe down"), "wipe-up": qsTr("Wipe up"), "barn-door-h": qsTr("Barn door horizontal"),
            "barn-door-v": qsTr("Barn door vertical"), "diagonal": qsTr("Diagonal"), "iris-circle": qsTr("Iris circle"),
            "iris-box": qsTr("Iris box"), "clock": qsTr("Clock wipe"), "matrix": qsTr("Matrix")
        }
        return names[type] || type
    }

    function save() {
        if (!editor.saveProject())
            saveDialog.open()
    }

    ProjectSettingsDialog { id: settingsDialog }

    Dialog {
        id: unsavedDialog
        anchors.centerIn: parent
        width: 460
        modal: true
        title: qsTr("Save changes to %1?").arg(editor.projectName)
        contentItem: Label {
            text: qsTr("Your changes will be lost if you don't save them.")
            color: LingmoUI.Theme.secondaryTextColor
            font.pointSize: LingmoUI.Units.fontBody
            wrapMode: Text.WordWrap
        }
        footer: RowLayout {
            spacing: LingmoUI.Units.smallSpacing
            Item { Layout.fillWidth: true }
            LingmoUI.ActionButton {
                text: qsTr("Don't save")
                destructive: true
                subtle: true
                onClicked: { unsavedDialog.close(); root.closeConfirmed = true; root.close() }
            }
            LingmoUI.ActionButton { text: qsTr("Cancel"); onClicked: unsavedDialog.close() }
            LingmoUI.ActionButton {
                text: qsTr("Save")
                primary: true
                Layout.rightMargin: LingmoUI.Units.largeSpacing
                Layout.bottomMargin: LingmoUI.Units.largeSpacing
                onClicked: {
                    unsavedDialog.close()
                    if (editor.saveProject()) {
                        root.closeConfirmed = true
                        root.close()
                    } else {
                        saveDialog.thenClose = true
                        saveDialog.open()
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ menus
    Menu {
        id: fileMenu
        MenuItem { text: qsTr("New project"); onTriggered: root.showStart = true }
        MenuItem { text: qsTr("Open…"); onTriggered: openDialog.open() }
        Menu {
            title: qsTr("Open recent")
            enabled: editor.recentProjects.length > 0
            Repeater {
                model: editor.recentProjects
                MenuItem { text: modelData; onTriggered: editor.openPath(modelData) }
            }
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Save"); onTriggered: root.save() }
        MenuItem { text: qsTr("Save as…"); onTriggered: saveDialog.open() }
        MenuSeparator {}
        MenuItem { text: qsTr("Import media…"); onTriggered: importDialog.open() }
        MenuItem { text: qsTr("Import subtitles (SRT)…"); onTriggered: srtOpenDialog.open() }
        MenuItem { text: qsTr("Export subtitles (SRT)…"); onTriggered: srtSaveDialog.open() }
        MenuSeparator {}
        MenuItem { text: qsTr("Project settings…"); onTriggered: settingsDialog.open() }
        MenuItem {
            text: qsTr("Use proxies for playback")
            checkable: true
            checked: editor.useProxies
            onToggled: editor.useProxies = checked
        }
        MenuItem {
            text: qsTr("Make proxies for 4K media automatically")
            checkable: true
            checked: editor.autoProxy
            onToggled: editor.autoProxy = checked
        }
        MenuItem {
            text: qsTr("Dark interface")
            checkable: true
            checked: editor.darkUi
            onToggled: editor.darkUi = checked
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Quit"); onTriggered: root.close() }
    }

    // ------------------------------------------------------------ shortcuts
    readonly property var player: editor.player
    function typing() {
        return activeFocusItem && (activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit)
    }
    Shortcut { sequence: "Space"; enabled: !root.typing(); onActivated: player.togglePlay() }
    Shortcut { sequence: "J"; enabled: !root.typing(); onActivated: player.shuttle(-1) }
    Shortcut { sequence: "K"; enabled: !root.typing(); onActivated: player.shuttle(0) }
    Shortcut { sequence: "L"; enabled: !root.typing(); onActivated: player.shuttle(1) }
    Shortcut { sequence: "Left"; enabled: !root.typing(); onActivated: player.step(-1) }
    Shortcut { sequence: "Right"; enabled: !root.typing(); onActivated: player.step(1) }
    Shortcut { sequence: "Shift+Left"; enabled: !root.typing(); onActivated: player.step(-Math.round(editor.fps)) }
    Shortcut { sequence: "Shift+Right"; enabled: !root.typing(); onActivated: player.step(Math.round(editor.fps)) }
    Shortcut { sequence: "Up"; enabled: !root.typing(); onActivated: player.seek(editor.nextEdit(player.position, false)) }
    Shortcut { sequence: "Down"; enabled: !root.typing(); onActivated: player.seek(editor.nextEdit(player.position, true)) }
    Shortcut { sequence: "Home"; enabled: !root.typing(); onActivated: player.seek(0) }
    Shortcut { sequence: "End"; enabled: !root.typing(); onActivated: player.seek(player.duration - 1) }
    Shortcut { sequence: "I"; enabled: !root.typing(); onActivated: player.markInHere() }
    Shortcut { sequence: "O"; enabled: !root.typing(); onActivated: player.markOutHere() }
    Shortcut { sequence: "S"; enabled: !root.typing(); onActivated: editor.splitAtPlayhead() }
    Shortcut { sequence: "Ctrl+B"; enabled: !root.typing(); onActivated: editor.splitAtPlayhead() }
    Shortcut { sequence: "M"; enabled: !root.typing(); onActivated: editor.addMarker(player.timelinePosition) }
    Shortcut { sequence: "Delete"; enabled: !root.typing(); onActivated: editor.deleteSelected(root.rippleMode) }
    Shortcut { sequence: "Backspace"; enabled: !root.typing(); onActivated: editor.deleteSelected(root.rippleMode) }
    Shortcut { sequence: "Shift+Delete"; enabled: !root.typing(); onActivated: editor.deleteSelected(true) }
    Shortcut { sequence: "Ctrl+D"; enabled: !root.typing(); onActivated: editor.addTransitionAtPlayhead("dissolve", Math.round(editor.fps / 2)) }
    Shortcut { sequence: "Shift+F"; enabled: !root.typing(); onActivated: editor.freezeFrame() }
    Shortcut { sequence: "A"; enabled: !root.typing(); onActivated: root.tool = "select" }
    Shortcut { sequence: "B"; enabled: !root.typing(); onActivated: root.tool = "razor" }
    Shortcut { sequence: "R"; enabled: !root.typing(); onActivated: root.tool = "roll" }
    Shortcut { sequence: "Y"; enabled: !root.typing(); onActivated: root.tool = "slip" }
    Shortcut { sequence: "U"; enabled: !root.typing(); onActivated: root.tool = "slide" }
    Shortcut { sequence: "N"; enabled: !root.typing(); onActivated: root.snapping = !root.snapping }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !root.typing(); onActivated: editor.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; enabled: !root.typing(); onActivated: editor.redo() }
    Shortcut { sequences: [StandardKey.Save]; onActivated: root.save() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: saveDialog.open() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: openDialog.open() }
    Shortcut { sequences: [StandardKey.New]; onActivated: root.showStart = true }
    Shortcut { sequence: "Ctrl+I"; onActivated: importDialog.open() }
    Shortcut { sequence: "Ctrl+A"; enabled: !root.typing(); onActivated: editor.selectAll() }
    Shortcut { sequence: "Escape"; enabled: !root.typing(); onActivated: editor.clearSelection() }
    Shortcut { sequence: "Ctrl+M"; enabled: !root.typing(); onActivated: root.page = 4 }
    Shortcut { sequence: "Shift+1"; onActivated: root.page = 0 }
    Shortcut { sequence: "Shift+2"; onActivated: root.page = 1 }
    Shortcut { sequence: "Shift+3"; onActivated: root.page = 2 }
    Shortcut { sequence: "Shift+4"; onActivated: root.page = 3 }
    Shortcut { sequence: "Shift+5"; onActivated: root.page = 4 }

    // Files dragged in from the file manager
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: function(drop) {
            if (drop.hasUrls) {
                editor.importUrls(drop.urls)
                root.showStart = false
            }
        }
    }

    // ------------------------------------------------------------ layout
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: LingmoUI.Units.smallSpacing
        anchors.rightMargin: LingmoUI.Units.smallSpacing
        anchors.bottomMargin: LingmoUI.Units.smallSpacing
        spacing: LingmoUI.Units.smallSpacing

        // Top strip, shared with the window buttons on the left
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: root.header.height
            spacing: LingmoUI.Units.smallSpacing

            Item { Layout.preferredWidth: LingmoUI.Units.windowControlsInset }
            LingmoUI.IconButton {
                iconName: "open-menu-symbolic"
                tipText: qsTr("Menu")
                onClicked: fileMenu.popup(this, 0, height)
            }
            Label {
                text: "Lingmo Studio"
                font.pointSize: LingmoUI.Units.fontSubheading
                font.bold: true
                color: LingmoUI.Theme.textColor
            }
            Label {
                text: "· " + editor.projectName + (editor.dirty ? " •" : "")
                font.pointSize: LingmoUI.Units.fontBody
                color: LingmoUI.Theme.secondaryTextColor
                elide: Text.ElideRight
                Layout.maximumWidth: 260
            }
            Item { Layout.fillWidth: true }
            LingmoUI.SegmentedControl {
                id: pages
                model: [
                    { text: qsTr("Edit"), iconName: "video-x-generic" },
                    { text: qsTr("Color"), iconName: "color-management" },
                    { text: qsTr("Effects"), iconName: "draw-bezier-curves" },
                    { text: qsTr("Audio"), iconName: "audio-volume-high" },
                    { text: qsTr("Deliver"), iconName: "document-export" }
                ]
                currentIndex: root.page
                onActivated: function(index) { root.page = index; root.showStart = false }
            }
            Item { Layout.fillWidth: true }
            LingmoUI.Spinner {
                running: editor.busyTask.length > 0 || editor.mediaBin.importing > 0 || editor.renderQueue.running
                visible: running
                size: LingmoUI.Units.iconSizes.small
            }
            LingmoUI.IconButton {
                iconName: "edit-undo"
                tipText: editor.canUndo ? qsTr("Undo %1").arg(editor.undoText) : qsTr("Undo")
                enabled: editor.canUndo
                onClicked: editor.undo()
            }
            LingmoUI.IconButton {
                iconName: "edit-redo"
                tipText: editor.canRedo ? qsTr("Redo %1").arg(editor.redoText) : qsTr("Redo")
                enabled: editor.canRedo
                onClicked: editor.redo()
            }
            LingmoUI.IconButton {
                iconName: "document-save"
                tipText: qsTr("Save (Ctrl+S)")
                onClicked: root.save()
            }
            LingmoUI.ActionButton {
                text: qsTr("Export")
                iconName: "document-export"
                primary: true
                onClicked: { root.page = 4; root.showStart = false }
            }
            Item { Layout.preferredWidth: LingmoUI.Units.smallSpacing }
        }

        LingmoUI.Banner {
            Layout.fillWidth: true
            type: LingmoUI.Banner.Warning
            visible: editor.recoveryPath.length > 0
            title: visible ? qsTr("Recover unsaved work?") : ""
            text: visible ? qsTr("Lingmo Studio closed without saving a project. Its last autosave can be restored.") : ""
            closable: false
            LingmoUI.ActionButton { text: qsTr("Discard"); onClicked: editor.discardRecovery() }
            LingmoUI.ActionButton { text: qsTr("Recover"); primary: true; onClicked: { editor.recover(); root.showStart = false } }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.showStart ? 5 : root.page

            EditPage { }
            ColorPage { active: root.page === 1 && !root.showStart }
            EffectsPage { }
            AudioPage { }
            DeliverPage { }
            StartPage {
                onDone: root.showStart = false
                onOpenRequested: openDialog.open()
            }
        }

        // Status line
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: LingmoUI.Units.smallSpacing
            spacing: LingmoUI.Units.largeSpacing
            Label {
                Layout.fillWidth: true
                text: editor.busyTask.length > 0 ? editor.busyTask : editor.status
                color: LingmoUI.Theme.secondaryTextColor
                font.pointSize: LingmoUI.Units.fontCaption
                elide: Text.ElideRight
            }
            Label {
                text: editor.projectWidth + "×" + editor.projectHeight + " · " + Number(editor.fps).toLocaleString(Qt.locale(), 'f', editor.fps % 1 ? 2 : 0) + " fps"
                color: LingmoUI.Theme.secondaryTextColor
                font.pointSize: LingmoUI.Units.fontCaption
                MouseArea { anchors.fill: parent; onClicked: settingsDialog.open() }
            }
            Label {
                text: editor.hwDetected ? (editor.hwEncoders.length > 0 ? qsTr("GPU encode: %1").arg(editor.hwEncoders.join(", ")) : qsTr("GPU encode: none")) : qsTr("Checking GPU encoders…")
                color: LingmoUI.Theme.secondaryTextColor
                font.pointSize: LingmoUI.Units.fontCaption
            }
        }
    }
}
