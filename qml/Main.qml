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

    background.opacity: 1
    background.color: StudioTheme.window
    header.height: LingmoUI.Units.windowHeaderHeight
    contentTopMargin: 0

    // Pages along the bottom, in the order of a finishing workflow
    readonly property var pages: [
        { id: "media", label: qsTr("Media"), icon: "page-media", shortcut: "Shift+2" },
        { id: "cut", label: qsTr("Cut"), icon: "page-cut", shortcut: "Shift+3" },
        { id: "edit", label: qsTr("Edit"), icon: "page-edit", shortcut: "Shift+4" },
        { id: "effects", label: qsTr("Effects"), icon: "page-effects", shortcut: "Shift+5" },
        { id: "color", label: qsTr("Color"), icon: "page-color", shortcut: "Shift+6" },
        { id: "audio", label: qsTr("Audio"), icon: "page-audio", shortcut: "Shift+7" },
        { id: "deliver", label: qsTr("Deliver"), icon: "page-deliver", shortcut: "Shift+8" }
    ]
    readonly property var pageIds: pages.map(function(p) { return p.id })
    readonly property int deliverPage: pageIds.indexOf("deliver")
    property int page: pageIds.indexOf(startPage) >= 0 ? pageIds.indexOf(startPage) : pageIds.indexOf("edit")
    property bool showStart: startPage.length === 0 && editor.mediaBin.count === 0 && editor.timeline.duration <= 1
    property bool showShorts: startPage === "shorts"

    function goToPage(index) {
        page = index
        showStart = false
        showShorts = false
    }
    function openCaptions(clipId) { captionsDialog.openFor(clipId) }
    function openShorts(mediaId) {
        if (mediaId)
            editor.shorts.mediaId = mediaId
        showStart = false
        showShorts = true
    }
    property bool closeConfirmed: false
    // Edit tools: select, razor, ripple, roll, slip, slide
    property string tool: "select"
    property bool snapping: true
    property bool rippleMode: false
    property bool linkedSelection: true

    // Editors are dark by convention: judging picture and colour needs a
    // neutral, dim surround. The studio is charcoal whatever the system
    // theme, and the shared Lingmo components take its palette.
    function applyTheme() {
        LingmoTheme.darkMode = LingmoThemeType.Dark
        LingmoUI.Theme.darkMode = true
        LingmoUI.Theme.backgroundColor = StudioTheme.window
        LingmoUI.Theme.secondBackgroundColor = StudioTheme.panel
        LingmoUI.Theme.alternateBackgroundColor = StudioTheme.raised
        LingmoUI.Theme.textColor = StudioTheme.text
        LingmoUI.Theme.secondaryTextColor = StudioTheme.textSecondary
        LingmoUI.Theme.disabledTextColor = StudioTheme.textDisabled
        LingmoUI.Theme.highlightColor = StudioTheme.accent
        LingmoUI.Theme.borderColor = StudioTheme.border
        LingmoUI.Theme.dividerColor = StudioTheme.divider
        LingmoUI.Theme.blueColor = StudioTheme.trackVideo
        LingmoUI.Theme.greenColor = StudioTheme.trackAudio
    }
    Component.onCompleted: {
        applyTheme()
        if (startPage === "captions")
            Qt.callLater(function() { captionsDialog.openFor("") })
    }
    Connections {
        target: editor
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

    // ------------------------------------------------------------ edits shared by the pages
    // Three-point edits from the source viewer (Resolve's F9, F10, F12, Shift+F12)
    function topVideoTrack() {
        const tl = editor.timeline
        for (let t = 0; t < tl.trackCount; ++t)
            if (tl.trackKind(t) === "video")
                return t
        return -1
    }
    function editFromSource(mode) {
        const p = editor.player
        const id = p.sourceMediaId
        if (!id) {
            root.showPassiveNotification(qsTr("Choose a clip in the media pool first"))
            return
        }
        const inF = p.sourceIn >= 0 ? p.sourceIn : 0
        const outF = p.sourceOut >= 0 ? p.sourceOut : -1
        let at = p.timelinePosition
        let track = -1
        let ripple = false
        if (mode === "append") {
            at = editor.timeline.duration
        } else if (mode === "insert") {
            ripple = true
        } else if (mode === "top") {
            track = topVideoTrack()
            const end = outF >= 0 ? at + outF - inF + 1 : Number.MAX_SAFE_INTEGER
            const clips = track >= 0 ? editor.timeline.clipsOf(track) : []
            const busy = clips.some(function(c) { return c.position < end && c.position + c.duration > at })
            if (track < 0 || busy) {
                editor.addTrack("video")
                track = topVideoTrack()
            }
        }
        p.setMode("timeline")
        editor.addMediaToTimeline(id, track, at, inF, outF, ripple)
    }
    // Trims the nearer edge of the clip under the playhead to the playhead,
    // closing the gap (ripple)
    function smartTrim() {
        const at = editor.player.timelinePosition
        let id = ""
        const sel = editor.selectedClip
        if (sel && sel.id !== undefined && sel.position < at && sel.position + sel.duration > at)
            id = sel.id
        const tl = editor.timeline
        for (let t = 0; t < tl.trackCount && !id; ++t)
            if (tl.trackKind(t) === "video")
                id = editor.clipAt(t, at)
        for (let t = 0; t < tl.trackCount && !id; ++t)
            id = editor.clipAt(t, at)
        if (!id) {
            root.showPassiveNotification(qsTr("Move the playhead over a clip to trim it"))
            return
        }
        const c = editor.clipInfo(id)
        if (at <= c.position || at >= c.position + c.duration)
            return
        const left = at - c.position < c.position + c.duration - at
        editor.trimClip(id, left, at, true)
    }

    // ------------------------------------------------------------ file dialogs
    FileDialog {
        id: openDialog
        title: qsTr("Open project")
        nameFilters: [qsTr("MLT projects (*.mlt)"), qsTr("All files (*)")]
        onAccepted: { if (editor.openProject(selectedFile)) root.showStart = false }
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
    function importMedia() { importDialog.open() }
    FileDialog {
        id: srtOpenDialog
        title: qsTr("Import subtitles")
        nameFilters: [qsTr("Subtitles (*.srt)")]
        onAccepted: editor.importSubtitles(selectedFile)
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
    function openProjectDialog() { openDialog.open() }

    ProjectSettingsDialog { id: settingsDialog }
    CaptionsDialog { id: captionsDialog }
    FileDialog {
        id: captionExportDialog
        title: qsTr("Export captions")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "srt"
        nameFilters: [qsTr("SubRip (*.srt)"), qsTr("WebVTT (*.vtt)"), qsTr("Advanced SubStation (*.ass)")]
        onAccepted: {
            const f = String(selectedFile)
            editor.exportCaptions(selectedFile, f.endsWith(".vtt") ? "vtt" : f.endsWith(".ass") ? "ass" : "srt")
        }
    }

    Dialog {
        id: unsavedDialog
        anchors.centerIn: parent
        width: 460
        modal: true
        title: qsTr("Save changes to %1?").arg(editor.projectName)
        contentItem: Label {
            text: qsTr("Your changes will be lost if you don't save them.")
            color: StudioTheme.textSecondary
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

    // ------------------------------------------------------------ menu
    Menu {
        id: fileMenu
        MenuItem { text: qsTr("New project"); onTriggered: root.showStart = true }
        MenuItem { text: qsTr("Open…"); onTriggered: openDialog.open() }
        Menu {
            title: qsTr("Open recent")
            enabled: editor.recentProjects.length > 0
            Repeater {
                model: editor.recentProjects
                MenuItem { text: modelData; onTriggered: { if (editor.openPath(modelData)) root.showStart = false } }
            }
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Save"); onTriggered: root.save() }
        MenuItem { text: qsTr("Save as…"); onTriggered: saveDialog.open() }
        MenuSeparator {}
        MenuItem { text: qsTr("Import media…"); onTriggered: importDialog.open() }
        MenuItem { text: qsTr("Import subtitles (SRT)…"); onTriggered: srtOpenDialog.open() }
        MenuItem { text: qsTr("Export captions (SRT, VTT, ASS)…"); onTriggered: captionExportDialog.open() }
        MenuSeparator {}
        MenuItem { text: qsTr("Generate cuts (Shorts/Reels)…"); onTriggered: root.openShorts("") }
        MenuItem { text: qsTr("Automatic captions…"); onTriggered: root.openCaptions("") }
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
    Shortcut { sequences: ["T", "R"]; enabled: !root.typing(); onActivated: root.tool = "roll" }
    Shortcut { sequence: "Y"; enabled: !root.typing(); onActivated: root.tool = "slip" }
    Shortcut { sequence: "U"; enabled: !root.typing(); onActivated: root.tool = "slide" }
    Shortcut { sequence: "N"; enabled: !root.typing(); onActivated: root.snapping = !root.snapping }
    Shortcut { sequence: "Ctrl+Shift+L"; enabled: !root.typing(); onActivated: root.linkedSelection = !root.linkedSelection }
    Shortcut { sequence: "F9"; onActivated: root.editFromSource("insert") }
    Shortcut { sequence: "F10"; onActivated: root.editFromSource("overwrite") }
    Shortcut { sequence: "F12"; onActivated: root.editFromSource("top") }
    Shortcut { sequence: "Shift+F12"; onActivated: root.editFromSource("append") }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !root.typing(); onActivated: editor.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; enabled: !root.typing(); onActivated: editor.redo() }
    Shortcut { sequences: [StandardKey.Save]; onActivated: root.save() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: saveDialog.open() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: openDialog.open() }
    Shortcut { sequences: [StandardKey.New]; onActivated: root.showStart = true }
    Shortcut { sequence: "Ctrl+I"; onActivated: importDialog.open() }
    Shortcut { sequence: "Ctrl+A"; enabled: !root.typing(); onActivated: editor.selectAll() }
    Shortcut { sequence: "Escape"; enabled: !root.typing(); onActivated: editor.clearSelection() }
    Shortcut { sequence: "Ctrl+M"; enabled: !root.typing(); onActivated: root.goToPage(root.deliverPage) }
    Shortcut { sequence: "Shift+1"; onActivated: { root.showShorts = false; root.showStart = true } }
    Shortcut { sequence: "Shift+2"; onActivated: root.goToPage(0) }
    Shortcut { sequence: "Shift+3"; onActivated: root.goToPage(1) }
    Shortcut { sequence: "Shift+4"; onActivated: root.goToPage(2) }
    Shortcut { sequence: "Shift+5"; onActivated: root.goToPage(3) }
    Shortcut { sequence: "Shift+6"; onActivated: root.goToPage(4) }
    Shortcut { sequence: "Shift+7"; onActivated: root.goToPage(5) }
    Shortcut { sequence: "Shift+8"; onActivated: root.goToPage(6) }

    // Files dragged in from the file manager
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: function(drop) {
            if (drop.hasUrls) {
                // on the cuts page a dropped video becomes the one to cut
                editor.importUrls(drop.urls)
                if (!root.showShorts)
                    root.showStart = false
            }
        }
    }

    // ------------------------------------------------------------ layout
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Top bar: menu, undo/redo | project and save state | export.
        // It shares the strip with the window buttons on the left.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.header.height

            RowLayout {
                anchors.left: parent.left
                anchors.leftMargin: LingmoUI.Units.windowControlsInset
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                StudioButton { iconName: "menu"; tip: qsTr("Menu"); onClicked: fileMenu.popup(this, 0, height) }
                Rectangle { width: 1; height: 16; color: StudioTheme.border; Layout.leftMargin: 4; Layout.rightMargin: 4 }
                StudioButton {
                    iconName: "undo"
                    tip: editor.canUndo ? qsTr("Undo %1").arg(editor.undoText) : qsTr("Undo")
                    shortcut: "Ctrl+Z"
                    enabled: editor.canUndo
                    onClicked: editor.undo()
                }
                StudioButton {
                    iconName: "redo"
                    tip: editor.canRedo ? qsTr("Redo %1").arg(editor.redoText) : qsTr("Redo")
                    shortcut: "Ctrl+Shift+Z"
                    enabled: editor.canRedo
                    onClicked: editor.redo()
                }
            }

            // project name and save state, centred like a title
            Row {
                anchors.centerIn: parent
                spacing: 10
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: editor.projectName
                    color: StudioTheme.text
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontLarge
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                    width: Math.min(implicitWidth, 360)
                }
                AbstractButton {
                    id: saveState
                    anchors.verticalCenter: parent.verticalCenter
                    hoverEnabled: true
                    width: stateRow.implicitWidth + 16
                    height: 22
                    onClicked: root.save()
                    ToolTip.visible: hovered
                    ToolTip.text: StudioTheme.tip(qsTr("Save"), "Ctrl+S")
                    ToolTip.delay: 450
                    background: Rectangle {
                        radius: 11
                        color: saveState.hovered ? StudioTheme.raisedHover : StudioTheme.raised
                        Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
                    }
                    contentItem: Item {
                        Row {
                            id: stateRow
                            anchors.centerIn: parent
                            spacing: 6
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 6
                                height: 6
                                radius: 3
                                color: editor.dirty ? StudioTheme.warning : editor.projectPath.length ? StudioTheme.success : StudioTheme.textDisabled
                                Behavior on color { ColorAnimation { duration: StudioTheme.normal } }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: !editor.projectPath.length ? qsTr("Not saved yet") : editor.dirty ? qsTr("Unsaved changes") : qsTr("Saved")
                                color: StudioTheme.textSecondary
                                font.family: StudioTheme.font
                                font.pixelSize: StudioTheme.fontSmall
                            }
                        }
                    }
                }
            }

            RowLayout {
                anchors.right: parent.right
                anchors.rightMargin: StudioTheme.pad
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                LingmoUI.Spinner {
                    running: editor.busyTask.length > 0 || editor.mediaBin.importing > 0 || editor.renderQueue.running
                    visible: running
                    size: 16
                }
                StudioButton {
                    text: qsTr("Export")
                    iconName: "export"
                    iconSize: 14
                    size: 26
                    primary: true
                    tip: qsTr("Open the Deliver page")
                    shortcut: "Ctrl+M"
                    onClicked: root.goToPage(root.deliverPage)
                }
            }
        }

        LingmoUI.Banner {
            Layout.fillWidth: true
            Layout.leftMargin: StudioTheme.gap
            Layout.rightMargin: StudioTheme.gap
            Layout.bottomMargin: StudioTheme.gap
            type: LingmoUI.Banner.Warning
            visible: editor.recoveryPath.length > 0
            title: visible ? qsTr("Recover unsaved work?") : ""
            text: visible ? qsTr("Lingmo Studio closed without saving a project. Its last autosave can be restored.") : ""
            closable: false
            LingmoUI.ActionButton { text: qsTr("Discard"); onClicked: editor.discardRecovery() }
            LingmoUI.ActionButton { text: qsTr("Recover"); primary: true; onClicked: { editor.recover(); root.showStart = false } }
        }

        StackLayout {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: StudioTheme.gap
            Layout.rightMargin: StudioTheme.gap
            Layout.bottomMargin: StudioTheme.gap
            currentIndex: root.showShorts ? 8 : root.showStart ? 7 : root.page
            onCurrentIndexChanged: pageFade.restart()
            NumberAnimation { id: pageFade; target: stack; property: "opacity"; from: 0.35; to: 1; duration: StudioTheme.normal; easing.type: Easing.OutCubic }

            MediaPage { }
            CutPage { }
            EditPage { }
            EffectsPage { }
            ColorPage { active: root.page === 4 && !root.showStart && !root.showShorts }
            AudioPage { }
            DeliverPage { }
            StartPage {
                onDone: root.showStart = false
                onOpenRequested: openDialog.open()
                onShortsRequested: root.openShorts("")
            }
            ShortsPage {
                onBack: root.showShorts = false
            }
        }

        PageBar {
            Layout.fillWidth: true
            pages: root.pages
            currentIndex: root.page
            dimmed: root.showStart || root.showShorts
            onActivated: function(index) { root.goToPage(index) }
            onHomeRequested: { root.showShorts = false; root.showStart = true }
            onFormatClicked: settingsDialog.open()
        }
    }
}
