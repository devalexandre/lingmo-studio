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
    property var types: []          // only these effect types (Color page tabs)
    property bool showClipSection: true
    property bool showEffectsHeader: true
    property int focusEffect: -1    // Effects page: the node picked in the graph
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

    // nothing selected
    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - 32
        visible: !inspector.hasClip
        spacing: 8
        StudioIcon { Layout.alignment: Qt.AlignHCenter; name: "inspector"; size: 30; color: StudioTheme.textDisabled }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: editor.selection.length > 1 ? qsTr("%n clips selected", "", editor.selection.length) : qsTr("Nothing selected")
            color: StudioTheme.text
            font.family: StudioTheme.font
            font.pixelSize: StudioTheme.fontLarge
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Select a clip on the timeline to see its settings and effects.")
            color: StudioTheme.textSecondary
            font.family: StudioTheme.font
            font.pixelSize: StudioTheme.fontBody
        }
    }

    component FieldLabel: Text {
        color: StudioTheme.textSecondary
        font.family: StudioTheme.font
        font.pixelSize: StudioTheme.fontSmall
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: 6
        visible: inspector.hasClip
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            // fixed reserve for the scroll bar: wrapped text must not make
            // the bar appear and disappear
            width: scroll.width - 10
            spacing: 6

            // ---------------- caption (word-timed)
            InspectorSection {
                visible: inspector.showClipSection && inspector.hasClip && inspector.sel.kind === "caption"
                Layout.fillWidth: true
                title: qsTr("Caption")
                accent: StudioTheme.trackCaption
                CaptionInspector {
                    Layout.fillWidth: true
                    info: inspector.hasClip && inspector.sel.kind === "caption" ? inspector.sel : ({})
                    onStyleRequested: root.openCaptions("")
                }
            }

            // ---------------- clip
            InspectorSection {
                visible: inspector.showClipSection && inspector.sel.kind !== "caption"
                Layout.fillWidth: true
                title: inspector.sel.kind === "title" ? qsTr("Title") : inspector.sel.kind === "color" ? qsTr("Color") : qsTr("Clip")
                accent: inspector.sel.trackKind === "audio" ? StudioTheme.trackAudio : StudioTheme.trackVideo

                TextField {
                    Layout.fillWidth: true
                    text: inspector.hasClip ? (inspector.sel.name || "") : ""
                    placeholderText: inspector.sel.kind === "title" ? qsTr("Title") : inspector.sel.kind === "color" ? qsTr("Color") : qsTr("Clip")
                    font.pixelSize: StudioTheme.fontLarge
                    font.bold: true
                    onEditingFinished: editor.setClipProperty(inspector.sel.id, "name", text)
                }
                Text {
                    Layout.fillWidth: true
                    text: inspector.hasClip ? qsTr("Starts %1 · lasts %2 · source %3–%4")
                                              .arg(editor.timecode(inspector.sel.position)).arg(editor.timecode(inspector.sel.duration))
                                              .arg(editor.timecode(inspector.sel.in)).arg(editor.timecode(inspector.sel.out)) : ""
                    color: StudioTheme.textSecondary
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontSmall
                    font.features: { "tnum": 1 }
                    wrapMode: Text.WordWrap
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 2

                    FieldLabel { text: qsTr("Speed"); visible: inspector.sel.kind === "media" }
                    ComboBox {
                        Layout.fillWidth: true
                        visible: inspector.sel.kind === "media"
                        readonly property var speeds: [0.25, 0.5, 0.75, 1, 1.25, 1.5, 2, 4, 8, -1]
                        model: speeds.map(function(v) { return v < 0 ? qsTr("Reverse") : v + "×" })
                        currentIndex: speeds.indexOf(inspector.sel.speed)
                        displayText: inspector.sel.speedRamp && inspector.sel.speedRamp.length ? qsTr("Ramp") : (inspector.sel.speed < 0 ? qsTr("Reverse") : inspector.sel.speed + "×")
                        onActivated: function(i) { editor.setClipProperty(inspector.sel.id, "speed", speeds[i]) }
                    }
                    FieldLabel { text: qsTr("Fade in") }
                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, Math.floor((inspector.sel.duration || 2) / 2))
                        stepSize: 1
                        value: inspector.sel.fadeIn || 0
                        onMoved: editor.setClipProperty(inspector.sel.id, "fadeIn", value)
                    }
                    FieldLabel { text: qsTr("Fade out") }
                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, Math.floor((inspector.sel.duration || 2) / 2))
                        stepSize: 1
                        value: inspector.sel.fadeOut || 0
                        onMoved: editor.setClipProperty(inspector.sel.id, "fadeOut", value)
                    }
                    FieldLabel { text: qsTr("Transition"); visible: inspector.sel.transitionIn > 0 }
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
                    FieldLabel { text: qsTr("Color"); visible: inspector.sel.kind === "color" }
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

            // ---------------- effects
            RowLayout {
                visible: inspector.showEffectsHeader
                Layout.fillWidth: true
                Layout.topMargin: 4
                Layout.leftMargin: 4
                Text {
                    text: qsTr("Effects")
                    color: StudioTheme.textSecondary
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontSmall
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.6
                    Layout.fillWidth: true
                }
                StudioButton {
                    text: qsTr("Add")
                    iconName: "plus"
                    iconSize: 13
                    size: 24
                    fontSize: StudioTheme.fontSmall
                    flat: false
                    onClicked: addMenu.popup()
                }
            }

            Repeater {
                model: inspector.hasClip ? inspector.sel.effects : []
                delegate: InspectorSection {
                    id: fxCard
                    required property var modelData
                    readonly property var fx: modelData
                    readonly property var def: inspector.effectDef(fx.type)
                    readonly property var values: fx.animated ? editor.effectValuesAt(inspector.sel.id, fx.index, inspector.playhead) : fx.params
                    readonly property bool keyHere: fx.animated && editor.hasKeyframeAt(inspector.sel.id, fx.index, inspector.playhead)
                    readonly property bool canAnimate: def ? def.params.some(function(p) { return p.animatable }) : false
                    visible: inspector.shown(fx.category) && (inspector.types.length === 0 || inspector.types.indexOf(fx.type) >= 0)
                    Layout.fillWidth: true
                    title: fx.name
                    dim: !fx.enabled
                    expanded: inspector.focusEffect < 0 || inspector.focusEffect === fx.index
                    accent: ({ transform: StudioTheme.trackVideo, color: "#D8606A", stylize: StudioTheme.titleClipEdge, key: StudioTheme.trackAudio,
                               text: StudioTheme.trackCaption, audio: StudioTheme.trackAudio })[fx.category] || StudioTheme.textSecondary

                    tools: [
                        StudioButton {
                            visible: fxCard.canAnimate
                            iconName: fxCard.keyHere ? "keyframe-on" : "keyframe"
                            size: 22
                            iconSize: 12
                            tip: fxCard.keyHere ? qsTr("Remove keyframe at playhead") : qsTr("Add keyframe at playhead")
                            checkable: true
                            checked: fxCard.keyHere
                            onClicked: editor.toggleKeyframe(inspector.sel.id, fxCard.fx.index, inspector.playhead)
                        },
                        StudioButton { iconName: "chevron-left"; rotation: 90; size: 22; iconSize: 12; tip: qsTr("Move up"); enabled: fxCard.fx.index > 0; onClicked: editor.moveEffect(inspector.sel.id, fxCard.fx.index, fxCard.fx.index - 1) },
                        StudioButton { iconName: "chevron-right"; rotation: 90; size: 22; iconSize: 12; tip: qsTr("Move down"); enabled: fxCard.fx.index < inspector.sel.effects.length - 1; onClicked: editor.moveEffect(inspector.sel.id, fxCard.fx.index, fxCard.fx.index + 1) },
                        StudioButton {
                            iconName: "eye"
                            size: 22
                            iconSize: 13
                            tip: fxCard.fx.enabled ? qsTr("Bypass") : qsTr("Enable")
                            checkable: true
                            checked: fxCard.fx.enabled
                            onClicked: editor.setEffectEnabled(inspector.sel.id, fxCard.fx.index, !fxCard.fx.enabled)
                        },
                        StudioButton { iconName: "trash"; size: 22; iconSize: 13; tip: qsTr("Remove"); onClicked: editor.removeEffect(inspector.sel.id, fxCard.fx.index) }
                    ]

                    Repeater {
                        model: fxCard.def ? fxCard.def.params : []
                        ParamEditor {
                            Layout.fillWidth: true
                            def: modelData
                            value: fxCard.values[modelData.id]
                            animated: fxCard.fx.animated
                            onEdited: function(v) { editor.setEffectParam(inspector.sel.id, fxCard.fx.index, modelData.id, v) }
                        }
                    }
                    Text {
                        visible: fxCard.fx.type === "stabilize"
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("Stabilizing needs an analysis file (.trf) made with vid.stab; analysis inside Studio is not available yet.")
                        color: StudioTheme.textSecondary
                        font.family: StudioTheme.font
                        font.pixelSize: StudioTheme.fontSmall
                    }
                }
            }
            Item { Layout.preferredHeight: 10 }
        }
    }
}
