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

// Deliver page: render settings on the left (preset cards, file, encoder,
// quality, range), the viewer in the centre, the render queue on the right.
SplitView {
    id: page
    orientation: Qt.Horizontal
    handle: PanelHandle { }

    // The generated cuts export from Gerar cortes (one file per cut and platform)
    signal exportCutsRequested()

    property string presetId: "mp4-h264"
    // the usual destinations first
    readonly property var order: ["youtube-1080p", "shorts-9x16", "mp4-h264", "prores-hq", "youtube-4k-hevc", "webm-vp9", "av1", "dnxhr-hq", "gif", "audio-mp3", "audio-wav"]
    readonly property var presets: editor.exportPresets.slice().sort(function(a, b) {
        const ia = order.indexOf(a.id), ib = order.indexOf(b.id)
        return (ia < 0 ? 99 : ia) - (ib < 0 ? 99 : ib)
    })
    readonly property var preset: {
        const list = editor.exportPresets
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === presetId)
                return list[i]
        return list[0]
    }
    function presetIcon(p) {
        if (p.audioOnly)
            return "music"
        if (p.vertical)
            return "phone"
        return ({ "youtube-1080p": "widescreen", "youtube-4k-hevc": "widescreen", "gif": "image", "prores-hq": "film", "dnxhr-hq": "film" })[p.id] || "export"
    }
    property string output: ""
    onPresetChanged: output = editor.suggestedExportPath(presetId)
    Component.onCompleted: output = editor.suggestedExportPath(presetId)
    Connections {
        target: editor
        function onProjectChanged() { page.output = editor.suggestedExportPath(page.presetId) }
    }
    readonly property string hwFor: {
        if (!preset || ["h264", "hevc", "av1"].indexOf(preset.codec) < 0)
            return ""
        const nv = preset.codec + "_nvenc", va = preset.codec + "_vaapi"
        return editor.hwEncoders.indexOf(nv) >= 0 ? nv : (editor.hwEncoders.indexOf(va) >= 0 ? va : "")
    }
    readonly property bool projectVertical: editor.projectHeight > editor.projectWidth
    readonly property bool hasQuality: preset && !preset.audioOnly && preset.codec !== "gif" && preset.codec !== "prores" && preset.codec !== "dnxhr"

    FileDialog {
        id: outputDialog
        title: qsTr("Export to")
        fileMode: FileDialog.SaveFile
        currentFile: "file://" + page.output
        defaultSuffix: page.preset ? page.preset.extension : "mp4"
        onAccepted: page.output = editor.localPath(selectedFile)
    }
    FileDialog {
        id: srtSave
        fileMode: FileDialog.SaveFile
        defaultSuffix: "srt"
        nameFilters: [qsTr("Subtitles (*.srt)")]
        onAccepted: editor.exportSubtitles(selectedFile)
    }

    component SettingLabel: Text {
        color: StudioTheme.textSecondary
        font.family: StudioTheme.font
        font.pixelSize: StudioTheme.fontSmall
        font.weight: Font.DemiBold
    }

    // ---------------------------------------------------- render settings
    StudioPanel {
        title: qsTr("Render Settings")
        SplitView.preferredWidth: 400
        SplitView.minimumWidth: 330

        ScrollView {
            id: settingsScroll
            anchors.fill: parent
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                // fixed reserve for the scroll bar (see StartPage)
                width: settingsScroll.width - 12
                spacing: 10

                // preset cards
                GridLayout {
                    Layout.fillWidth: true
                    Layout.margins: StudioTheme.pad
                    Layout.bottomMargin: 0
                    columns: 2
                    columnSpacing: 6
                    rowSpacing: 6
                    Repeater {
                        model: page.presets
                        AbstractButton {
                            id: card
                            readonly property bool current: modelData.id === page.presetId
                            Layout.fillWidth: true
                            Layout.preferredHeight: 62
                            hoverEnabled: true
                            onClicked: page.presetId = modelData.id
                            background: Rectangle {
                                radius: StudioTheme.radius
                                color: card.current ? StudioTheme.accentSoft : card.hovered ? StudioTheme.raisedHover : StudioTheme.raised
                                border.width: 1
                                border.color: card.current ? StudioTheme.accent : StudioTheme.border
                                Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
                                Behavior on border.color { ColorAnimation { duration: StudioTheme.fast } }
                            }
                            contentItem: RowLayout {
                                spacing: 8
                                StudioIcon {
                                    Layout.leftMargin: 10
                                    name: page.presetIcon(modelData)
                                    size: 20
                                    color: card.current ? StudioTheme.accent : StudioTheme.textSecondary
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.rightMargin: 8
                                    spacing: 1
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        color: StudioTheme.text
                                        font.family: StudioTheme.font
                                        font.pixelSize: StudioTheme.fontSmall
                                        font.weight: Font.DemiBold
                                        wrapMode: Text.WordWrap
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: "." + modelData.extension + " · " + (modelData.audioOnly ? qsTr("audio") : (modelData.width > 0 ? (modelData.width + "×" + (modelData.height > 0 ? modelData.height : "…")) : qsTr("project size")))
                                        color: StudioTheme.textSecondary
                                        font.family: StudioTheme.font
                                        font.pixelSize: StudioTheme.fontTiny
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: StudioTheme.divider; Layout.topMargin: 4 }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: StudioTheme.pad
                    Layout.rightMargin: StudioTheme.pad
                    spacing: 8

                    Rectangle {
                        Layout.fillWidth: true
                        visible: page.preset && page.preset.vertical && !page.projectVertical
                        Layout.preferredHeight: warnCol.implicitHeight + 16
                        radius: StudioTheme.radius
                        color: StudioTheme.alpha(StudioTheme.warning, 0.12)
                        border.width: 1
                        border.color: StudioTheme.alpha(StudioTheme.warning, 0.5)
                        ColumnLayout {
                            id: warnCol
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 6
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: qsTr("This is a horizontal project") + ". " + qsTr("Reframe it for 9:16 first, or the video gets black bars.")
                                color: StudioTheme.text
                                font.family: StudioTheme.font
                                font.pixelSize: StudioTheme.fontSmall
                            }
                            StudioButton { text: qsTr("Reframe to 9:16"); flat: false; size: 24; fontSize: StudioTheme.fontSmall; onClicked: editor.smartReframe("9:16") }
                        }
                    }

                    SettingLabel { text: qsTr("File") }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            Layout.fillWidth: true
                            text: page.output
                            onEditingFinished: page.output = text
                        }
                        StudioButton { iconName: "folder"; tip: qsTr("Choose…"); flat: false; onClicked: outputDialog.open() }
                    }

                    SettingLabel { text: qsTr("Encoder"); Layout.topMargin: 4 }
                    Switch {
                        id: hw
                        Layout.fillWidth: true
                        text: page.hwFor.length > 0 ? qsTr("Use the graphics card (%1)").arg(page.hwFor) : qsTr("Use the graphics card")
                        checked: true
                        enabled: page.hwFor.length > 0
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: !editor.hwDetected ? qsTr("Checking which encoders the graphics card offers…")
                              : page.hwFor.length > 0 ? qsTr("Faster export; software encoding is used if it fails to start.")
                              : qsTr("No working hardware encoder for this format; the CPU encodes it.")
                        color: StudioTheme.textSecondary
                        font.family: StudioTheme.font
                        font.pixelSize: StudioTheme.fontSmall
                    }

                    SettingLabel { text: qsTr("Quality"); visible: page.hasQuality; Layout.topMargin: 4 }
                    RowLayout {
                        visible: page.hasQuality
                        Layout.fillWidth: true
                        Text { text: qsTr("Smaller"); color: StudioTheme.textSecondary; font.family: StudioTheme.font; font.pixelSize: StudioTheme.fontSmall }
                        Slider {
                            id: quality
                            Layout.fillWidth: true
                            from: 34; to: 14
                            stepSize: 1
                            value: 21
                        }
                        Text { text: qsTr("Better"); color: StudioTheme.textSecondary; font.family: StudioTheme.font; font.pixelSize: StudioTheme.fontSmall }
                    }

                    SettingLabel { text: qsTr("Range"); Layout.topMargin: 4 }
                    Row {
                        spacing: 2
                        StudioButton { text: qsTr("Whole timeline"); flat: false; checkable: true; checked: !range.inOut; onClicked: range.inOut = false }
                        StudioButton {
                            id: range
                            property bool inOut: false
                            text: qsTr("In to out")
                            flat: false
                            checkable: true
                            checked: inOut
                            enabled: editor.player.markIn >= 0
                            onClicked: inOut = true
                        }
                    }

                    StudioButton {
                        Layout.fillWidth: true
                        Layout.topMargin: 10
                        size: 36
                        text: qsTr("Add to Render Queue")
                        iconName: "export"
                        primary: true
                        enabled: editor.timeline.duration > 1 && page.output.length > 0
                        onClicked: editor.queueExport(page.presetId, "file://" + page.output, hw.checked && page.hwFor.length > 0,
                                                      (page.preset.codec === "h264" || page.preset.codec === "hevc" || page.preset.codec === "av1" || page.preset.codec === "vp9") ? Math.round(quality.value) : -1,
                                                      range.inOut && editor.player.markIn >= 0)
                    }
                    StudioButton {
                        Layout.fillWidth: true
                        text: qsTr("Export subtitles (SRT)…")
                        iconName: "captions"
                        flat: false
                        onClicked: srtSave.open()
                    }
                    Item { Layout.preferredHeight: 10 }
                }
            }
        }
    }

    // ---------------------------------------------------- viewer
    ColumnLayout {
        SplitView.fillWidth: true
        SplitView.minimumWidth: 400
        spacing: StudioTheme.gap
        MonitorPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            showSourceControls: false
        }
        StudioPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 84
            title: qsTr("Timeline")
            subtitle: editor.formatDuration(editor.timeline.duration)
            TimelineOverview {
                anchors.fill: parent
                anchors.margins: 6
                showView: false
                onScrubbed: function(frame) {
                    if (editor.player.mode !== "timeline")
                        editor.player.setMode("timeline")
                    editor.player.seek(frame)
                }
            }
        }
    }

    // ---------------------------------------------------- render queue
    StudioPanel {
        title: qsTr("Render Queue")
        subtitle: queue.count > 0 ? qsTr("%n job(s)", "", queue.count) : ""
        SplitView.preferredWidth: 360
        SplitView.minimumWidth: 280
        actions: [
            StudioButton { text: qsTr("Clear finished"); size: 24; fontSize: StudioTheme.fontSmall; enabled: editor.renderQueue.count > 0; onClicked: editor.renderQueue.clearFinished() }
        ]

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 6

        // ---- the cuts made in Gerar cortes: they export from there
        Rectangle {
            id: cutsBox
            Layout.fillWidth: true
            visible: editor.shortSequences.length > 0
            implicitHeight: cutsCol.implicitHeight + 20
            radius: StudioTheme.radius
            color: StudioTheme.raised
            border.width: 1
            border.color: StudioTheme.accent
            ColumnLayout {
                id: cutsCol
                anchors.fill: parent
                anchors.margins: 10
                spacing: 4
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Generated cuts (%1)").arg(editor.shortSequences.length)
                    color: StudioTheme.text
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontBody
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Each cut becomes one MP4 per platform, captions in the video. The settings on the left export the whole timeline instead.")
                    color: StudioTheme.textSecondary
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontSmall
                }
                Repeater {
                    model: editor.shorts.cuts
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: modelData.name
                            color: StudioTheme.text
                            font.family: StudioTheme.font
                            font.pixelSize: StudioTheme.fontSmall
                            elide: Text.ElideRight
                        }
                        Text {
                            text: modelData.state === "running" ? Math.round(modelData.progress * 100) + "%"
                                : modelData.state === "queued" ? qsTr("queued")
                                : modelData.state === "done" ? qsTr("done")
                                : modelData.state === "failed" ? qsTr("failed")
                                : modelData.length
                            color: modelData.state === "done" ? StudioTheme.success : modelData.state === "failed" ? StudioTheme.danger : StudioTheme.textSecondary
                            font.family: StudioTheme.font
                            font.pixelSize: StudioTheme.fontSmall
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    StudioButton {
                        visible: editor.shorts.exportDone
                        text: qsTr("Open folder")
                        iconName: "folder"
                        size: 24
                        fontSize: StudioTheme.fontSmall
                        onClicked: editor.shorts.openExportFolder()
                    }
                    StudioButton {
                        text: qsTr("Export cuts…")
                        iconName: "export"
                        primary: true
                        size: 24
                        fontSize: StudioTheme.fontSmall
                        enabled: !editor.shorts.exporting
                        onClicked: page.exportCutsRequested()
                    }
                }
            }
        }

        ListView {
            id: queue
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            clip: true
            model: editor.renderQueue
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: job
                required property int index
                required property string name
                required property string output
                required property string preset
                required property string state
                required property int progress
                required property string encoder
                required property string message
                required property int eta
                readonly property color tone: state === "done" ? StudioTheme.success : state === "failed" ? StudioTheme.danger : state === "cancelled" ? StudioTheme.textDisabled : StudioTheme.accent
                width: queue.width
                implicitHeight: jobCol.implicitHeight + 20
                radius: StudioTheme.radius
                color: StudioTheme.raised
                border.width: 1
                border.color: StudioTheme.border
                Rectangle { x: 0; y: 8; width: 3; height: parent.height - 16; radius: 1.5; color: job.tone }
                ColumnLayout {
                    id: jobCol
                    anchors.fill: parent
                    anchors.margins: 10
                    anchors.leftMargin: 14
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: job.name
                            color: StudioTheme.text
                            font.family: StudioTheme.font
                            font.pixelSize: StudioTheme.fontBody
                            font.weight: Font.DemiBold
                            elide: Text.ElideMiddle
                        }
                        Text {
                            text: ({ queued: qsTr("queued"), running: job.progress + "%", done: qsTr("done"), failed: qsTr("failed"), cancelled: qsTr("cancelled") })[job.state]
                            color: job.tone
                            font.family: StudioTheme.font
                            font.pixelSize: StudioTheme.fontSmall
                            font.weight: Font.DemiBold
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: job.preset + (job.encoder ? " · " + job.encoder : "") + (job.state === "running" && job.eta >= 0 ? " · " + qsTr("%1 s left").arg(job.eta) : "")
                        color: StudioTheme.textSecondary
                        font.family: StudioTheme.font
                        font.pixelSize: StudioTheme.fontSmall
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 4
                        visible: job.state === "running" || job.state === "queued"
                        radius: 2
                        color: StudioTheme.well
                        Rectangle {
                            width: parent.width * job.progress / 100
                            height: parent.height
                            radius: 2
                            color: StudioTheme.accent
                            Behavior on width { NumberAnimation { duration: StudioTheme.normal } }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: job.message.length > 0
                        text: job.message
                        wrapMode: Text.WordWrap
                        color: StudioTheme.danger
                        font.family: StudioTheme.font
                        font.pixelSize: StudioTheme.fontSmall
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        StudioButton {
                            visible: job.state === "running" || job.state === "queued"
                            text: qsTr("Cancel")
                            size: 24
                            fontSize: StudioTheme.fontSmall
                            onClicked: editor.renderQueue.cancel(job.index)
                        }
                        StudioButton {
                            visible: job.state === "done"
                            text: qsTr("Show in folder")
                            iconName: "folder"
                            size: 24
                            fontSize: StudioTheme.fontSmall
                            onClicked: editor.renderQueue.openFolder(job.index)
                        }
                    }
                }
            }
        }
        }
        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width - 40
            visible: queue.count === 0 && editor.shortSequences.length === 0
            spacing: 8
            StudioIcon { Layout.alignment: Qt.AlignHCenter; name: "page-deliver"; size: 32; color: StudioTheme.textDisabled }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Nothing rendering")
                color: StudioTheme.text
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontLarge
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("Pick a format and add the project to the queue. You can keep editing while it renders.")
                color: StudioTheme.textSecondary
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontBody
            }
        }
    }
}
