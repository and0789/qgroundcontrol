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
            gridView.resetPlanAnchor()
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

    /// Whether the plan could be moved to start from where the aircraft is standing now
    readonly property bool _canReanchor: gridView ? gridView.canReanchorPlan : false

    /// Which item of the plan the vehicle would pick up at, or 0 for the beginning
    readonly property int _vehicleResumeItemNumber: gridView ? gridView.vehicleResumeItemNumber : 0

    /// True when starting the plan as it stands would begin part way along the route.
    ///
    /// Held to a plan that matches the one on the vehicle. With unsent edits on the grid the index
    /// the vehicle reports counts against a different plan, and naming an item number off this one
    /// would point at the wrong waypoint. Uploading resets the index anyway, which is why an unsent
    /// plan needs no warning of its own.
    readonly property bool _willResumeMidPlan: !_offline && _hasItems && !_dirtyForUpload
                                                   && !_vehicleArmed && (_vehicleResumeItemNumber > 1)

    readonly property bool _vehicleArmed: gridView ? gridView.vehicleArmed : false

    /// Puts the vehicle's mission index back to the head of the plan it is holding, so the next Auto
    /// flies the route from the start rather than resuming where the last flight left off.
    function _restartPlanOnVehicle() {
        _confirm(qsTr("Start From The Beginning"),
                 qsTr("Send the vehicle back to the first item of this plan? It is holding item %1, "
                      + "where the last flight stopped.").arg(_vehicleResumeItemNumber),
                 function() { _root.gridView.restartPlanOnVehicle() })
    }

    /// Why it could not be, for the panel to say out loud
    readonly property string _reanchorBlockedReason: gridView ? gridView.reanchorBlockedReason : ""

    readonly property string _reanchorOffsetText: {
        if (!gridView || !gridView.gridTransform || isNaN(gridView.reanchorNorthMetres)) {
            return qsTr("--")
        }
        const transform = gridView.gridTransform
        return qsTr("%1 %3 north, %2 %3 east")
                    .arg(transform.toDisplay(gridView.reanchorNorthMetres).toFixed(1))
                    .arg(transform.toDisplay(gridView.reanchorEastMetres).toFixed(1))
                    .arg(transform.displayUnits)
    }

    /// How many items the last move shifted, or -1 when nothing has been moved yet. Shown for a
    /// moment afterwards: the pattern moving on the grid is easy to miss on a screen in sunlight,
    /// and the move is only half done until the plan is uploaded.
    property int _reanchoredItemCount: -1

    /// Moves the pattern to where the aircraft is standing, so the plan just flown can be flown
    /// again from here.
    ///
    /// The plan is held as coordinates worked out from the origin -- the point the aircraft stood on
    /// when the pattern was drawn. After a flight it is standing somewhere else, so re-flying the
    /// same plan sends it back over the ground it has already covered, starting part way along the
    /// route rather than at the head of it. Rebooting the aircraft was the only way to move the
    /// frame under the plan, because ArduPilot will not take a second origin; this moves the plan
    /// instead and needs nothing from the firmware.
    ///
    /// It asks first, and says which way and how far, because the direction is the whole of it: a
    /// pattern moved the wrong way ends up twice as far from where it was meant to be flown.
    function _reanchor() {
        _confirm(qsTr("Fly From Here"),
                 qsTr("Move the plan to start from where the aircraft is standing — %1? "
                      + "The pattern keeps its shape. Nothing reaches the vehicle until it is uploaded.")
                    .arg(_reanchorOffsetText),
                 function() {
                     _root._reanchoredItemCount = _root.gridView.reanchorPlanToVehicle()
                     reanchorNoticeTimer.restart()
                 })
    }

    /// Shows the plan the vehicle is holding, throwing away whatever is on the grid.
    ///
    /// showPlanFromManagerVehicle rather than loadFromVehicle: the latter refuses outright in the
    /// fly view -- it logs and returns, so the button did nothing at all -- because the fly view's
    /// controller is a mirror of the vehicle rather than an editor of its own. This rebuilds the
    /// item list from the copy that mirror already holds, which is what "what is on the vehicle"
    /// means here.
    function _download() {
        _confirm(qsTr("Download"),
                 qsTr("Replace the plan here with the one on the vehicle? Anything not sent is lost."),
                 function() {
                     _root.planMasterController.showPlanFromManagerVehicle()
                     if (_root.gridView) {
                         _root.gridView.resetPlanAnchor()
                     }
                 })
    }

    function _load() {
        fileDialog.title =       qsTr("Select Plan File")
        fileDialog.nameFilters = planMasterController.loadNameFilters
        fileDialog.openForLoad()
    }

    /// Sends the plan to the vehicle, through the pre-check the Plan view runs before its own upload.
    ///
    /// Upload here used to call sendToVehicle straight out, and the two states that check catches are
    /// the ones this view meets more than any other does:
    ///
    /// A mission already running -- ArduPilot rewrites its mission store in place, so a write landing
    /// while Auto is flying the old plan leaves the aircraft part way through a route that no longer
    /// exists.
    ///
    /// A plan file built for another firmware or vehicle class -- its commands upload cleanly and
    /// mean something else on this aircraft. A plan that gets as far as the takeoff and no further is
    /// what that looks like from the ground.
    function _upload() {
        if (!_hasController || !_missionController) {
            return
        }

        // Refused rather than asked about: an item still waiting on data carries no coordinate to
        // send, so there is no decision here for the operator to make.
        const readyState = planMasterController.readyForSaveState()
        if (readyState !== VisualMissionItem.ReadyForSave) {
            QGroundControl.showMessageDialog(
                _root, qsTr("Upload"),
                (readyState === VisualMissionItem.NotReadyForSaveTerrain)
                    ? qsTr("The plan is still waiting on terrain data for one of its items. Try again once it arrives.")
                    : qsTr("The plan holds an item that is not finished, so there is nothing to send for it yet."),
                Dialog.Ok)
            return
        }

        switch (_missionController.sendToVehiclePreCheck()) {
        case MissionController.SendToVehiclePreCheckStateOk:
            planMasterController.sendToVehicle()
            break
        case MissionController.SendToVehiclePreCheckStateNoActiveVehicle:
            QGroundControl.showMessageDialog(
                _root, qsTr("Upload"),
                qsTr("There is no vehicle connected to send the plan to."), Dialog.Ok)
            break
        case MissionController.SendToVehiclePreCheckStateActiveMission:
            QGroundControl.showMessageDialog(
                _root, qsTr("Upload"),
                qsTr("The vehicle is flying this mission. Pause it before sending a new plan — a plan "
                     + "written underneath a running mission leaves the aircraft part way through a "
                     + "route that is no longer there."), Dialog.Ok)
            break
        case MissionController.SendToVehiclePreCheckStateFirwmareVehicleMismatch:
            _confirm(qsTr("Upload"),
                     qsTr("This plan was built for a different firmware or vehicle type than the one "
                          + "connected. Its commands will upload cleanly and can mean something else "
                          + "on this aircraft. Send it anyway?"),
                     function() { _root.planMasterController.sendToVehicle() })
            break
        }
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

    Timer {
        id:             reanchorNoticeTimer
        interval:       8000
        onTriggered:    _root._reanchoredItemCount = -1
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
            // A plan off disk carries the coordinates it was drawn with, around whatever origin its
            // author was standing on. Where the last plan had been moved to says nothing about it.
            if (_root.gridView) {
                _root.gridView.resetPlanAnchor()
            }
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
                onClicked:  _root._upload()
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

        // On its own row, under the transfer buttons, because it is the one control here that
        // rewrites the pattern rather than moving it between the grid and the vehicle.
        QGCButton {
            objectName:         "localGrid_flyFromHereButton"
            Layout.fillWidth:   true
            text:               qsTr("Fly this plan from here")
            // Shut while armed as well as while a transfer runs. Moving the plan under an aircraft
            // that is already flying it changes where it is going mid-flight, which is not what
            // anyone reaching for this between flights means by it.
            enabled:            _root._canReanchor && !_root._syncing
            onClicked:          _root._reanchor()
        }

        // Why it is shut, for the case an operator meets after every flight. A button that goes
        // dead with no reason teaches them the feature is broken.
        QGCLabel {
            objectName:             "localGrid_flyFromHereReason"
            Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 28
            visible:                _root._reanchorBlockedReason !== ""
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.colorGrey
            text:                   _root._reanchorBlockedReason
        }

        // What the move did, and the half of it that is still outstanding. The pattern shifting on
        // the grid is easy to miss, and a plan moved but not sent is the plan the aircraft already
        // has -- which is the one the operator was trying to get away from.
        QGCLabel {
            objectName:             "localGrid_flyFromHereResult"
            Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 28
            visible:                _root._reanchoredItemCount >= 0
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.colorGreen
            text:                   qsTr("%1 item(s) moved. Upload the plan to fly it from here.")
                                        .arg(_root._reanchoredItemCount)
        }

        // Where the aircraft would pick this plan up if it were started as it stands. ArduPilot
        // resumes rather than restarts -- MIS_RESTART defaults to Resume -- so entering Auto after a
        // flight that was cut short carries on from the item it stopped on. The aircraft takes off
        // and then flies to the middle of the route, which is what a second flight "not working"
        // looks like from the ground. Uploading clears the vehicle's mission and puts this back to
        // the head of the plan, and so does the button below.
        ColumnLayout {
            Layout.fillWidth:       true
            Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 28
            spacing:                ScreenTools.defaultFontPixelHeight / 6
            visible:                _root._willResumeMidPlan

            QGCLabel {
                objectName:         "localGrid_resumePointWarning"
                Layout.fillWidth:   true
                wrapMode:           Text.WordWrap
                font.pointSize:     ScreenTools.smallFontPointSize
                color:              qgcPal.colorOrange
                text:               qsTr("The vehicle would start this plan at item %1, not at the beginning — it is holding the place the last flight stopped at.")
                                        .arg(_root._vehicleResumeItemNumber)
            }

            QGCButton {
                objectName:         "localGrid_restartPlanButton"
                Layout.fillWidth:   true
                text:               qsTr("Start plan from the beginning")
                enabled:            !_root._syncing
                onClicked:          _root._restartPlanOnVehicle()
            }
        }
    }
}
