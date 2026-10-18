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
import Lingmo.Studio 1.0
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// "Gerar cortes": pick a long video and the platforms, get scored highlight
// cards, play/trim/select them, and turn them into vertical sequences.
Item {
    id: page
    signal back()
    // a cut opened on the timeline: show the Edit page
    signal openEditor()

    readonly property var shorts: editor.shorts
    readonly property var studioEditor: editor
    property string playingId: ""
    // media can change without the bin's count changing (project size, proxies)
    property int mediaRevision: 0
    readonly property var mediaList: editor.mediaBin.count >= 0 && mediaRevision >= 0 && visible ? shorts.mediaChoices() : []
    Connections {
        target: editor
        function onDocumentChanged() { page.mediaRevision++ }
        function onSettingsChanged() { page.mediaRevision++ }
        function onProjectChanged() { page.mediaRevision++ }
    }

    readonly property var currentMedia: {
        for (let i = 0; i < mediaList.length; ++i)
            if (mediaList[i].id === shorts.mediaId)
                return mediaList[i]
        return null
    }

    // The export of the generated cuts (also from the top bar's Export and Deliver)
    function openExport() {
        page.shorts.captionOutput = "video" // burned in, unless chosen otherwise for this export
        exportDialog.open()
    }

    onVisibleChanged: {
        if (!visible) {
            playingId = ""
            shorts.stopPreview()
        } else if (shorts.mediaId === "" && mediaList.length > 0) {
            shorts.mediaId = mediaList[mediaList.length - 1].id
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("Choose a long video")
        fileMode: FileDialog.OpenFile
        nameFilters: editor.mediaNameFilters()
        onAccepted: editor.importUrls([selectedFile])
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Export the cuts to")
        currentFolder: page.shorts.exportFolderUrl
        onAccepted: page.shorts.exportFolder = selectedFolder.toString()
    }

    Dialog {
        id: exportDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        width: Math.min(Overlay.overlay ? Overlay.overlay.width - 80 : 560, 560)
        title: qsTr("Export cuts")
        padding: LingmoUI.Units.largeSpacing

        readonly property int cutCount: editor.shortSequences.length > 0 ? editor.shortSequences.length : page.shorts.selectedCount
        readonly property int fileCount: {
            if (editor.shortSequences.length === 0)
                return page.shorts.selectedCount * page.shorts.selectedPlatforms.length
            let n = 0
            for (let i = 0; i < editor.shortSequences.length; ++i)
                n += (editor.shortSequences[i].platforms || []).length
            return n
        }

        contentItem: ColumnLayout {
            spacing: LingmoUI.Units.largeSpacing

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("%n cut(s)", "", exportDialog.cutCount) + " · " + qsTr("%n MP4 file(s), one per platform", "", exportDialog.fileCount)
                font.pointSize: LingmoUI.Units.fontBody
                color: LingmoUI.Theme.textColor
            }
            LingmoUI.Banner {
                Layout.fillWidth: true
                visible: editor.shortSequences.length === 0 && page.shorts.selectedCount > 0
                type: LingmoUI.Banner.Info
                closable: false
                text: visible ? qsTr("The %n selected cut(s) are created first.", "", page.shorts.selectedCount) : ""
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { text: qsTr("Folder"); font.pointSize: LingmoUI.Units.fontBody; font.bold: true; color: LingmoUI.Theme.textColor }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        Layout.fillWidth: true
                        readOnly: true
                        text: page.shorts.exportFolder
                    }
                    LingmoUI.ActionButton { text: qsTr("Choose…"); iconName: "folder-open"; onClicked: folderDialog.open() }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { text: qsTr("Captions"); font.pointSize: LingmoUI.Units.fontBody; font.bold: true; color: LingmoUI.Theme.textColor }
                ButtonGroup { id: captionGroup }
                Repeater {
                    model: [
                        { id: "video", text: qsTr("In the video (default)"), note: qsTr("Burned into the picture: they show on every platform and player.") },
                        { id: "video+srt", text: qsTr("In the video + .srt file"), note: qsTr("Also a .srt next to each MP4, for platforms that take caption files.") },
                        { id: "srt", text: qsTr("Only a .srt file"), note: qsTr("The video without captions and a .srt next to it.") }
                    ]
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        RadioButton {
                            id: option
                            Layout.fillWidth: true
                            ButtonGroup.group: captionGroup
                            text: modelData.text
                            checked: page.shorts.captionOutput === modelData.id
                            onToggled: if (checked) page.shorts.captionOutput = modelData.id
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.leftMargin: option.leftPadding + option.indicator.width + option.spacing
                            text: modelData.note
                            wrapMode: Text.WordWrap
                            font.pointSize: LingmoUI.Units.fontCaption
                            color: LingmoUI.Theme.secondaryTextColor
                        }
                    }
                }
            }
        }

        footer: RowLayout {
            spacing: LingmoUI.Units.smallSpacing
            Item { Layout.fillWidth: true }
            LingmoUI.ActionButton {
                text: qsTr("Cancel")
                Layout.bottomMargin: LingmoUI.Units.largeSpacing
                onClicked: exportDialog.close()
            }
            LingmoUI.ActionButton {
                text: qsTr("Export")
                iconName: "document-export"
                primary: true
                enabled: exportDialog.cutCount > 0 && !page.shorts.exporting
                Layout.rightMargin: LingmoUI.Units.largeSpacing
                Layout.bottomMargin: LingmoUI.Units.largeSpacing
                onClicked: {
                    page.shorts.exportCuts("", page.shorts.captionOutput)
                    exportDialog.close()
                }
            }
        }
    }
    Connections {
        target: editor.mediaBin
        function onCountChanged() {
            const list = page.shorts.mediaChoices()
            if (list.length > 0 && page.visible)
                page.shorts.mediaId = list[list.length - 1].id // the one just imported
        }
    }
    Connections {
        target: editor.player
        function onPlayingChanged() { if (!editor.player.playing) page.playingId = "" }
    }

    RowLayout {
        anchors.fill: parent
        spacing: StudioTheme.gap

        // ------------------------------------------------ setup
        StudioPanel {
            Layout.preferredWidth: 380
            Layout.fillHeight: true

            ScrollView {
                id: setupScroll
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.largeSpacing
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    width: setupScroll.availableWidth
                    spacing: LingmoUI.Units.smallSpacing

                    LingmoUI.PageHeader {
                        Layout.fillWidth: true
                        title: qsTr("Generate cuts")
                        subtitle: qsTr("Shorts, Reels and TikToks from a long video")
                        backVisible: true
                        onBack: page.back()
                    }

                    Label { text: qsTr("Video"); font.pointSize: LingmoUI.Units.fontBody; font.bold: true; color: LingmoUI.Theme.textColor }
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            id: mediaBox
                            Layout.fillWidth: true
                            model: page.mediaList
                            textRole: "name"
                            valueRole: "id"
                            displayText: page.mediaList.length ? currentText : qsTr("No video in the project")
                            currentIndex: indexOfValue(page.shorts.mediaId)
                            onActivated: page.shorts.mediaId = currentValue
                        }
                        LingmoUI.IconButton { iconName: "document-open"; tipText: qsTr("Import a video"); onClicked: importDialog.open() }
                    }
                    Label {
                        visible: mediaBox.currentIndex >= 0 && page.mediaList.length > 0
                        text: visible ? qsTr("Length %1").arg(page.mediaList[Math.max(0, mediaBox.currentIndex)].duration) : ""
                        font.pointSize: LingmoUI.Units.fontCaption
                        color: LingmoUI.Theme.secondaryTextColor
                    }

                    Label { text: qsTr("Platforms"); font.pointSize: LingmoUI.Units.fontBody; font.bold: true; color: LingmoUI.Theme.textColor; Layout.topMargin: LingmoUI.Units.smallSpacing }
                    Flow {
                        Layout.fillWidth: true
                        spacing: LingmoUI.Units.smallSpacing
                        Repeater {
                            model: page.shorts.platforms
                            LingmoUI.Card {
                                readonly property bool on: page.shorts.selectedPlatforms.indexOf(modelData.id) >= 0
                                width: (setupScroll.availableWidth - LingmoUI.Units.smallSpacing) / 2
                                height: 52
                                clickable: true
                                highlighted: on
                                radius: LingmoUI.Theme.smallRadius
                                onClicked: page.shorts.togglePlatform(modelData.id)
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: LingmoUI.Units.largeSpacing
                                    anchors.rightMargin: LingmoUI.Units.smallSpacing
                                    spacing: 0
                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        font.pointSize: LingmoUI.Units.fontBody
                                        font.bold: parent.parent.on
                                        color: LingmoUI.Theme.textColor
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("%1 · up to %2").arg(modelData.aspect).arg(page.shorts.formatTime(modelData.maxSeconds * 1000))
                                        font.pointSize: LingmoUI.Units.fontCaption
                                        color: LingmoUI.Theme.secondaryTextColor
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("Cuts of %1 to %2 seconds, on sentence boundaries.").arg(page.shorts.minSeconds).arg(page.shorts.maxSeconds)
                        font.pointSize: LingmoUI.Units.fontCaption
                        color: LingmoUI.Theme.secondaryTextColor
                    }
                    LingmoUI.Banner {
                        Layout.fillWidth: true
                        type: LingmoUI.Banner.Info
                        closable: false
                        visible: page.shorts.mixedAspects
                        text: visible ? qsTr("These platforms use different shapes. Cuts are edited in the first one's shape; the export reframes each file for its platform.") : ""
                    }

                    Label { text: qsTr("Options"); font.pointSize: LingmoUI.Units.fontBody; font.bold: true; color: LingmoUI.Theme.textColor; Layout.topMargin: LingmoUI.Units.smallSpacing }
                    RowLayout {
                        Label { text: qsTr("How many cuts"); Layout.fillWidth: true; font.pointSize: LingmoUI.Units.fontBody; color: LingmoUI.Theme.textColor }
                        SpinBox { from: 1; to: 20; value: page.shorts.count; onValueModified: page.shorts.count = value }
                    }
                    Switch {
                        Layout.fillWidth: true
                        text: qsTr("Captions synced to the speech")
                        checked: page.shorts.captions
                        onToggled: page.shorts.captions = checked
                    }
                    ComboBox {
                        Layout.fillWidth: true
                        visible: page.shorts.captions
                        model: editor.captionStyles
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: indexOfValue(page.shorts.captionStyle)
                        onActivated: page.shorts.captionStyle = currentValue
                    }
                    Switch {
                        Layout.fillWidth: true
                        text: qsTr("Reframe following faces")
                        checked: page.shorts.reframe
                        onToggled: page.shorts.reframe = checked
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: page.shorts.reframe && !page.shorts.opencvAvailable
                        wrapMode: Text.WordWrap
                        text: qsTr("Face tracking needs OpenCV for Python (python-opencv). Without it the crop stays centered; move it with Position X keyframes.")
                        font.pointSize: LingmoUI.Units.fontCaption
                        color: LingmoUI.Theme.secondaryTextColor
                    }

                    TranscriptionStatus { Layout.fillWidth: true; Layout.topMargin: LingmoUI.Units.smallSpacing }

                    LingmoUI.ActionButton {
                        Layout.fillWidth: true
                        Layout.topMargin: LingmoUI.Units.smallSpacing
                        text: page.shorts.candidates.length ? qsTr("Find cuts again") : qsTr("Find the best moments")
                        iconName: "edit-find"
                        primary: true
                        busy: page.shorts.running
                        enabled: editor.transcriber.available && page.shorts.mediaId !== "" && page.shorts.selectedPlatforms.length > 0
                        onClicked: page.shorts.analyze()
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: page.shorts.running
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: page.shorts.stage
                            font.pointSize: LingmoUI.Units.fontCaption
                            color: LingmoUI.Theme.secondaryTextColor
                            elide: Text.ElideRight
                        }
                        RowLayout {
                            ProgressBar {
                                Layout.fillWidth: true
                                from: 0; to: 1
                                value: Math.max(0, page.shorts.progress)
                                indeterminate: page.shorts.progress < 0
                            }
                            LingmoUI.ActionButton { text: qsTr("Cancel"); onClicked: page.shorts.cancel() }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: editor.transcriber.running
                            text: editor.transcriber.timing
                            wrapMode: Text.WordWrap
                            font.pointSize: LingmoUI.Units.fontCaption
                            color: LingmoUI.Theme.secondaryTextColor
                        }
                    }
                    LingmoUI.Banner {
                        Layout.fillWidth: true
                        type: LingmoUI.Banner.Error
                        visible: page.shorts.error.length > 0 && !page.shorts.running
                        text: visible ? page.shorts.error : ""
                    }
                }
            }
        }

        // ------------------------------------------------ results
        StudioPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.largeSpacing
                spacing: LingmoUI.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: page.shorts.candidates.length ? qsTr("Suggested cuts") : qsTr("Cuts")
                        font.pointSize: LingmoUI.Units.fontHeading
                        font.bold: true
                        color: LingmoUI.Theme.textColor
                    }
                    LingmoUI.Badge {
                        visible: page.shorts.candidates.length > 0
                        text: page.shorts.usedLlm ? qsTr("Scored by %1 (local)").arg(page.shorts.llmModel) : qsTr("Scored by sound, speech and scenes")
                        subtle: true
                    }
                }

                // ---- the generated cuts, with their export
                ColumnLayout {
                    Layout.fillWidth: true
                    // all the room when there are no suggestions left
                    Layout.fillHeight: page.shorts.candidates.length === 0
                    visible: page.shorts.cuts.length > 0
                    spacing: LingmoUI.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Your cuts (%1)").arg(page.shorts.cuts.length)
                            font.pointSize: LingmoUI.Units.fontBody
                            font.bold: true
                            color: LingmoUI.Theme.textColor
                        }
                        Label {
                            Layout.fillWidth: true
                            text: page.shorts.exportSummary
                            font.pointSize: LingmoUI.Units.fontCaption
                            color: page.shorts.exportFailed > 0 ? LingmoUI.Theme.redColor : LingmoUI.Theme.secondaryTextColor
                            elide: Text.ElideMiddle
                        }
                        LingmoUI.ActionButton {
                            visible: page.shorts.exportDone
                            text: qsTr("Open folder")
                            iconName: "folder-open"
                            onClicked: page.shorts.openExportFolder()
                        }
                        LingmoUI.ActionButton {
                            text: qsTr("Export cuts…")
                            iconName: "document-export"
                            primary: !page.shorts.exportDone
                            busy: page.shorts.exporting
                            enabled: !page.shorts.exporting
                            onClicked: page.openExport()
                        }
                    }
                    ProgressBar {
                        Layout.fillWidth: true
                        visible: page.shorts.exporting
                        from: 0; to: 1
                        value: page.shorts.exportProgress
                        indeterminate: page.shorts.exportProgress <= 0
                    }
                    ListView {
                        id: cutList
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.shorts.candidates.length === 0 ? -1 : Math.min(contentHeight, 84 * 3 + spacing * 2)
                        Layout.fillHeight: page.shorts.candidates.length === 0
                        clip: true
                        spacing: 4
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        model: page.shorts.cuts
                        delegate: LingmoUI.Card {
                            id: cutRow
                            required property var modelData
                            readonly property string state: modelData.state
                            readonly property bool playing: page.playingId === "seq:" + modelData.id && editor.player.playing
                            width: cutList.width - LingmoUI.Units.smallSpacing
                            height: 84
                            radius: LingmoUI.Theme.smallRadius
                            highlighted: cutRow.playing
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: LingmoUI.Units.smallSpacing
                                anchors.leftMargin: LingmoUI.Units.largeSpacing
                                spacing: LingmoUI.Units.largeSpacing
                                // vertical thumbnail with play: the cut with its captions in the preview
                                Rectangle {
                                    Layout.preferredWidth: 40
                                    Layout.preferredHeight: 70
                                    radius: 4
                                    color: StudioTheme.well
                                    clip: true
                                    Image {
                                        anchors.fill: parent
                                        source: cutRow.modelData.thumbnail
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                    }
                                    LingmoUI.IconButton {
                                        anchors.centerIn: parent
                                        iconName: cutRow.playing ? "media-playback-pause" : "media-playback-start"
                                        tipText: qsTr("Play with captions")
                                        onClicked: {
                                            if (cutRow.playing) {
                                                editor.player.pause()
                                            } else {
                                                page.playingId = "seq:" + cutRow.modelData.id
                                                page.shorts.previewCut(cutRow.modelData.id)
                                            }
                                        }
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label {
                                        Layout.fillWidth: true
                                        text: cutRow.modelData.name
                                        font.pointSize: LingmoUI.Units.fontBody
                                        font.bold: true
                                        color: LingmoUI.Theme.textColor
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: cutRow.modelData.range + " · " + cutRow.modelData.length + " · " + cutRow.modelData.platformNames
                                        font.pointSize: LingmoUI.Units.fontCaption
                                        color: LingmoUI.Theme.secondaryTextColor
                                        elide: Text.ElideRight
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        LingmoUI.Spinner {
                                            visible: cutRow.state === "reframing" || cutRow.state === "running"
                                            running: visible
                                            size: 14
                                        }
                                        Label {
                                            text: ({ reframing: qsTr("Reframing on the faces…"), ready: qsTr("Ready"),
                                                     queued: qsTr("Waiting to export"), running: qsTr("Exporting %1%").arg(Math.round(cutRow.modelData.progress * 100)),
                                                     done: qsTr("Exported"), failed: qsTr("Export failed"), cancelled: qsTr("Export cancelled") })[cutRow.state] || ""
                                            font.pointSize: LingmoUI.Units.fontCaption
                                            color: cutRow.state === "failed" ? LingmoUI.Theme.redColor
                                                 : cutRow.state === "done" || cutRow.state === "ready" ? LingmoUI.Theme.greenColor : LingmoUI.Theme.secondaryTextColor
                                        }
                                        ProgressBar {
                                            Layout.fillWidth: true
                                            visible: cutRow.state === "running" || cutRow.state === "queued"
                                            from: 0; to: 1
                                            value: cutRow.modelData.progress
                                        }
                                    }
                                }
                                LingmoUI.ActionButton {
                                    text: qsTr("Open in the editor")
                                    quiet: true
                                    onClicked: {
                                        if (page.shorts.editCut(cutRow.modelData.id))
                                            page.openEditor()
                                    }
                                }
                                LingmoUI.ActionButton {
                                    text: qsTr("Export")
                                    iconName: "document-export"
                                    enabled: !page.shorts.exporting
                                    onClicked: page.shorts.exportCut(cutRow.modelData.id)
                                }
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: StudioTheme.border }
                }

                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: page.shorts.candidates.length > 0 || page.shorts.cuts.length === 0 || page.shorts.running
                    clip: true
                    spacing: LingmoUI.Units.smallSpacing
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    model: page.shorts.candidates
                    delegate: CandidateCard {
                        width: list.width - LingmoUI.Units.smallSpacing
                        cut: modelData
                        playing: page.playingId === modelData.id && editor.player.playing
                        onPlayRequested: {
                            if (playing) {
                                editor.player.pause()
                            } else {
                                page.playingId = modelData.id
                                page.shorts.preview(modelData.id)
                            }
                        }
                    }
                    LingmoUI.WheelHandler { target: list }

                    LingmoUI.EmptyState {
                        anchors.centerIn: parent
                        width: parent.width - LingmoUI.Units.largeSpacing * 4
                        visible: list.count === 0 && !page.shorts.running
                        iconName: "view-media-playlist"
                        title: page.mediaList.length ? qsTr("Ready to find the best moments") : qsTr("Bring a long video")
                        text: page.mediaList.length
                              ? qsTr("Lingmo Studio transcribes the video on this computer, splits it on sentences and scores each moment. You pick, trim and turn them into vertical clips.")
                              : qsTr("Import an interview, a class, a podcast or a live stream. Everything runs on this computer.")
                        actionText: page.mediaList.length ? "" : qsTr("Import video…")
                        onActionTriggered: importDialog.open()
                    }
                    LingmoUI.Spinner {
                        anchors.centerIn: parent
                        running: page.shorts.running && list.count === 0
                        visible: running
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: page.shorts.candidates.length > 0 || editor.shortSequences.length > 0
                    Label {
                        Layout.fillWidth: true
                        text: page.shorts.tracking ? qsTr("Following faces for the vertical crop…")
                                                   : page.shorts.candidates.length > 0 ? qsTr("%n cut(s) selected", "", page.shorts.selectedCount) : ""
                        font.pointSize: LingmoUI.Units.fontCaption
                        color: LingmoUI.Theme.secondaryTextColor
                    }
                    LingmoUI.ActionButton {
                        visible: page.shorts.candidates.length > 0
                        text: qsTr("Create %n cut(s)", "", page.shorts.selectedCount)
                        iconName: "edit-cut"
                        primary: editor.shortSequences.length === 0
                        enabled: page.shorts.selectedCount > 0 && !page.shorts.running
                        // the cuts appear above, ready to export or edit: the page stays
                        onClicked: {
                            page.playingId = ""
                            page.shorts.createCuts()
                        }
                    }
                }
            }
        }

        // ------------------------------------------------ preview (vertical, like the result)
        StudioPanel {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            visible: page.width > 1180
            title: qsTr("Preview")
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.largeSpacing
                spacing: LingmoUI.Units.smallSpacing
                Rectangle {
                    Layout.fillWidth: true
                    // the shape of the cut (the first platform's)
                    Layout.preferredHeight: width * page.shorts.previewSize.height / Math.max(1, page.shorts.previewSize.width)
                    radius: StudioTheme.smallRadius
                    color: StudioTheme.well
                    clip: true
                    // the source, centred and cropped like the cut, with its captions
                    // drawn by libass from the ASS the export burns in
                    VideoView {
                        anchors.fill: parent
                        editor: page.studioEditor
                        visible: page.playingId !== ""
                        fill: true
                        contentAspect: editor.sourceAspect // the media playing, inside the project's frame
                    }
                    CaptionOverlay {
                        objectName: "shortsCaptionOverlay"
                        anchors.fill: parent
                        visible: page.playingId !== "" && page.shorts.previewCaptions !== ""
                        source: page.shorts.previewCaptions
                        time: editor.player.mode === "source" ? editor.player.position * 1000 / editor.fps : 0
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - LingmoUI.Units.largeSpacing * 2
                        visible: page.playingId === ""
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Press play on a cut to watch it here")
                        font.pointSize: LingmoUI.Units.fontCaption
                        color: LingmoUI.Theme.secondaryTextColor
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
