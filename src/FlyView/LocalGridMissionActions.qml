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

    /// The most room this panel may take. Past it the body scrolls rather than the panel running off
    /// the top of the view or into the tool strip that shares this corner. Zero for no limit.
    property real maximumHeight: 0


    /// Folded away to leave the grid clear, keeping the title so it can be found again -- the same
    /// idiom the readout and the mission list already use, rather than a drawer of its own.
    ///
    /// Starts folded on a view too small to carry every panel open at once (see gridView.compact),
    /// open everywhere else so nothing changes on a desktop. Assigning to this later -- the header
    /// below does, on a click -- breaks the binding and leaves the operator's own choice in charge.
    property bool collapsed: gridView ? gridView.compact : false

    /// What to give this panel for a height while it is folded
    readonly property real collapsedHeight: titleHeaderBlock.implicitHeight + (_margins * 2)

    /// What is left for the scrollable body once the fixed title and the outer margins have had theirs
    readonly property real _bodyMaximumHeight: (maximumHeight > 0)
                                                ? Math.max(0, maximumHeight - titleHeaderBlock.implicitHeight
                                                                - layout.spacing - (_margins * 2))
                                                : Number.POSITIVE_INFINITY

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
    // On screen while there is a plan controller behind it, and only while the aircraft is disarmed.
    //
    // Everything this panel holds is work done between flights. Upload is refused outright while the
    // vehicle is flying a mission -- ArduPilot rewrites its mission store in place -- Load and Clear
    // would leave the grid drawing a pattern the aircraft is not flying, and neither after-flight
    // control can be used in the air at all. What would be left in the air is a titled box holding a
    // warning about what to lower before flying and a transfer bar for a transfer that cannot start,
    // which reads as a panel that has failed rather than as one with nothing to do. It comes back on
    // disarm with the operator's own fold left as they set it.
    visible:        _hasController && !_vehicleArmed

    property real _margins: ScreenTools.defaultFontPixelHeight / 3

    readonly property string _limitText: (gridView && gridView.altitudeLimitKnown)
                                            ? (gridView.gridTransform.toDisplay(gridView.altitudeLimitMetres).toFixed(1)
                                               + " " + gridView.gridTransform.displayUnits)
                                            : qsTr("--")

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    // Same reason as LocalGridMissionList: the actions body scrolls when the panel is capped, and a
    // Flickable at its bounds lets the wheel through to the grid's zoom underneath.
    DeadMouseArea {
        anchors.fill: parent
    }

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

    // ---------------- Standing the aircraft back on the origin ----------------

    /// The other remedy for a drifted frame, and the one to reach for first: it repairs the estimate
    /// instead of working around it.
    ///
    /// Done outright rather than through the correction dialog. That dialog exists to let an operator
    /// check a claim they judged by eye off a grid with nothing on it to judge against -- the offsets,
    /// the coordinate, the fallback, the last resort. None of that applies here: the origin is a mark
    /// on the ground the aircraft was carried back to, the offsets are zero by definition, and a
    /// dialog whose every field reads 0.00 is a page of confirmation for a claim with nothing in it
    /// to confirm.
    readonly property var  _vehicle:     gridView ? gridView.vehicle : null
    readonly property bool _originKnown: gridView ? gridView.originKnown : false

    /// How far the estimator had drifted, which is simply where it says the aircraft is: the claim
    /// being made is the origin, so the reported position *is* the error being corrected.
    readonly property real _driftNorth: gridView ? gridView.vehicleNorth : NaN
    readonly property real _driftEast:  gridView ? gridView.vehicleEast  : NaN
    /// How well a mark on the ground is known once the aircraft has been stood square on it. Not a
    /// formality: the estimator weighs the correction against this, so a figure invented large enough
    /// to feel safe is a figure that barely moves the position.
    readonly property real _originAccuracyMetres: 1.0

    property bool   _awaitingOriginReply: false
    property bool   _originReplied:       false
    property bool   _originAccepted:      false
    property string _originReason:        ""

    /// The drift as it stood when the correction went out, held so the result can name the number the
    /// operator just repaired -- which is the measurement a GNSS-denied flight is being flown for
    property real _correctedNorth: NaN
    property real _correctedEast:  NaN

    readonly property bool _canStandOnOrigin: (_vehicle !== null) && _originKnown && !_vehicleArmed
                                                && !_awaitingOriginReply

    /// The button carries no line saying why it is shut, because there is no case left in which it is
    /// shut and on screen. It appears only once the estimator has an origin -- which needs a vehicle
    /// -- and the whole after-flight section stands down while that vehicle is armed. What is left is
    /// a button that is either pressable or already correcting, and its own label says which.

    function _standOnOrigin() {
        if (!_canStandOnOrigin) {
            return
        }

        _correctedNorth      = _driftNorth
        _correctedEast       = _driftEast
        _originReplied       = false
        _originReason        = ""
        _awaitingOriginReply = true
        _vehicle.sendExternalPositionEstimate(gridView.originCoordinate, _originAccuracyMetres)
    }

    /// A pair of offsets as one line, each named for the direction it points. "2.0 m south" is a thing
    /// an operator can check against the field in front of them; "-2.0 m north" is a thing they have
    /// to decode.
    function _offsetText(northMetres, eastMetres) {
        if (!gridView || !gridView.gridTransform || isNaN(northMetres) || isNaN(eastMetres)) {
            return qsTr("an unknown distance")
        }

        const transform = gridView.gridTransform
        const units     = transform.displayUnits
        const northText = Math.abs(transform.toDisplay(northMetres)).toFixed(2) + " " + units
                            + " " + (northMetres < 0 ? qsTr("south") : qsTr("north"))
        const eastText  = Math.abs(transform.toDisplay(eastMetres)).toFixed(2) + " " + units
                            + " " + (eastMetres < 0 ? qsTr("west") : qsTr("east"))
        return northText + ", " + eastText
    }

    // The answer comes from the vehicle rather than from the call, because the vehicle is what
    // decides. Kept here rather than raised as one of QGC's generic command failures: the refusals
    // carry the diagnosis -- firmware built without the feature, an estimator that has stopped aiding
    // and cannot take a correction -- and a banner saying a command failed throws all of that away.
    Connections {
        target:  _root._vehicle
        enabled: _root._vehicle !== null

        function onExternalPositionEstimateResult(accepted, reason) {
            if (!_root._awaitingOriginReply) {
                return
            }
            _root._awaitingOriginReply = false
            _root._originReplied       = true
            _root._originAccepted      = accepted
            _root._originReason        = reason
            // Only the good news goes away by itself. A refusal is the operator's next problem and
            // stays until they try again.
            if (accepted) {
                standOnOriginNoticeTimer.restart()
            }
        }
    }

    Timer {
        id:             standOnOriginNoticeTimer
        interval:       8000
        onTriggered:    _root._originReplied = false
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

        // The row is wrapped so the mouse area covering it has a sibling to anchor to. Anchored
        // straight onto the RowLayout it would be an anchored child of a layout, which Qt calls
        // undefined behaviour and warns about on every build of the grid.
        Item {
            id:                 titleHeaderBlock
            Layout.fillWidth:   true
            // Needed for the same reason actionsFlickable below needs its own implicitWidth: this
            // panel sizes itself from implicitWidth rather than being given a width from outside (the
            // way missionList is), and an Item does not pick up a RowLayout child's width on its own.
            // Without it, the moment the body below is folded away, this header -- the only child
            // still contributing anything -- collapses this whole panel to the width of its chevron.
            implicitWidth:      titleRow.implicitWidth
            implicitHeight:     titleRow.implicitHeight

            // Declared before the row rather than after it, so the controls in the row are the ones
            // on top. A mouse area covering the whole header and declared last takes every press in
            // it, including the presses meant for the help switch beside the title -- which then
            // folds the panel instead of answering.
            QGCMouseArea {
                objectName: "localGrid_missionActionsHeader"
                fillItem:   parent
                onClicked:  _root.collapsed = !_root.collapsed
            }

            RowLayout {
                id:                     titleRow
                anchors.left:           parent.left
                anchors.right:          parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing:                ScreenTools.defaultFontPixelWidth / 2

                QGCColoredImage {
                    Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight * 0.75
                    Layout.preferredHeight: Layout.preferredWidth
                    Layout.alignment:       Qt.AlignVCenter
                    source:                 "/InstrumentValueIcons/cheveron-right.svg"
                    color:                  qgcPal.text
                    rotation:               _root.collapsed ? 0 : 90
                }

                QGCLabel {
                    Layout.alignment:   Qt.AlignVCenter
                    Layout.fillWidth:   true
                    font.pointSize:     ScreenTools.smallFontPointSize
                    font.bold:          true
                    text:               qsTr("Mission")
                }
            }
        }

        // Everything the operator does with the plan, below the fixed title above. Wrapped in a
        // Flickable and capped by maximumHeight for the same reason LocalGridMissionList's rows are:
        // nothing here used to stop this panel growing taller than the window had room for, and on a
        // short screen it grew straight into the tool strip that shares this corner.
        QGCFlickable {
            id:                 actionsFlickable
            visible:            !_root.collapsed
            // Flickable does not pick up its contentItem's natural size the way a plain Item or a
            // Layout would, so without this the panel's own implicitWidth -- unconstrained, computed
            // bottom-up from its children -- would collapse to zero the moment the buttons and labels
            // moved in here.
            implicitWidth:          bodyLayout.implicitWidth
            Layout.fillWidth:       true
            Layout.preferredHeight: Math.min(bodyLayout.implicitHeight, _root._bodyMaximumHeight)
            contentWidth:       width
            contentHeight:      bodyLayout.implicitHeight

            ColumnLayout {
                id:         bodyLayout
                width:      actionsFlickable.width
                spacing:    layout.spacing

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

                // Everything above moves a plan between the grid, a file and the vehicle. Everything below is
                // what an operator does between two flights, and the two were reading as one undifferentiated
                // stack of buttons.
                //
                // Stood down while the aircraft is armed. The panel as a whole goes with it today, so this
                // is not what takes the section off the screen -- it is the section keeping the rule that
                // belongs to it rather than borrowing the panel's. Neither control here can be used in the
                // air whatever else is on show: a position correction is a step change the position
                // controller flies straight out, and moving the plan under an aircraft already flying it
                // changes where it is going mid-flight. The resume warning below has always said so this
                // way; this is the rest of the section saying it too.
                ColumnLayout {
                    objectName:             "localGrid_afterFlightSection"
                    Layout.fillWidth:       true
                    spacing:                bodyLayout.spacing
                    visible:                !_root._vehicleArmed

                    QGCLabel {
                        Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 3
                        font.pointSize:     ScreenTools.smallFontPointSize
                        font.bold:          true
                        text:               qsTr("After a flight")
                    }

                    // First of the two remedies for a drifted frame, because it repairs the estimate rather than
                    // working around it. One press: there is nothing here to confirm that the button does not
                    // already say.
                    QGCButton {
                        objectName:         "localGrid_standOnOriginButton"
                        Layout.fillWidth:   true
                        visible:            _root._originKnown
                        text:               _root._awaitingOriginReply
                                                ? qsTr("Correcting…")
                                                : qsTr("Vehicle is on the origin")
                        enabled:            _root._canStandOnOrigin
                        onClicked:          _root._standOnOrigin()
                    }

                    // Names the distance that was repaired, not just that something happened. On a flight flown
                    // to measure how far an estimator wanders, that number is the result -- and it is gone the
                    // moment the correction lands, so this is the only place it can be read.
                    QGCLabel {
                        objectName:             "localGrid_standOnOriginResult"
                        Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 28
                        visible:                _root._originReplied
                        wrapMode:               Text.WordWrap
                        font.pointSize:         ScreenTools.smallFontPointSize
                        color:                  _root._originAccepted ? qgcPal.colorGreen : qgcPal.colorOrange
                        text:                   _root._originAccepted
                                                    ? qsTr("Position corrected. The estimator had drifted %1.")
                                                        .arg(_root._offsetText(_root._correctedNorth, _root._correctedEast))
                                                    : _root._originReason
                    }

                    // The fallback, under the repair it falls back from. It is the one control here that rewrites
                    // the pattern rather than moving it between the grid and the vehicle.
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
    }
}
