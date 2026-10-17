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

// Properties of the selected clip and its effect stack.
// `categories` limits the effects shown (Color page: color, Audio page: audio).
Item {
    id: inspector

    property var categories: []
    property bool showClipSection: true
    readonly property var sel: editor.selectedClip
    readonly property bool hasClip: sel && sel.id !== undefined
    readonly property int playhead: editor.player.timelinePosition

    function effectDef(type) {
        const defs = editor.effectDefs
        for (let i = 0; i < defs.length; ++i)
            if (defs[i].id === type)
                return defs[i]
        return null
    }
    function shown(category) {
        return categories.length === 0 || categories.indexOf(category) >= 0
    }

    Menu {
        id: addMenu
    }
    Instantiator {
        model: ["transform", "color", "stylize", "key", "text", "audio"]
        delegate: Menu {
            id: catMenu
            required property string modelData
            enabled: inspector.shown(modelData)
            title: ({ transform: qsTr("Transform"), color: qsTr("Color"), stylize: qsTr("Stylize"), key: qsTr("Keying"), text: qsTr("Text"), audio: qsTr("Audio") })[modelData]
            Repeater {
                model: editor.effectDefs.filter(function(d) { return d.category === catMenu.modelData && !d.hidden })
                MenuItem {
                    text: modelData.name + (modelData.available ? "" : " " + qsTr("(not installed)"))
                    enabled: modelData.available
                    onTriggered: editor.addEffect(inspector.sel.id, modelData.id)
                }
            }
        }
        onObjectAdded: function(index, object) { addMenu.insertMenu(index, object) }
        onObjectRemoved: function(index, object) { addMenu.removeMenu(object) }
    }

    LingmoUI.EmptyState {
        anchors.centerIn: parent
        width: parent.width - LingmoUI.Units.largeSpacing * 2
        visible: !inspector.hasClip
        iconName: "document-properties"
        iconSize: LingmoUI.Units.iconSizes.large
        title: editor.selection.length > 1 ? qsTr("%n clips selected", "", editor.selection.length) : qsTr("Nothing selected")
        text: qsTr("Select a clip on the timeline to see its settings and effects.")
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        visible: inspector.hasClip
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroll.availableWidth
            spacing: LingmoUI.Units.smallSpacing

            // ---------------- caption (word-timed)
            CaptionInspector {
                visible: inspector.showClipSection && inspector.hasClip && inspector.sel.kind === "caption"
                Layout.fillWidth: true
                Layout.leftMargin: LingmoUI.Units.smallSpacing
                Layout.rightMargin: LingmoUI.Units.smallSpacing
                info: visible ? inspector.sel : ({})
                onStyleRequested: root.openCaptions("")
            }

            // ---------------- clip
            LingmoUI.Card {
                visible: inspector.showClipSection && inspector.sel.kind !== "caption"
                Layout.fillWidth: true
                Layout.leftMargin: LingmoUI.Units.smallSpacing
                Layout.rightMargin: LingmoUI.Units.smallSpacing
                implicitHeight: clipCol.implicitHeight + LingmoUI.Units.largeSpacing * 2
                ColumnLayout {
                    id: clipCol
                    anchors.fill: parent
                    anchors.margins: LingmoUI.Units.largeSpacing
                    spacing: LingmoUI.Units.smallSpacing

                    TextField {
                        Layout.fillWidth: true
                        text: inspector.hasClip ? (inspector.sel.name || "") : ""
                        placeholderText: inspector.sel.kind === "title" ? qsTr("Title") : inspector.sel.kind === "color" ? qsTr("Color") : qsTr("Clip")
                        font.pointSize: LingmoUI.Units.fontSubheading
                        font.bold: true
                        onEditingFinished: editor.setClipProperty(inspector.sel.id, "name", text)
                    }
                    Label {
                        Layout.fillWidth: true
                        text: inspector.hasClip ? qsTr("Starts %1 · lasts %2 · source %3–%4")
                                                  .arg(editor.timecode(inspector.sel.position)).arg(editor.timecode(inspector.sel.duration))
                                                  .arg(editor.timecode(inspector.sel.in)).arg(editor.timecode(inspector.sel.out)) : ""
                        color: LingmoUI.Theme.secondaryTextColor
                        font.pointSize: LingmoUI.Units.fontCaption
                        wrapMode: Text.WordWrap
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: LingmoUI.Units.smallSpacing
                        rowSpacing: 2

                        Label { text: qsTr("Speed"); color: LingmoUI.Theme.secondaryTextColor; font.pointSize: LingmoUI.Units.fontCaption; visible: inspector.sel.kind === "media" }
                        ComboBox {
                            Layout.fillWidth: true
                            visible: inspector.sel.kind === "media"
                            readonly property var speeds: [0.25, 0.5, 0.75, 1, 1.25, 1.5, 2, 4, 8, -1]
                            model: speeds.map(function(v) { return v < 0 ? qsTr("Reverse") : v + "×" })
                            currentIndex: speeds.indexOf(inspector.sel.speed)
                            displayText: inspector.sel.speedRamp && inspector.sel.speedRamp.length ? qsTr("Ramp") : (inspector.sel.speed < 0 ? qsTr("Reverse") : inspector.sel.speed + "×")
                            onActivated: function(i) { editor.setClipProperty(inspector.sel.id, "speed", speeds[i]) }
                        }
                        Label { text: qsTr("Fade in"); color: LingmoUI.Theme.secondaryTextColor; font.pointSize: LingmoUI.Units.fontCaption }
                        Slider {
                            Layout.fillWidth: true
                            from: 0
                            to: Math.max(1, Math.floor((inspector.sel.duration || 2) / 2))
                            stepSize: 1
                            value: inspector.sel.fadeIn || 0
                            onMoved: editor.setClipProperty(inspector.sel.id, "fadeIn", value)
                        }
                        Label { text: qsTr("Fade out"); color: LingmoUI.Theme.secondaryTextColor; font.pointSize: LingmoUI.Units.fontCaption }
                        Slider {
                            Layout.fillWidth: true
                            from: 0
                            to: Math.max(1, Math.floor((inspector.sel.duration || 2) / 2))
                            stepSize: 1
                            value: inspector.sel.fadeOut || 0
                            onMoved: editor.setClipProperty(inspector.sel.id, "fadeOut", value)
                        }
                        Label { text: qsTr("Transition"); color: LingmoUI.Theme.secondaryTextColor; font.pointSize: LingmoUI.Units.fontCaption; visible: inspector.sel.transitionIn > 0 }
                        RowLayout {
                            visible: inspector.sel.transitionIn > 0
                            Layout.fillWidth: true
                            ComboBox {
                                Layout.fillWidth: true
                                model: editor.transitionTypes
                                currentIndex: Math.max(0, editor.transitionTypes.indexOf(inspector.sel.transition))
                                onActivated: editor.setTransition(inspector.sel.id, inspector.sel.transitionIn, currentText)
                            }
                            SpinBox {
                                from: 1
                                to: 600
                                value: inspector.sel.transitionIn || 1
                                editable: true
                                onValueModified: editor.setTransition(inspector.sel.id, value, "")
                            }
                        }
                        Label { text: qsTr("Color"); color: LingmoUI.Theme.secondaryTextColor; font.pointSize: LingmoUI.Units.fontCaption; visible: inspector.sel.kind === "color" }
                        ParamEditor {
                            visible: inspector.sel.kind === "color"
                            Layout.fillWidth: true
                            def: ({ type: "color", name: "" })
                            value: inspector.sel.color || "#000000"
                            onEdited: function(v) { editor.setClipProperty(inspector.sel.id, "color", v) }
                        }
                    }
                    Switch {
                        visible: inspector.sel.kind === "media" && inspector.sel.trackKind === "video" && inspector.sel.mediaKind === "video"
                        text: qsTr("Play this clip's sound")
                        checked: !inspector.sel.audioMuted
                        onToggled: editor.setClipProperty(inspector.sel.id, "audioMuted", !checked)
                    }
                }
            }

            // ---------------- effects
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: LingmoUI.Units.largeSpacing
                Layout.rightMargin: LingmoUI.Units.smallSpacing
                Label {
                    text: qsTr("Effects")
                    font.pointSize: LingmoUI.Units.fontBody
                    font.bold: true
                    color: LingmoUI.Theme.textColor
                    Layout.fillWidth: true
                }
                LingmoUI.ActionButton {
                    text: qsTr("Add")
                    iconName: "list-add"
                    onClicked: addMenu.popup()
                }
            }

            Repeater {
                model: inspector.hasClip ? inspector.sel.effects : []
                delegate: LingmoUI.Card {
                    id: fxCard
                    required property var modelData
                    readonly property var fx: modelData
                    readonly property var def: inspector.effectDef(fx.type)
                    readonly property var values: fx.animated ? editor.effectValuesAt(inspector.sel.id, fx.index, inspector.playhead) : fx.params
                    readonly property bool keyHere: fx.animated && editor.hasKeyframeAt(inspector.sel.id, fx.index, inspector.playhead)
                    readonly property bool canAnimate: def ? def.params.some(function(p) { return p.animatable }) : false
                    property bool expanded: true
                    visible: inspector.shown(fx.category)
                    Layout.fillWidth: true
                    Layout.leftMargin: LingmoUI.Units.smallSpacing
                    Layout.rightMargin: LingmoUI.Units.smallSpacing
                    implicitHeight: visible ? fxCol.implicitHeight + LingmoUI.Units.largeSpacing * 1.5 : 0
                    opacity: fx.enabled ? 1 : 0.6

                    ColumnLayout {
                        id: fxCol
                        anchors.fill: parent
                        anchors.margins: LingmoUI.Units.largeSpacing * 0.75
                        spacing: LingmoUI.Units.smallSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            LingmoUI.IconButton {
                                iconName: fxCard.expanded ? "go-down" : "go-next"
                                size: 22
                                iconSize: 12
                                onClicked: fxCard.expanded = !fxCard.expanded
                            }
                            Label {
                                text: fxCard.fx.name
                                font.pointSize: LingmoUI.Units.fontBody
                                font.bold: true
                                color: LingmoUI.Theme.textColor
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            LingmoUI.IconButton {
                                visible: fxCard.canAnimate
                                iconName: "keyframe-add"
                                tipText: fxCard.keyHere ? qsTr("Remove keyframe at playhead") : qsTr("Add keyframe at playhead")
                                checkable: true
                                checked: fxCard.keyHere
                                size: 24
                                onClicked: editor.toggleKeyframe(inspector.sel.id, fxCard.fx.index, inspector.playhead)
                            }
                            LingmoUI.IconButton { iconName: "go-up"; tipText: qsTr("Move up"); size: 24; enabled: fxCard.fx.index > 0; onClicked: editor.moveEffect(inspector.sel.id, fxCard.fx.index, fxCard.fx.index - 1) }
                            LingmoUI.IconButton { iconName: "go-down"; tipText: qsTr("Move down"); size: 24; enabled: fxCard.fx.index < inspector.sel.effects.length - 1; onClicked: editor.moveEffect(inspector.sel.id, fxCard.fx.index, fxCard.fx.index + 1) }
                            Switch {
                                checked: fxCard.fx.enabled
                                onToggled: editor.setEffectEnabled(inspector.sel.id, fxCard.fx.index, checked)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Bypass")
                            }
                            LingmoUI.IconButton { iconName: "edit-delete"; tipText: qsTr("Remove"); size: 24; onClicked: editor.removeEffect(inspector.sel.id, fxCard.fx.index) }
                        }
                        Repeater {
                            model: fxCard.expanded && fxCard.def ? fxCard.def.params : []
                            ParamEditor {
                                Layout.fillWidth: true
                                def: modelData
                                value: fxCard.values[modelData.id]
                                animated: fxCard.fx.animated
                                onEdited: function(v) { editor.setEffectParam(inspector.sel.id, fxCard.fx.index, modelData.id, v) }
                            }
                        }
                        Label {
                            visible: fxCard.fx.type === "stabilize"
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Stabilizing needs an analysis file (.trf) made with vid.stab; analysis inside Studio is not available yet.")
                            color: LingmoUI.Theme.secondaryTextColor
                            font.pointSize: LingmoUI.Units.fontCaption
                        }
                    }
                }
            }
            Item { Layout.preferredHeight: LingmoUI.Units.largeSpacing }
        }
    }
}
