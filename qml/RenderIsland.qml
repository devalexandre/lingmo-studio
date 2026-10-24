import QtQuick
import QtQml.Models
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// The render queue in the status bar's Island (com.lingmo.Island, see
// docs/island-api.md): the job being rendered, its progress and ETA, how many
// are waiting, and Cancel (cancels the job being rendered). The activity lasts
// while anything is queued or rendering, so it does not blink between jobs.
LingmoUI.IslandActivity {
    id: island

    property var queue: editor.renderQueue

    // One small object per queue row (name, state, progress, ETA)
    readonly property Instantiator _rows: Instantiator {
        id: rows
        model: island.queue
        delegate: QtObject {
            required property int index
            required property string name
            required property string state
            required property int progress
            required property var eta
        }
    }

    property int _finished: 0
    property int _failed: 0

    readonly property var current: {
        for (let i = 0; i < rows.count; ++i) {
            const row = rows.objectAt(i)
            if (row && row.state === "running")
                return row
        }
        return null
    }
    readonly property int waiting: {
        let n = 0
        for (let i = 0; i < rows.count; ++i) {
            const row = rows.objectAt(i)
            if (row && row.state === "queued")
                ++n
        }
        return n
    }

    function etaText(seconds) {
        if (seconds === undefined || seconds < 0)
            return ""
        if (seconds < 60)
            return qsTr("%1 s left").arg(Math.round(seconds))
        return qsTr("%1 min left").arg(Math.ceil(seconds / 60))
    }

    appId: "lingmo-studio"
    active: current !== null || waiting > 0
    kind: "render"
    title: qsTr("Exporting")
    subtitle: current ? current.name : qsTr("Waiting to render")
    iconName: "lingmo-studio"
    progress: current ? current.progress / 100 : 0
    hints: ({
        detail: [island.waiting > 0 ? qsTr("%n more in the queue", "", island.waiting) : "",
                 island.current ? island.etaText(island.current.eta) : ""].filter(s => s !== "").join(" · ")
    })
    actions: [ { id: "cancel", label: qsTr("Cancel") } ]

    onActionInvoked: (actionId) => {
        if (actionId === "cancel" && island.current)
            island.queue.cancel(island.current.index)
    }

    onActiveChanged: {
        if (active) {
            _finished = 0
            _failed = 0
        }
    }

    readonly property Connections _jobs: Connections {
        target: island.queue
        function onJobFinished(output, ok) {
            if (ok)
                ++island._finished
            else
                ++island._failed
            island.finishMessage = island._failed > 0 ? qsTr("Export failed") : qsTr("Export complete")
        }
    }
}
