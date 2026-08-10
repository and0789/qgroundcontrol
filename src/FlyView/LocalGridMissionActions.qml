import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// What can be done with the plan as a whole, from the grid it was built on.
///
/// Building a mission here and then having to leave for the Plan view to send it is the same break
/// this whole view exists to remove: the operator loses sight of the aircraft and the pattern at the
/// moment they are committing to it.
///
/// The destructive ones ask first. Clearing a plan and overwriting one with the vehicle's copy both
/// throw away work that took a flight line to build, and neither can be undone.
Rectangle {
    id: _root

    property var planMasterController: null

    /// The grid this belongs to, for the altitude ceiling it knows about
    property var gridView: null

    readonly property var  _itemsTooHigh:   gridView ? gridView.itemsAboveAltitudeLimit : []
    readonly property bool _anyItemTooHigh: _itemsTooHigh.length > 0

    readonly property var _missionController: planMasterController ? planMasterController.missionController : null
    readonly property bool _hasController:    planMasterController !== null
    readonly property bool _offline:          _hasController ? planMasterController.offline : true
    readonly property bool _dirtyForUpload:   _hasController ? planMasterController.dirtyForUpload : false
    readonly property bool _hasItems:         _missionController ? (_missionController.visualItems.count > 1) : false

    /// Clearing covers the trail as well as the plan, so it stays available while either is there.
    /// Gated on the plan alone it went dead the moment the plan emptied -- which is exactly when an
    /// operator who has just flown a pattern reaches for it to wipe the line they flew.
    readonly property bool _hasTrail: gridView ? (gridView.trailPointCount > 0) : false
    readonly property bool _canClear: _hasItems || _hasTrail

    /// True while the plan is being sent to or fetched from the vehicle. A mission upload is a
    /// request-and-acknowledge exchange per item over a link that drops them, so it takes long
    /// enough that an operator with no indication of it cannot tell a transfer in progress from one
    /// that never started -- and pressing Upload again mid-transfer restarts it.
    readonly property bool _syncing:    _hasController ? planMasterController.syncInProgress : false
    readonly property real _progress:   _missionController ? _missionController.progressPct : 0

    /// Shown for a moment once a transfer lands, so a fast upload is not just a bar that flickers
    /// and leaves the operator no wiser about whether it went
    property bool _showSyncComplete: false

    on_SyncingChanged: {
        if (_syncing) {
            _showSyncComplete = false
        } else if (_hasController) {
            _showSyncComplete = true
            syncCompleteTimer.restart()
        }
    }

    implicitWidth:  layout.implicitWidth + (_margins * 2)
    implicitHeight: layout.implicitHeight + (_margins * 2)
    color:          qgcPal.window
    opacity:        0.9
    radius:         ScreenTools.defaultFontPixelHeight / 4
    visible:        _hasController

    property real _margins: ScreenTools.defaultFontPixelHeight / 3

    readonly property string _limitText: (gridView && gridView.altitudeLimitKnown)
                                            ? (gridView.gridTransform.toDisplay(gridView.altitudeLimitMetres).toFixed(1)
                                               + " " + gridView.gridTransform.displayUnits)
                                            : qsTr("--")

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    function _confirm(title, message, action) {
        QGroundControl.showMessageDialog(_root, title, message, Dialog.Yes | Dialog.Cancel, action)
    }

    /// Wipes the plan and the trail together. Clearing is how a new pattern is started, and a grid
    /// still carrying the line the last one was flown along shows two flights at once -- with no way
    /// to tell which of them the vehicle is about to repeat.
    function _clearPlanAndTrail() {
        if (_hasItems) {
            // Removing from the vehicle as well when one is connected, matching what the Plan view
            // does: clearing only the editor would leave the aircraft holding the plan that was
            // just discarded
            if (_offline) {
                planMasterController.removeAll()
            } else {
                planMasterController.removeAllFromVehicle()
            }
        }
        if (gridView) {
            gridView.clearTrail()
        }
    }

    /// Says which of the two is about to go, since the button covers both and the operator should
    /// not have to guess which one they are about to lose
    function _clearMessage() {
        if (!_hasItems) {
            return qsTr("Remove the trail flown so far?")
        }
        if (_offline) {
            return qsTr("Remove every item from the plan, and the trail flown so far?")
        }
        return qsTr("Remove the plan from the vehicle and from here, along with the trail flown so far?")
    }

    function _clear() {
        _confirm(qsTr("Clear"), _clearMessage(), _clearPlanAndTrail)
    }

    function _download() {
        _confirm(qsTr("Download"),
                 qsTr("Replace the plan here with the one on the vehicle? Anything not sent is lost."),
                 function() { planMasterController.loadFromVehicle() })
    }

    function _load() {
        fileDialog.title =       qsTr("Select Plan File")
        fileDialog.nameFilters = planMasterController.loadNameFilters
        fileDialog.openForLoad()
    }

    function _save() {
        fileDialog.title =       qsTr("Save Plan")
        fileDialog.nameFilters = planMasterController.saveNameFilters
        fileDialog.openForSave()
    }

    Timer {
        id:             syncCompleteTimer
        interval:       4000
        onTriggered:    _root._showSyncComplete = false
    }

    QGCFileDialog {
        id:     fileDialog
        folder: QGroundControl.settingsManager.appSettings.missionSavePath

        onAcceptedForSave: (file) => {
            if (_root.planMasterController.saveToFile(file)) {
                close()
            }
        }

        onAcceptedForLoad: (file) => {
            _root.planMasterController.loadFromFile(file)
            close()
        }
    }

    ColumnLayout {
        id:                 layout
        anchors.margins:    _root._margins
        anchors.left:       parent.left
        anchors.top:        parent.top
        spacing:            ScreenTools.defaultFontPixelHeight / 6

        QGCLabel {
            font.pointSize: ScreenTools.smallFontPointSize
            font.bold:      true
            text:           qsTr("Mission")
        }

        // Placed where the plan is committed, and holding Upload shut while it stands. A warning
        // beside the button that sends the plan is one the operator meets at the moment it matters;
        // one tucked into an item editor is met only by chance.
        QGCLabel {
            Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 28
            visible:                _root._anyItemTooHigh
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.colorOrange
            text:                   qsTr("Item %1 climbs past the rangefinder's %2 range — %3. Lower it before flying.")
                                        .arg(_root._itemsTooHigh.join(", "))
                                        .arg(_root._limitText)
                                        .arg(_root.gridView ? _root.gridView.altitudeLimitReason : "")
        }

        // What the link is doing, and how far through it is. Above the buttons rather than beside
        // them: this is the answer to "did it go?", and the operator is already looking at the
        // button they pressed to ask.
        ColumnLayout {
            Layout.fillWidth:       true
            Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 28
            spacing:                0
            visible:                _root._syncing || _root._showSyncComplete

            QGCLabel {
                font.pointSize: ScreenTools.smallFontPointSize
                color:          _root._syncing ? qgcPal.text : qgcPal.colorGreen
                text:           _root._syncing
                                    ? qsTr("Transferring… %1%").arg(Math.round(_root._progress * 100))
                                    : qsTr("Transfer complete")
            }

            ProgressBar {
                Layout.fillWidth:   true
                visible:            _root._syncing
                value:              _root._progress
            }
        }

        RowLayout {
            spacing: ScreenTools.defaultFontPixelWidth / 2

            QGCButton {
                objectName: "localGrid_uploadMissionButton"
                // Highlighted while the vehicle is holding something older than what is on screen,
                // since that difference is invisible otherwise
                primary:    _root._dirtyForUpload
                text:       _root._syncing ? qsTr("Sending…") : qsTr("Upload")
                // Held shut rather than warned about twice. This is the failure that runs a vehicle
                // away rather than merely degrading it, and the remedy is one field. Also shut while
                // a transfer is running: pressing it again restarts the one already in flight.
                enabled:    !_root._offline && _root._hasItems && !_root._anyItemTooHigh && !_root._syncing
                onClicked:  _root.planMasterController.sendToVehicle()
            }

            QGCButton {
                objectName: "localGrid_downloadMissionButton"
                text:       qsTr("Download")
                enabled:    !_root._offline && !_root._syncing
                onClicked:  _root._download()
            }
        }

        RowLayout {
            spacing: ScreenTools.defaultFontPixelWidth / 2

            QGCButton {
                text:       qsTr("Save")
                enabled:    _root._hasItems
                onClicked:  _root._save()
            }

            // Both shut while a transfer is running, for the same reason Upload is: what they change
            // is the item list, and the vehicle's reply to the transfer in flight rebuilds it. A
            // plan loaded into that window is thrown away, and a second Clear is refused outright by
            // MissionController -- after the operator has already answered its confirmation.
            QGCButton {
                text:       qsTr("Load")
                enabled:    !_root._syncing
                onClicked:  _root._load()
            }

            QGCButton {
                objectName: "localGrid_clearMissionButton"
                text:       qsTr("Clear")
                enabled:    _root._canClear && !_root._syncing
                onClicked:  _root._clear()
            }
        }
    }
}
