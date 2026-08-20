import QtQuick
import QtPositioning

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView

/// A plan of the ground in the estimator's own frame: metres north and east of the origin, drawn as
/// a measured grid rather than as a map.
///
/// A vehicle navigating without GNSS has no meaningful latitude to plot. Its position is an
/// integration of velocity since the origin was set, and the question the operator actually needs
/// answered -- how far, and in what direction, from where we started -- is one a world map answers
/// badly and this answers directly. The grid is north-up, so a bearing read off it is a bearing that
/// can be flown.
Item {
    id: _root

    property var vehicle: null

    /// The plan being flown, so its waypoints can be drawn on the frame they will be flown in
    property var missionController: null

    /// The controller that owns the plan as a whole, for sending, saving and clearing it
    property var planMasterController: null

    /// Where the fly view's own widgets already are. The grid draws behind them, so its readouts
    /// have to be kept out from under the tool strip and the instrument panels rather than laid out
    /// against the bare edges of the window.
    property var toolInsets: null

    /// How much of the top of this item the toolbar covers. The grid fills the whole window, while
    /// toolInsets are measured from below the toolbar, so without this the top row of the grid's own
    /// widgets is laid out into a strip that is already painted over.
    property real topEdgeOffset: 0

    /// Keeps the vehicle in the middle of the view. Dragging turns it off, since the operator has
    /// then said where they want to look.
    property bool followVehicle: true

    /// The point the grid's (0,0) is anchored to. LOCAL_POSITION_NED is measured from the vehicle's
    /// estimator origin, so a mission drawn against anything else would be drawn against a different
    /// frame than the aircraft is flying in.
    readonly property var originCoordinate: vehicle ? vehicle.estimatorOrigin : QtPositioning.coordinate()
    readonly property bool originKnown: originCoordinate.isValid

    /// Waypoints in grid metres, rebuilt whenever the plan or the origin moves
    readonly property var missionPoints: _buildMissionPoints()

    readonly property bool positionValid: _localPosition
                                            ? (_localPosition.telemetryAvailable && !isNaN(_north) && !isNaN(_east))
                                            : false
    readonly property real vehicleNorth: _north
    readonly property real vehicleEast:  _east

    /// Where the nose points, in degrees clockwise from north, or NaN when the vehicle has not said.
    ///
    /// Read as a number rather than drawn as a second dial. The fly view's own instrument panel
    /// already carries a compass, and two pictures of one heading cost the top of the screen and
    /// leave the operator checking whether they disagree. NaN survives to the readout on purpose: an
    /// unknown heading shown as zero is a heading pointing confidently at north.
    readonly property real vehicleHeadingDegrees: _headingDegrees

    /// How long the estimate may go without a message before it stops being treated as current.
    /// LOCAL_POSITION_NED arrives at around 10 Hz, so this is many missed messages rather than one
    /// late one -- a threshold that trips on ordinary link jitter teaches the operator to ignore it.
    property int stalePositionTimeoutMs: 3000

    /// True when the position on screen is the last one that arrived rather than the current one.
    ///
    /// This is the failure this whole view has to survive. Without GNSS the grid is the only picture
    /// of where the aircraft is, and telemetryAvailable is a one-way latch -- FactGroup never sets
    /// it back to false -- so a vehicle that stops reporting leaves its marker frozen exactly where
    /// it was last seen. On a grid, a frozen marker and a hovering aircraft are the same image, and
    /// the operator flies on believing a position that stopped being true minutes ago.
    readonly property bool positionStale: positionValid && _positionStale

    /// Seconds since the last position message, or NaN before the first one
    readonly property real positionAgeSeconds: (_lastPositionMSecs > 0)
                                                ? ((_ageClock - _lastPositionMSecs) / 1000)
                                                : NaN

    property bool _positionStale:    false
    property real _lastPositionMSecs: 0
    /// Sampled rather than read live: "now" is not a value anything can bind to, so the age would
    /// never recompute on its own
    property real _ageClock:         0

    Connections {
        target:  _root._localPosition
        enabled: _root._localPosition !== null

        // Every message restarts the countdown, including one that reports the same position as the
        // last. A vehicle holding station is still reporting.
        function onUpdated() {
            _root._lastPositionMSecs = Date.now()
            _root._ageClock = _root._lastPositionMSecs
            _root._positionStale = false
            staleTimer.restart()
        }
    }

    Timer {
        id:       staleTimer
        interval: _root.stalePositionTimeoutMs
        onTriggered: {
            // Read the clock here as well as on the ticker. Left to the ticker alone the age is
            // still zero at the moment the warning appears, so the first thing the operator reads is
            // "0 s old" beside a warning that the position is not current -- which reads as a bug in
            // the warning rather than a fault in the estimate.
            _root._ageClock = Date.now()
            _root._positionStale = true
            ageTicker.start()
        }
    }

    /// Only runs once the estimate has gone stale, so a healthy flight is not repainting a clock
    Timer {
        id:         ageTicker
        interval:   500
        repeat:     true
        running:    false
        onTriggered: {
            _root._ageClock = Date.now()
            if (!_root._positionStale) {
                stop()
            }
        }
    }

    onVehicleChanged: {
        // A new aircraft has not gone silent, it has simply not spoken yet
        _positionStale = false
        _lastPositionMSecs = 0
        _ageClock = 0
        staleTimer.stop()
        ageTicker.stop()

        // And a trail from the previous aircraft, drawn against this one's origin, is a picture of
        // a flight that never happened
        trail.reset()
    }

    property var  _localPosition:   vehicle ? vehicle.localPosition : null
    property real _north:           _localPosition ? _localPosition.x.rawValue : NaN
    property real _east:            _localPosition ? _localPosition.y.rawValue : NaN
    property real _headingDegrees:  vehicle ? vehicle.heading.rawValue : NaN

    property real _margins:         ScreenTools.defaultFontPixelHeight / 2

    /// The most the readout/mission-list column on the right is allowed to widen to.
    ///
    /// A floor with no ceiling -- the shape this used to be -- gives every panel in the column room
    /// to grow but nothing that ever asks it to stop, so a warning sentence or a long plan could take
    /// half a phone screen. Matched to the Plan view's own right panel (PlanView.qml's
    /// _rightPanelWidth): a third of the view, or 30 characters, whichever is narrower. On a desktop
    /// that is 30 characters, close enough to the 28 this column used to float at that nothing here
    /// should look different; on a phone it is a third of a much smaller number.
    readonly property real _rightColumnMaximumWidth: Math.min(width / 3, ScreenTools.defaultFontPixelWidth * 30)

    /// True while this view is too small to carry every panel open at once.
    ///
    /// Derived from this view's own size, never from ScreenTools.isMobile: --fake-mobile flips that
    /// flag without changing the window, and a real phone can still report a large Screen through it.
    /// Sizing off the view's own width and height is also the only form a test can drive, since a
    /// test resizes the window rather than the platform it thinks it is running on.
    readonly property bool compact: (width < ScreenTools.defaultFontPixelWidth * 110)
                                        || (height < ScreenTools.defaultFontPixelHeight * 32)

    /// Roughly how far apart grid lines should sit before the spacing is rounded to a countable one
    property real _targetGridPixels: ScreenTools.defaultFontPixelHeight * 4

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    readonly property color _gridMinorColor: Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.15)
    readonly property color _gridMajorColor: Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.35)
    readonly property color _axisColor:      Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.7)

    LocalGridTransform {
        id:         transform
        viewWidth:  _root.width
        viewHeight: _root.height

        onCentreNorthChanged:   _root._repaintAll()
        onCentreEastChanged:    _root._repaintAll()
        onMetresPerPixelChanged: _root._repaintAll()
        onViewWidthChanged:     _root._repaintAll()
        onViewHeightChanged:    _root._repaintAll()
    }

    LocalGridTrail {
        id: trail
    }

    LocalGridProjection {
        id: projection
    }

    LocalGridAltitudeLimit {
        id:      altitudeLimit
        vehicle: _root.vehicle
    }

    LocalGridEstimatorHealth {
        id:      estimatorHealth
        vehicle: _root.vehicle
    }

    /// The estimator's verdict on its own solution, exposed so the readout can say it in words
    readonly property alias estimatorDegraded: estimatorHealth.degraded
    readonly property alias estimatorSevere:   estimatorHealth.severe
    readonly property alias estimatorWarning:  estimatorHealth.warning

    /// Exposed so the waypoint panel can warn against it without reaching for parameters itself
    readonly property alias altitudeLimitMetres: altitudeLimit.limitMetres
    readonly property alias altitudeLimitKnown:  altitudeLimit.limitKnown
    readonly property alias altitudeLimitReason: altitudeLimit.limitReason

    /// Where the vehicle is against that ceiling right now, rather than where the plan put it
    readonly property alias heightNearCeiling:  altitudeLimit.nearCeiling
    readonly property alias heightAboveCeiling: altitudeLimit.aboveCeiling
    readonly property alias currentHeightMetres: altitudeLimit.currentHeightMetres

    /// How high a return to launch would climb, and whether that leaves the height reference behind
    readonly property alias returnAltitudeMetres:       altitudeLimit.returnAltitudeMetres
    readonly property alias returnAltitudeAboveCeiling: altitudeLimit.returnAltitudeAboveCeiling

    /// The numbers of every item in the plan whose altitude climbs past that ceiling.
    ///
    /// Scanned across the whole plan rather than reported per selected item. A warning that appears
    /// only when the operator happens to be looking at the offending waypoint is not a warning: the
    /// item that ran a flight away was the takeoff, while the item on screen was the landing.
    readonly property var itemsAboveAltitudeLimit: _findItemsAboveAltitudeLimit()

    function _findItemsAboveAltitudeLimit() {
        const offenders = []
        if (!altitudeLimit.limitKnown) {
            return offenders
        }

        const points = missionPoints
        for (var i = 0; i < points.length; i++) {
            const fact = waypointAltitudeFact(points[i].index)
            // rawValue is read inside the loop on purpose: it makes this binding depend on every
            // item's altitude, so editing one re-runs the scan
            if (fact && altitudeLimit.exceeds(fact.rawValue)) {
                offenders.push(points[i].number)
            }
        }
        return offenders
    }

    /// @return true when this altitude would climb past the rangefinder the estimator takes its
    /// height from
    function altitudeExceedsLimit(metres) {
        return altitudeLimit.exceeds(metres)
    }

    /// Brings an altitude back under the ceiling: far enough under the rangefinder's range that the
    /// estimator keeps its height reference rather than sitting on the edge of losing it.
    ///     @return the altitude to use, which is the one given whenever it was already flyable
    function clampAltitude(metres) {
        return altitudeLimit.exceeds(metres) ? altitudeLimit.safeDefaultMetres : metres
    }

    /// Walks the plan and turns every item in it into a row, working out grid offsets for the ones
    /// that can be drawn. Reading missionController.visualItems.count and the origin here is
    /// deliberate: both are what this depends on, and touching them makes the binding re-run when a
    /// waypoint is added or the vehicle's origin changes.
    ///
    /// Every item, not only the ones with a coordinate. ArduPilot's takeoff carries no coordinate at
    /// all -- it climbs in place, so the firmware describes it as altitude-only -- and a plan list
    /// built from drawable items alone left it out entirely. The operator then saw a plan starting
    /// at "2", could not set the takeoff's altitude with the rest of the pattern, and got no warning
    /// when that one item was the one above the rangefinder's range. An item with nowhere to be
    /// drawn is still an item the aircraft will fly.
    function _buildMissionPoints() {
        const points = []
        if (!missionController || !originKnown) {
            return points
        }

        const items = missionController.visualItems
        if (!items) {
            return points
        }

        for (var i = 0; i < items.count; i++) {
            const item = items.get(i)
            if (!item) {
                continue
            }
            // The plan's first visual item is the mission settings, which carries the planned home
            // position as its coordinate. Drawn as a waypoint it lands on top of the origin marker
            // and, worse, makes a plan that has just been cleared look like it already holds a
            // route -- which is what stopped a new plan from starting with a takeoff.
            if (item.homePosition === true) {
                continue
            }

            var north = NaN
            var east = NaN
            var onGrid = false
            if (item.specifiesCoordinate && item.coordinate.isValid) {
                const offsets = projection.northEastFrom(originCoordinate, item.coordinate)
                if (offsets) {
                    north = offsets.north
                    east = offsets.east
                    onGrid = true
                }
            }

            // Where the vehicle's own mission index falls for this item. An item can occupy more
            // than one place in the uploaded mission -- a waypoint carrying a speed is flown as a
            // NAV_WAYPOINT followed by a DO_CHANGE_SPEED -- so the match is against the span rather
            // than the first number of it, otherwise no row is marked while the vehicle is working
            // through the second half of one.
            const firstSequence = item.sequenceNumber
            const lastSequence = (item.lastSequenceNumber === undefined) ? firstSequence
                                                                         : item.lastSequenceNumber

            points.push({
                north:      north,
                east:       east,
                /// False for an item the grid has nowhere to put: it is listed, but no marker is
                /// drawn for it and it cannot be dragged or moved with the rest of the plan
                onGrid:     onGrid,
                /// True for an item the vehicle actually flies through -- a waypoint, a landing --
                /// and false for one that carries a coordinate without flying to it, such as an ROI
                /// or a return to launch. Separate from onGrid: an ROI is still drawn as a marker
                /// (it is a real, useful place on the grid), but a leg is not measured through it
                /// and the polyline does not run through it either. Same test MissionController
                /// itself uses everywhere it needs this distinction (MissionController.cc:2011 among
                /// others), so an item this grid does not yet know about still classifies correctly.
                flyThrough: item.specifiesCoordinate && !item.isStandaloneCoordinate,
                /// Where this item comes in the plan, counting from one. This is what the row and
                /// the marker show, and it is deliberately not the mission sequence number: those
                /// count the home position and the DO_CHANGE_SPEED items QGC folds into a waypoint,
                /// so a plan of three items read "2" and "4" on screen with nothing to say what
                /// happened to 1 and 3.
                number:     points.length + 1,
                // The index into visualItems, which is what removal and reordering take
                index:      i,
                sequence:     firstSequence,
                lastSequence: lastSequence,
                // Compared against the vehicle's own mission index rather than reading the item's
                // isCurrentItem. Both are written in the fly view: the vehicle advancing sets it,
                // but so does inserting an item, so a freshly placed waypoint marked itself as the
                // one being flown to while the aircraft was still standing on the origin.
                isVehicleTarget: (_root.vehicleTargetSequence >= firstSequence)
                                    && (_root.vehicleTargetSequence <= lastSequence),
                // Takeoffs are listed but not dragged: this one is anchored to the origin
                isPinned:   _isPinnedItem(item)
            })
        }
        return points
    }

    /// The plan's items that have somewhere on the grid to be drawn, in plan order
    /// The plan's items that are actually flown through, in plan order. Used for the polyline and
    /// for measuring legs -- an ROI is drawn on the grid but the aircraft is never routed to it, so
    /// a leg measured through one would describe a turn that is never flown.
    function _drawnMissionPoints() {
        return missionPoints.filter(point => point.flyThrough)
    }

    /// Whether the estimator's frame has slid away from the ground while the aircraft sat on it
    property LocalGridOriginDrift originDrift: LocalGridOriginDrift {
        vehicle: _root.vehicle
    }

    readonly property bool   positionDrifting:    originDrift.drifting
    readonly property string positionDriftWarning: originDrift.warning
    readonly property real   positionDriftMetres:  originDrift.driftMetres

    /// Why the autopilot will not arm, said beside everything else that stops this grid being flown.
    ///
    /// The refusal is worth stating even before its reason arrives, because "will not arm" and "you
    /// have not pressed arm yet" look identical on a grid and only one of them is a problem the
    /// operator can go and fix. The vehicle asks the autopilot for the reason on its own account, so
    /// the wait is measured in seconds rather than the half minute ArduPilot takes to volunteer one.
    readonly property string armingBlockedWarning: {
        if (!vehicle || !vehicle.armingBlocked) {
            return ""
        }
        if (vehicle.prearmError !== "") {
            return qsTr("Will not arm — %1").arg(vehicle.prearmError)
        }
        return qsTr("Will not arm. Asking the autopilot which check is failing…")
    }

    /// The sequence number the vehicle is flying to, or -1 when nothing is.
    ///
    /// MissionController reads this off the vehicle's own mission manager, and answers -1 outside
    /// the fly view. It is the only honest source for this: the items' isCurrentItem flag is also
    /// set when one is inserted, so it says "the item just added" as often as it says "the item
    /// being flown to".
    readonly property int vehicleTargetSequence: missionController ? missionController.currentMissionIndex : -1

    /// True for an item that belongs where it is and may not be moved from the grid.
    ///
    /// The takeoff is the only one. It sits on the origin because that is where the aircraft is
    /// standing, and a multirotor's NAV_TAKEOFF climbs in place whatever coordinate is uploaded with
    /// it -- so a dragged takeoff marker would show a departure the vehicle will not fly. Moving one
    /// also drags the planned home position along with it, since QGC ties the two together.
    function _isPinnedItem(item) {
        return (item !== null) && (item.isTakeoffItem === true)
    }

    /// @return true when the item at this index is anchored where it is
    function waypointIsPinned(index) {
        return _isPinnedItem(_visualItemAt(index))
    }

    onMissionPointsChanged: missionCanvas.requestPaint()

    /// The marker drawn for the nth point, or null where none has been built. Exposed because the
    /// markers are the only part of the plan that can be pointed at, and whether they exist at all
    /// depends on how the Repeater below is modelled -- which has been got wrong once already.
    function waypointMarkerAt(pointIndex) {
        return waypointRepeater.itemAt(pointIndex)
    }

    /// Exposed so the view can be driven from tests and from the surrounding fly view
    readonly property alias gridTransform: transform
    readonly property alias trailPointCount:   trail.pointCount
    readonly property alias trailLengthMetres: trail.pathLengthMetres

    function clearTrail() {
        trail.reset()
        vehicleCanvas.requestPaint()
    }

    /// True while the plan is being sent to, fetched from or cleared on the vehicle.
    ///
    /// Placing anything during one of those loses it. The fly view's mission controller is a mirror
    /// of the vehicle rather than an editor: when a transaction completes it rebuilds its items from
    /// the vehicle's copy, so a waypoint drawn while a Clear is still in flight is thrown away
    /// without a word. The operator finds out at the flight line, when Auto refuses a mission that
    /// was never there.
    readonly property bool planSyncInProgress: planMasterController ? planMasterController.syncInProgress : false

    /// True when a waypoint placed on the grid would land where it was drawn. Without an origin
    /// there is no mapping between this frame and the coordinates a mission is stored in, and a
    /// waypoint invented from a guessed origin uploads cleanly and flies somewhere else.
    readonly property bool canPlaceWaypoints: originKnown && (missionController !== null) && !planSyncInProgress

    // A transfer that has started will rebuild the plan from the vehicle's copy when it lands, so
    // whatever is on the undo entry stops describing anything. Dropped as the transfer starts rather
    // than when it finishes, so the control goes away while the buttons around it do.
    onPlanSyncInProgressChanged: {
        if (planSyncInProgress) {
            _clearUndo()
        }
    }

    /// Where a new item goes: straight after the item currently selected, or after the end of the
    /// plan when nothing is. The rule the Plan view's own insert strip uses (PlanView.qml's
    /// insertSimpleItemAfterCurrent and its siblings), and the same point every insert-validity flag
    /// on the controller -- isInsertTakeoffValid, isInsertLandValid, isInsertROIValid,
    /// flyThroughCommandsAllowed -- is computed against. Read off the controller rather than off
    /// selectedWaypointIndex on purpose: the two are kept in step by selectWaypoint and
    /// clearWaypointSelection below, but the controller is the one place those flags actually come
    /// from, so asking it directly cannot go out of step with what it just answered.
    function _insertIndex() {
        return missionController ? (missionController.currentPlanViewVIIndex + 1) : -1
    }

    /// Adds a mission item at a point on the grid, in metres from the origin.
    ///     @param kind one of "waypoint", "takeoff", "land", "landHere"
    ///     @return true if it was added
    ///
    /// The same three calls the Plan view's insert strip makes, so an item added here is the same
    /// item as one added there -- including that a multirotor's "land" is a return to launch, which
    /// is what the Plan view inserts and labels that way.
    function addMissionItemAt(kind, north, east) {
        if (!canPlaceWaypoints) {
            return false
        }

        // Captured before anything is written, so undo can put the plan back exactly as it was. Both
        // are needed: one tap can add two items when an empty plan gains its takeoff as well.
        const insertAt = _insertIndex()
        const countBefore = _visualItemCount()

        // A plan started from nothing is a new pattern, laid out from the origin like every other
        // one. Wherever the last plan had been moved to describes that plan, not this one, and left
        // standing it would take the first move of this one short by that distance.
        if (planIsEmpty) {
            resetPlanAnchor()
        }

        // Wherever on the grid it was asked for, a takeoff belongs on the origin
        if (kind === "takeoff") {
            return insertTakeoffAtOrigin()
        }

        const coordinate = projection.coordinateAt(originCoordinate, north, east)
        if (!coordinate.isValid) {
            return false
        }

        // A plan that starts with a waypoint does not climb -- the aircraft sits there -- and asking
        // the operator to remember that on every new plan is asking them to remember it on the one
        // flight they forget. The takeoff goes on the origin rather than swallowing the point that
        // was clicked, so the operator still gets the item they asked for where they asked for it.
        // The plan being empty is the only case, so nothing already built is reinterpreted. This
        // also advances the controller's current item to the takeoff just added, which is exactly
        // where the item below still needs to land: right after it.
        if (planIsEmpty) {
            insertTakeoffAtOrigin()
        }

        switch (kind) {
        case "land":
            _applyDefaultAltitude(missionController.insertLandItem(coordinate, _insertIndex(), true /* makeCurrentItem */))
            break
        case "landHere":
            return _insertLandHere(coordinate)
        default: {
            const item = missionController.insertSimpleMissionItem(coordinate, _insertIndex(), true /* makeCurrentItem */)
            _applyDefaultAltitude(item)
            _applyDefaultSpeed(item)
            break
        }
        }

        // Nothing selects the new item here on purpose: makeCurrentItem above already moved the
        // controller's current item to it, and the onPlanViewStateChanged handler further down
        // follows that to keep selectedWaypointIndex in step. Selecting it again from this end used
        // to be done by picking the newest item off the end of the plan (_selectNewestItem), which
        // was correct only while every insert landed at the end -- it is gone along with that
        // assumption.
        _recordInsertUndo(qsTr("Undo add"), insertAt, countBefore)
        return true
    }

    /// Puts the takeoff on the origin, which is the point the aircraft is standing on.
    ///     @return true if it was added
    function insertTakeoffAtOrigin() {
        if (!canPlaceWaypoints || !_takeoffAllowed()) {
            return false
        }

        const insertAt = _insertIndex()
        const countBefore = _visualItemCount()

        const coordinate = projection.coordinateAt(originCoordinate, 0, 0)
        if (!coordinate.isValid) {
            return false
        }

        const item = missionController.insertTakeoffItem(coordinate, _insertIndex(), true /* makeCurrentItem */)
        if (!item) {
            return false
        }

        // Placed again after the insertion on purpose. insertTakeoffItem ignores the coordinate it
        // is handed and puts the item on the plan's home position -- where the vehicle reported it
        // launched from, which is not necessarily where the estimator is counting from. On this grid
        // the origin is the only point a distance can be measured against.
        item.coordinate = coordinate

        _applyDefaultAltitude(item)
        _recordInsertUndo(qsTr("Undo takeoff"), insertAt, countBefore)
        return true
    }

    /// Brings a newly placed item under the ceiling the estimator can actually hold a height at.
    ///
    /// QGC's default mission altitude is chosen for a vehicle with GNSS and a barometer. On one
    /// flying off a rangefinder it is above the only height reference there is, and the way that
    /// fails is silent: the plan uploads cleanly and the aircraft climbs out of range in flight.
    function _applyDefaultAltitude(item) {
        if (!item || !item.altitude) {
            return
        }
        const capped = clampAltitude(item.altitude.rawValue)
        if (capped !== item.altitude.rawValue) {
            item.altitude.rawValue = capped
        }
    }

    /// True when a landing may be placed at the point clicked, rather than only appended past the
    /// end of the plan. "Land here" builds a plain waypoint and swaps its command afterward, so it
    /// never goes through insertLandItem and never picks up isInsertLandValid's own refusal --
    /// without this it would happily insert a landing in the middle of a pattern the aircraft was
    /// never going to stop flying, with nothing after it ever reached.
    readonly property bool canInsertLandHere: canPlaceWaypoints
                                                && (missionController
                                                    ? (missionController.isInsertLandValid || planIsEmpty)
                                                    : false)

    /// Lands the vehicle where it is standing on the grid, rather than flying it home first.
    ///
    /// QGC's own insert strip has no way to say this on a multirotor: its landing button produces a
    /// return to launch. That is the right ending for a mission that starts and finishes in the same
    /// place, and the wrong one for a pattern meant to finish at its far corner.
    ///     @return true if it was added
    function _insertLandHere(coordinate) {
        if (!canInsertLandHere) {
            return false
        }

        const insertAt = _insertIndex()
        const countBefore = _visualItemCount()

        const item = missionController.insertSimpleMissionItem(coordinate, _insertIndex(), true /* makeCurrentItem */)
        if (!item) {
            return false
        }

        item.command = commandLand
        // Not given the default altitude the waypoints get: a landing is flown at the height of the
        // leg that reaches it, whatever that is, so it takes the altitude of the item before it
        syncLandingAltitudes()
        _recordInsertUndo(qsTr("Undo landing"), insertAt, countBefore)
        return true
    }

    /// @return true if it was added
    function addWaypointAt(north, east) {
        return addMissionItemAt("waypoint", north, east)
    }

    /// True when the plan holds nothing yet. The mission settings item is not part of a route, and
    /// neither is an item the vehicle's copy of the plan has not produced yet, but everything the
    /// aircraft would fly counts -- including a takeoff, which carries no coordinate on ArduPilot
    /// and so is nowhere on the grid.
    readonly property bool planIsEmpty: missionPoints.length === 0

    /// True when the plan already begins with a takeoff.
    ///
    /// Exposed so a refused takeoff can be explained rather than shown as a button that does
    /// nothing. "Already has one" and "a takeoff can only go first" are different problems with
    /// different ways out, and the operator cannot tell them apart from a grey button.
    readonly property bool planHasTakeoff: _planHasTakeoff()

    function _planHasTakeoff() {
        const points = missionPoints
        for (var i = 0; i < points.length; i++) {
            // isPinned is set for exactly the takeoff -- it is pinned because it belongs on the
            // origin -- so it is the same answer without walking the items a second time
            if (points[i].isPinned) {
                return true
            }
        }
        return false
    }

    function _takeoffAllowed() {
        return missionController.isInsertTakeoffValid === true
    }

    /// Adds a waypoint under a point on screen, which is what a click on the grid means
    function addWaypointAtPixel(x, y) {
        return addWaypointAt(transform.northForPixelY(y), transform.eastForPixelX(x))
    }

    /// Which insert tool the next click on bare grid places, or "" for none. Set from the fly
    /// view's own tool strip (LocalGridPlanAction's drop panel), which is the only entry point that
    /// can arm one: the strip has no map of its own to click on to summon a drop panel the way the
    /// Plan view's does, so arming here and placing on this grid is how the same "tap the type, tap
    /// the spot" workflow reaches a view with no map under it.
    property string armedTool: ""

    /// Arms an insert tool, or disarms it if it is the one already armed -- the same toggle the Plan
    /// view's own Waypoint and ROI buttons use.
    function toggleArmedTool(tool) {
        armedTool = (armedTool === tool) ? "" : tool
    }

    /// @return true if the armed tool placed something at this point on screen
    function placeArmedToolAtPixel(pixelX, pixelY) {
        return placeArmedTool(transform.northForPixelY(pixelY), transform.eastForPixelX(pixelX))
    }

    /// @return true if the armed tool placed something at this point on the grid, in metres from
    /// the origin
    function placeArmedTool(north, east) {
        switch (armedTool) {
        case "waypoint":
            return addWaypointAt(north, east)
        case "roi":
            return _insertROIAt(north, east)
        default:
            return false
        }
    }

    /// Inserts a return, or a landing on a fixed wing, using the origin as its coordinate -- the
    /// same reason a takeoff always lands on the origin regardless of what was asked for. Used from
    /// the tool strip's drop panel, which triggers this without a click to read a point from.
    ///     @return true if it was added
    function insertReturnOrLandItem() {
        return addMissionItemAt("land", 0, 0)
    }

    /// Marks where the vehicle should point its yaw and, on a supporting firmware, its camera --
    /// the one absolute reference this grid has that is not itself estimated. A pattern flown with
    /// an ROI and one flown without are two different experiments: the ROI turns the airframe, and
    /// the optical flow sensor turns with it.
    ///     @return true if it was added
    function _insertROIAt(north, east) {
        if (!canPlaceWaypoints || !missionController || (missionController.isInsertROIValid !== true)) {
            return false
        }

        const coordinate = projection.coordinateAt(originCoordinate, north, east)
        if (!coordinate.isValid) {
            return false
        }

        const insertAt = _insertIndex()
        const countBefore = _visualItemCount()

        if (planIsEmpty) {
            resetPlanAnchor()
            insertTakeoffAtOrigin()
        }

        missionController.insertROIMissionItem(coordinate, _insertIndex(), true /* makeCurrentItem */)
        _recordInsertUndo(qsTr("Undo ROI"), insertAt, countBefore)
        return true
    }

    /// Cancels whatever ROI is active, returning the vehicle and its camera to flying the plan
    /// itself. Inserted directly rather than armed: unlike an ROI location, a cancel carries no
    /// coordinate to place, so there is nothing for a click on the grid to supply.
    ///     @return true if it was added
    function insertCancelROIItem() {
        if (!canPlaceWaypoints || !missionController) {
            return false
        }
        const insertAt = _insertIndex()
        const countBefore = _visualItemCount()
        missionController.insertCancelROIMissionItem(insertAt, true /* makeCurrentItem */)
        _recordInsertUndo(qsTr("Undo cancel ROI"), insertAt, countBefore)
        return true
    }

    /// Points the nose at a heading and holds it there for the rest of the plan.
    ///
    /// The way an ArduCopter mission actually sets yaw. A waypoint's own yaw parameter does not
    /// reach the aircraft -- the same 15-byte record that drops the acceptance radius drops param4
    /// with it, and QGC's own command tree already removes it for ArduPilot multirotors -- so the
    /// heading has to be its own item.
    ///
    /// Worth more here than on a map. Without GNSS the compass is the only absolute reference the
    /// aircraft has, and the optical flow sensor measures in the airframe's frame: which way the
    /// nose points while a leg is flown is part of what is being measured, not a detail of how it
    /// looks. A pattern flown nose-forward and the same pattern flown nose-fixed are two different
    /// experiments.
    ///     @param headingDegrees clockwise from north, the same convention the whole grid uses
    ///     @return true if it was added
    function insertConditionYaw(headingDegrees) {
        if (!canPlaceWaypoints || !missionController || isNaN(headingDegrees)) {
            return false
        }

        const insertAt = _insertIndex()
        const countBefore = _visualItemCount()

        // Carries no coordinate at all, so it takes the same route a cancelled ROI does: inserted as
        // a plain item and then given its command, since MissionController offers no insertion of
        // its own for it.
        const item = missionController.insertSimpleMissionItem(QtPositioning.coordinate(),
                                                               insertAt, true /* makeCurrentItem */)
        if (!item) {
            return false
        }

        item.command = commandConditionYaw
        setWaypointYawHeading(missionController.currentPlanViewVIIndex, headingDegrees)
        _recordInsertUndo(qsTr("Undo heading"), insertAt, countBefore)
        return true
    }

    /// @return the fact the command tree gave this name on an item, or null when it has none.
    ///
    /// Both lists are searched because which one a parameter lands in is the command tree's own
    /// choice -- Hold is marked advanced and Heading is not -- and that is not something this grid
    /// should encode. Searched by name rather than by position: the lists hold only the parameters
    /// shown for this firmware and vehicle, so an index would point at a different parameter the
    /// moment that set changes.
    function _namedFactOf(item, factName) {
        if (!item) {
            return null
        }
        const lists = [ item.textFieldFacts, item.textFieldFactsAdvanced ]
        for (var i = 0; i < lists.length; i++) {
            const facts = lists[i]
            if (!facts) {
                continue
            }
            for (var j = 0; j < facts.count; j++) {
                const fact = facts.get(j)
                if (fact && (fact.name === factName)) {
                    return fact
                }
            }
        }
        return null
    }

    /// Adds a copy of an item right after it: same command, position, altitude and, when the item
    /// carries one, the same per-leg speed. Not offered for the takeoff, which only a plan's first
    /// item may be, and not for an item with no real coordinate to copy -- a cancelled ROI, which
    /// carries none at all.
    ///     @return true if it was added
    function duplicateItem(index) {
        const item = _visualItemAt(index)
        if (!item || !canPlaceWaypoints || _isPinnedItem(item) || !item.coordinate || !item.coordinate.isValid) {
            return false
        }

        const countBefore = _visualItemCount()
        const command = (item.command !== undefined) ? item.command : commandWaypoint
        var newItem
        if (command === commandLand) {
            newItem = missionController.insertLandItem(item.coordinate, index + 1, true /* makeCurrentItem */)
        } else {
            newItem = missionController.insertSimpleMissionItem(item.coordinate, index + 1, true /* makeCurrentItem */)
            if (newItem) {
                newItem.command = command
            }
        }
        if (!newItem) {
            return false
        }

        if (item.altitude && newItem.altitude) {
            newItem.altitude.rawValue = item.altitude.rawValue
            newItem.altitudeFrame = item.altitudeFrame
        }

        const sourceSpeed = waypointSpeedSection(index)
        const newSpeed = newItem.speedSection
        if (sourceSpeed && newSpeed && newSpeed.available && sourceSpeed.specifyFlightSpeed) {
            newSpeed.flightSpeed.rawValue = sourceSpeed.flightSpeed.rawValue
            newSpeed.specifyFlightSpeed = true
        }

        syncLandingAltitudes()
        _recordInsertUndo(qsTr("Undo duplicate"), index + 1, countBefore)
        return true
    }

    /// Splits the leg leaving this item: adds a waypoint at its midpoint, in grid metres. Building a
    /// pattern one leg at a time means the common edit is turning one side of it into two, and a
    /// waypoint invented at the midpoint of the leg it splits is the only version of that which does
    /// not ask the operator to work out a position for it by hand.
    ///     @return true if it was added
    function insertWaypointBetween(index) {
        if (!canPlaceWaypoints) {
            return false
        }

        const drawn = _drawnMissionPoints()
        for (var i = 0; i < drawn.length; i++) {
            if (drawn[i].index !== index) {
                continue
            }
            // The plan's last flown-through item has no leg after it to split
            if ((i + 1) >= drawn.length) {
                return false
            }
            const next = drawn[i + 1]
            selectWaypoint(index)
            return addWaypointAt((drawn[i].north + next.north) / 2, (drawn[i].east + next.east) / 2)
        }
        return false
    }

    /// @return true when this item has a leg after it for insertWaypointBetween to split. Read by
    /// the row's own footer to decide whether "insert after" is worth offering: the plan's last
    /// flown-through item has nothing past it to split, and a button that fails every time it is
    /// pressed teaches the operator to stop trusting the row.
    function hasLegAfter(index) {
        const drawn = _drawnMissionPoints()
        for (var i = 0; i < drawn.length; i++) {
            if (drawn[i].index === index) {
                return (i + 1) < drawn.length
            }
        }
        return false
    }

    /// Index into the mission's visual items of the waypoint being worked on, or -1 for none
    property int selectedWaypointIndex: -1

    /// @return the visual item at an index, or null when the index no longer names one. Re-fetched
    /// at the moment of use rather than held: deleting or reordering shifts every index after the
    /// change, and a remembered item would be operated on after it had moved or gone.
    function _visualItemAt(index) {
        if (!missionController || (index < 0)) {
            return null
        }
        const items = missionController.visualItems
        if (!items || (index >= items.count)) {
            return null
        }
        return items.get(index)
    }

    /// @return true when an index names an item this grid actually lists -- every item but the
    /// mission settings item at index 0, which _buildMissionPoints steps over. Guards the
    /// controller-follow handler below against ever opening a row for the one item that has none.
    function _isDrawnListIndex(index) {
        const points = missionPoints
        for (var i = 0; i < points.length; i++) {
            if (points[i].index === index) {
                return true
            }
        }
        return false
    }

    // ---- Undo -------------------------------------------------------------------------------
    //
    // One level, and only for actions that commit on a single tap or drag with nothing else
    // guarding them. Clear, Restart, Fly From Here, Download and Upload all go through a
    // confirmation dialog already (LocalGridMissionActions.qml's _confirm) and are deliberately left
    // out: an action that has already asked does not also need taking back.
    //
    // One level rather than a stack because this controller is a mirror of the vehicle -- it rebuilds
    // every item when a plan transaction completes -- so any recorded action is only valid until the
    // next plan arrives. A single entry, dropped aggressively, can be shown to be correct; a stack is
    // a set of claims about a plan that may no longer exist.
    //
    // Entries hold the inverse action rather than a snapshot of the plan. A snapshot built from what
    // this grid understands would quietly drop anything it does not -- a camera section, a complex
    // item, a command that arrived from a file -- and restoring it would destroy that without a word.

    /// The one action that can be taken back, as { label, apply }, or null when there is none
    property var _undoEntry: null

    /// Set while an inverse is running, so the inverse does not record an entry of its own
    property bool _applyingUndo: false

    /// Set while a whole-plan operation is running, so the per-item writes it makes do not each
    /// record an entry over the single one the operation itself recorded
    property bool _batchingUndo: false

    /// True while there is something to take back. The undo control exists only when this is true,
    /// which is also why it costs nothing against the chrome budget: in the default state there is
    /// nothing to undo and no control.
    readonly property bool canUndo: _undoEntry !== null

    /// What would be taken back, said on the control itself. One tap after an accident the operator
    /// does not necessarily know which action was the last one recorded.
    readonly property string undoLabel: _undoEntry ? _undoEntry.label : ""

    function _recordUndo(label, apply) {
        if (_applyingUndo || _batchingUndo) {
            return
        }
        _undoEntry = { label: label, apply: apply }
    }

    function _clearUndo() {
        _undoEntry = null
    }

    /// Takes back the last recorded action.
    ///     @return true if something was taken back
    function undoLastAction() {
        const entry = _undoEntry
        if (!entry) {
            return false
        }

        // Cleared before the inverse runs, not after. The inverse calls the same functions the
        // original action did, and those record entries -- so leaving it in place would have the
        // undo overwrite itself with its own mirror image and offer to undo the undo.
        _undoEntry = null
        _applyingUndo = true
        entry.apply()
        _applyingUndo = false
        return true
    }

    /// Records the removal of whatever a single insert just added, so taking it back leaves the plan
    /// exactly as it was before the tap.
    ///
    /// Counted rather than pointed at, because one tap can add more than one item: placing the first
    /// waypoint of an empty plan puts a takeoff on the origin ahead of it, and an undo that removed
    /// only the waypoint would leave behind a takeoff the operator never asked for.
    ///     @param label what to call the action on the control
    ///     @param firstIndex the insert index the action used
    ///     @param countBefore how many visual items there were before it ran
    function _recordInsertUndo(label, firstIndex, countBefore) {
        const items = missionController ? missionController.visualItems : null
        if (!items) {
            return
        }
        const added = items.count - countBefore
        if (added <= 0) {
            return
        }
        _recordUndo(label, () => {
            // Highest first: removing from the front would shift every index after it, and the
            // second removal would take out the wrong item.
            for (var i = added - 1; i >= 0; i--) {
                if (_visualItemAt(firstIndex + i)) {
                    missionController.removeVisualItem(firstIndex + i)
                }
            }
            clearWaypointSelection()
        })
    }

    /// @return how many visual items the plan holds, including the settings item the grid never draws
    function _visualItemCount() {
        const items = missionController ? missionController.visualItems : null
        return items ? items.count : 0
    }

    /// Everything about an item that this grid is able to put back, or null for one it cannot.
    ///
    /// The boundary is drawn where it can be proved rather than guessed: only the commands this grid
    /// can itself create, and only while the item carries no camera section that would be lost. An
    /// item outside that boundary records no undo entry at all, so deleting it simply offers no
    /// undo -- which is honest, where restoring it as something subtly different would not be.
    function _describeItemForUndo(index) {
        const item = _visualItemAt(index)
        if (!item) {
            return null
        }

        const command = waypointCommand(index)
        const known = [ commandWaypoint, commandLand, commandTakeoff, commandConditionYaw ]
        if (known.indexOf(command) < 0) {
            return null
        }
        if (item.cameraSection && item.cameraSection.available && item.cameraSection.specifyGimbal) {
            return null
        }

        const point = _pointForIndex(index)
        const altitudeFact = waypointAltitudeFact(index)
        const speedSection = waypointSpeedSection(index)
        const holdFact = waypointHoldTimeFact(index)

        return {
            index:      index,
            command:    command,
            north:      point ? point.north : NaN,
            east:       point ? point.east : NaN,
            onGrid:     point ? point.onGrid : false,
            altitude:   altitudeFact ? altitudeFact.rawValue : NaN,
            speed:      (speedSection && speedSection.specifyFlightSpeed) ? speedSection.flightSpeed.rawValue : NaN,
            hold:       holdFact ? holdFact.rawValue : NaN,
            heading:    waypointYawHeading(index)
        }
    }

    /// @return the missionPoints entry for a visual item index, or null
    function _pointForIndex(index) {
        const points = missionPoints
        for (var i = 0; i < points.length; i++) {
            if (points[i].index === index) {
                return points[i]
            }
        }
        return null
    }

    /// Puts back an item taken out, from the description recorded before it went
    function _restoreDescribedItem(described) {
        if (!described || !missionController) {
            return
        }

        // Placed back at the index it came from, so a pattern keeps its order rather than having the
        // restored item reappear on the end of the route
        const insertAt = Math.min(described.index, _visualItemCount())
        var item = null

        if (described.command === commandConditionYaw) {
            item = missionController.insertSimpleMissionItem(QtPositioning.coordinate(), insertAt, true)
            if (item) {
                item.command = commandConditionYaw
                setWaypointYawHeading(insertAt, described.heading)
            }
        } else if (described.command === commandTakeoff) {
            const originCoord = projection.coordinateAt(originCoordinate, 0, 0)
            item = missionController.insertTakeoffItem(originCoord, insertAt, true)
            if (item) {
                item.coordinate = originCoord
            }
        } else {
            const coordinate = described.onGrid
                                ? projection.coordinateAt(originCoordinate, described.north, described.east)
                                : QtPositioning.coordinate()
            item = missionController.insertSimpleMissionItem(coordinate, insertAt, true)
            if (item && (described.command !== commandWaypoint)) {
                item.command = described.command
            }
        }

        if (!item) {
            return
        }

        if (!isNaN(described.altitude) && item.altitude) {
            item.altitude.rawValue = described.altitude
        }
        if (!isNaN(described.speed)) {
            const section = waypointSpeedSection(insertAt)
            if (section) {
                section.flightSpeed.rawValue = described.speed
                section.specifyFlightSpeed = true
            }
        }
        if (!isNaN(described.hold)) {
            const holdFact = waypointHoldTimeFact(insertAt)
            if (holdFact) {
                holdFact.rawValue = described.hold
            }
        }
        syncLandingAltitudes()
    }

    /// Set while clearWaypointSelection is putting the controller's current item back at the end of
    /// the plan on purpose. Without this guard the onPlanViewStateChanged handler below would read
    /// that as a fresh selection and re-open the last row's editor immediately after this function
    /// told it to close -- undoing the very click that asked for the plan to stop being edited.
    property bool _revertingSelectionToEndOfPlan: false

    function selectWaypoint(index) {
        const item = _visualItemAt(index)
        if (!item) {
            return
        }
        selectedWaypointIndex = index
        clickPanel.visible = false
        // Tells the controller where an insert should land: right after whatever the operator is
        // looking at. force is false so an insert's own makeCurrentItem call (which lands on the
        // very item this selects) does not trigger a second, redundant recompute.
        if (missionController) {
            missionController.setCurrentPlanViewSeqNum(item.sequenceNumber, false)
        }
    }

    /// Deselects, and puts the controller back at the end of the plan -- which is where an insert
    /// belongs once nothing in particular is being worked on. This mirrors exactly what
    /// MissionController computes for itself when the fly view first takes a plan
    /// (MissionController.cc:1524-1531): the last item's own last sequence number, or 0 for an empty
    /// plan.
    function clearWaypointSelection() {
        selectedWaypointIndex = -1
        if (!missionController) {
            return
        }
        const items = missionController.visualItems
        const lastItem = (items && (items.count > 0)) ? items.get(items.count - 1) : null
        _revertingSelectionToEndOfPlan = true
        missionController.setCurrentPlanViewSeqNum(lastItem ? lastItem.lastSequenceNumber : 0, true)
        _revertingSelectionToEndOfPlan = false
    }

    /// Keeps selectedWaypointIndex following whichever item the controller just made current --
    /// which is how an insert's makeCurrentItem lands on the item just added. This replaces
    /// _selectNewestItem, which picked the newest item off the end of the plan and so opened the
    /// wrong editor the moment an insert could land anywhere else.
    Connections {
        target:  missionController
        enabled: missionController !== null

        function onPlanViewStateChanged() {
            if (_root._revertingSelectionToEndOfPlan) {
                return
            }
            const viIndex = missionController.currentPlanViewVIIndex
            if ((viIndex === _root.selectedWaypointIndex) || !_root._isDrawnListIndex(viIndex)) {
                return
            }
            _root.selectedWaypointIndex = viIndex
        }

        // A plan that just arrived whole -- downloaded, cleared, loaded from the vehicle -- is not
        // the plan whatever was selected belonged to. Left alone, the same index could now name a
        // completely different item and silently reopen its editor without anything having been
        // clicked.
        function onVisualItemsReset() {
            _root.clearWaypointSelection()
            // The recorded action describes a plan that no longer exists. Applying its inverse to
            // the one that just arrived would edit an item it was never about.
            _root._clearUndo()
        }
    }

    /// @return true if a waypoint was removed
    function removeSelectedWaypoint() {
        const index = selectedWaypointIndex
        if (!_visualItemAt(index)) {
            return false
        }

        // Described before it goes, while there is still something to read. Null for an item this
        // grid cannot put back exactly -- those record nothing, so no undo is offered rather than one
        // that would restore something subtly different.
        const described = _describeItemForUndo(index)

        // Cleared first. Removal renumbers everything after it, so a selection held across the call
        // would name a different waypoint than the one the operator was looking at.
        clearWaypointSelection()
        missionController.removeVisualItem(index)

        if (described) {
            _recordUndo(qsTr("Undo delete"), () => _restoreDescribedItem(described))
        } else {
            _clearUndo()
        }
        return true
    }

    /// Where the leg that reaches a waypoint starts, in grid metres: the waypoint drawn before it,
    /// or the origin for the first one, since that is where the vehicle starts.
    ///
    /// Items with nowhere on the grid to be drawn are stepped over rather than counted as the leg's
    /// start. A takeoff carries no coordinate on ArduPilot, and a leg measured from an item that has
    /// no position is a leg measured from nothing.
    ///     @return { north, east }, or null when the index names no drawn waypoint
    function legStartFor(index) {
        const points = missionPoints
        for (var i = 0; i < points.length; i++) {
            if ((points[i].index !== index) || !points[i].onGrid) {
                continue
            }
            // Walking backward for the nearest item the aircraft actually flies through, not merely
            // the nearest one drawn: an ROI sits on the grid with a real position but is never
            // routed to, and a leg measured from one would describe a turn that is not flown.
            for (var previous = i - 1; previous >= 0; previous--) {
                if (points[previous].flyThrough) {
                    return { north: points[previous].north, east: points[previous].east }
                }
            }
            return { north: 0, east: 0 }
        }
        return null
    }

    /// Moves a waypoint so the leg reaching it runs on the given bearing for the given distance.
    /// A route without a map is built one leg at a time -- "from there, ninety degrees for twenty
    /// metres" -- and each leg is what the vehicle actually flies.
    ///     @return true if it moved
    function moveWaypointToLeg(index, bearingDegrees, distanceMetres) {
        const start = legStartFor(index)
        if (!start || isNaN(bearingDegrees) || isNaN(distanceMetres) || (distanceMetres < 0)) {
            return false
        }

        const radians = bearingDegrees * Math.PI / 180
        return moveWaypointTo(index,
                              start.north + (distanceMetres * Math.cos(radians)),
                              start.east + (distanceMetres * Math.sin(radians)))
    }

    /// Moves a waypoint to a bearing and range from the origin. The same point as the offsets below,
    /// said the way a leg is briefed and flown.
    ///     @return true if it moved
    function moveWaypointToPolar(index, bearingDegrees, rangeMetres) {
        if (isNaN(bearingDegrees) || isNaN(rangeMetres) || (rangeMetres < 0)) {
            return false
        }
        const radians = bearingDegrees * Math.PI / 180
        return moveWaypointTo(index, rangeMetres * Math.cos(radians), rangeMetres * Math.sin(radians))
    }

    /// The altitude fact of a waypoint, or null for an item that has none. Plain waypoints carry
    /// one; a complex item may not, and reaching for it blindly would break the panel on those.
    function waypointAltitudeFact(index) {
        const item = _visualItemAt(index)
        return (item && item.altitude) ? item.altitude : null
    }

    /// The speed a newly placed waypoint is given.
    ///
    /// Written onto the waypoint rather than left to the vehicle's WP_SPD, because on this way of
    /// navigating the cruise speed is not a matter of taste. Above EK3_RNG_USE_SPD the estimator
    /// stops using the rangefinder as its height source, and optical flow is scaled by height -- so
    /// the speed a leg is flown at decides how far the aircraft thinks it has gone. A plan that does
    /// not say its speed is flown at whatever the last person to touch the parameters chose.
    readonly property real defaultWaypointSpeedMetersPerSecond: 1

    /// The speed section of a waypoint, or null for an item that has none.
    ///
    /// ArduPilot carries a per-waypoint speed as a DO_CHANGE_SPEED item flown alongside the
    /// waypoint, and QGC models that as a section on the item rather than as a visual item of its
    /// own -- which is why adding one does not renumber anything on this grid. Only plain waypoints
    /// have one: a takeoff climbs at its own rate and a landing descends at its own.
    function waypointSpeedSection(index) {
        const item = _visualItemAt(index)
        if (!item || !item.speedSection || !item.speedSection.available) {
            return null
        }
        return item.speedSection
    }

    /// The label QGC's command tree gives NAV_WAYPOINT's param1. Matched by name rather than by
    /// position in the list below, because that list is filtered -- only the params the command tree
    /// marks as shown for this firmware and vehicle appear in it, so an index would point at a
    /// different parameter the moment that set changes. A rename upstream makes the field disappear,
    /// which is the safe direction to fail: the alternative is silently editing the wrong parameter.
    readonly property string _holdTimeFactName: "Hold"

    /// How long the aircraft waits on a waypoint before flying on, in seconds, or null for an item
    /// that has no such wait.
    ///
    /// The one NAV_WAYPOINT parameter that survives the trip to an ArduCopter. Its mission records
    /// are 15 bytes and cannot hold both a delay and a radius, so for every non-Plane build the
    /// firmware keeps param1 and discards the rest -- AP_Mission.cpp says so in as many words at the
    /// case that decodes this command. What it keeps is flown: do_nav_wp copies it into
    /// loiter_time_max and verify_nav_wp holds the aircraft there until it runs out.
    ///
    /// Worth having beyond parity with the Plan view: hovering in one place is how drift is measured
    /// without a distance term in it, which is a different experiment from flying a pattern and one
    /// this grid could not express at all until now.
    ///
    /// Offered only on plain waypoints. A takeoff and a landing both use param1 for something else
    /// entirely, and the command tree names it accordingly -- so the name match below finds nothing
    /// on them, which is the answer that keeps the field off items it would not mean anything on.
    function waypointHoldTimeFact(index) {
        if (waypointCommand(index) !== commandWaypoint) {
            return null
        }
        return _namedFactOf(_visualItemAt(index), _holdTimeFactName)
    }

    /// True for an item whose whole job is to point the nose somewhere
    function waypointIsYawCommand(index) {
        return waypointCommand(index) === commandConditionYaw
    }

    /// The heading a yaw item holds, in degrees clockwise from north, or NaN for any other item.
    ///
    /// Read and written through the grid's own field rather than bound straight to the fact, because
    /// QGC's command tree describes this parameter as -180..180 while ArduPilot reads it as 0-360
    /// with zero at north (AP_Mission stores param1 into yaw.angle_deg, and Copter hands it to
    /// set_fixed_yaw_rad unchanged). The grid speaks the aircraft's convention everywhere else, so a
    /// bearing of 270 has to be typeable here without the field calling it out of range.
    function waypointYawHeading(index) {
        const fact = waypointIsYawCommand(index) ? _namedFactOf(_visualItemAt(index), "Heading") : null
        return fact ? fact.rawValue : NaN
    }

    /// @return true if the heading was set
    function setWaypointYawHeading(index, headingDegrees) {
        if (isNaN(headingDegrees) || !waypointIsYawCommand(index)) {
            return false
        }
        const fact = _namedFactOf(_visualItemAt(index), "Heading")
        if (!fact) {
            return false
        }
        // Wrapped rather than refused: 370 means 10, and typing past the wrap is an ordinary thing
        // to do while rotating a pattern
        fact.rawValue = ((headingDegrees % 360) + 360) % 360
        return true
    }

    /// Every waypoint's wait added together, in seconds. Read by the statistics panel, which has to
    /// add this itself: QGC's flight-status calculator has no hold term at all, so a plan with waits
    /// in it would otherwise be reported as taking less time than it takes.
    readonly property real missionHoldSeconds: _sumHoldSeconds()

    function _sumHoldSeconds() {
        var total = 0
        const points = missionPoints
        for (var i = 0; i < points.length; i++) {
            const fact = waypointHoldTimeFact(points[i].index)
            // rawValue is read inside the loop so this binding depends on every hold in the plan,
            // and editing one re-runs the sum -- the same reason _findItemsAboveAltitudeLimit does it
            if (fact && !isNaN(fact.rawValue)) {
                total += fact.rawValue
            }
        }
        return total
    }

    /// How far the aircraft flies to complete this plan, in metres.
    ///
    /// Taken from MissionController's own flight-status calculation rather than measured across the
    /// grid's points, so a plan holding items the grid does not draw still measures correctly. That
    /// calculation runs in the fly view -- it is not one of the passes gated behind !_flyView -- and
    /// it reads each waypoint's own speed, which is exactly what this grid writes onto every
    /// waypoint it places. So the numbers describe the plan that was drawn, not a guess from the
    /// vehicle's parameters.
    readonly property real missionDistanceMetres: _finiteOrZero(missionController ? missionController.missionTotalDistance
                                                                                  : 0)

    /// How long the plan takes, in seconds: the flying, plus every wait added on.
    ///
    /// The calculator has no hold term of its own, so a plan with waits in it reads short by exactly
    /// the sum of them. Added here rather than left out, because a duration that is quietly too
    /// small is worse than one that is missing -- it is the number an operator sizes a battery
    /// against.
    readonly property real missionDurationSeconds: _finiteOrZero(missionController ? missionController.missionTime : 0)
                                                        + missionHoldSeconds

    /// True once the plan holds enough for those two numbers to mean anything
    readonly property bool missionStatsKnown: (missionPoints.length > 0) && (missionDistanceMetres > 0)

    /// @return the number given, or zero for anything that is not one.
    ///
    /// A controller that does not carry a property answers undefined, which is not a number and
    /// which QML refuses to assign to a real -- reporting it on every rebuild. The same guard the
    /// click panel already applies to the insert-validity flags, for the same reason.
    function _finiteOrZero(value) {
        return (typeof value === "number") && isFinite(value) ? value : 0
    }

    /// Gives every placed waypoint the same speed, and makes each of them say so.
    ///
    /// The companion to setAllWaypointAltitudes, and needed for the same reason: flying one pattern
    /// at two speeds to compare them means retyping every waypoint otherwise. It also repairs a plan
    /// arrived from a file whose waypoints carry no speed of their own.
    ///     @return how many waypoints were changed
    function setAllWaypointSpeeds(metersPerSecond) {
        if (isNaN(metersPerSecond) || (metersPerSecond <= 0)) {
            return 0
        }

        // Recorded for the same reason the altitudes are: one tap, every waypoint, and nothing left
        // on screen to read the old values back from
        const previous = []
        const beforePoints = missionPoints
        for (var p = 0; p < beforePoints.length; p++) {
            const beforeSection = waypointSpeedSection(beforePoints[p].index)
            if (beforeSection) {
                previous.push({ index:     beforePoints[p].index,
                                value:     beforeSection.flightSpeed.rawValue,
                                specified: beforeSection.specifyFlightSpeed })
            }
        }

        var changed = 0
        const points = missionPoints
        for (var i = 0; i < points.length; i++) {
            const section = waypointSpeedSection(points[i].index)
            if (section) {
                section.flightSpeed.rawValue = metersPerSecond
                section.specifyFlightSpeed = true
                changed++
            }
        }

        if (changed > 0) {
            _recordUndo(qsTr("Undo speeds"), () => {
                for (var j = 0; j < previous.length; j++) {
                    const restoreSection = waypointSpeedSection(previous[j].index)
                    if (restoreSection) {
                        restoreSection.flightSpeed.rawValue = previous[j].value
                        restoreSection.specifyFlightSpeed = previous[j].specified
                    }
                }
            })
        }
        return changed
    }

    function _applyDefaultSpeed(item) {
        if (!item || !item.speedSection || !item.speedSection.available) {
            return
        }
        item.speedSection.flightSpeed.rawValue = defaultWaypointSpeedMetersPerSecond
        item.speedSection.specifyFlightSpeed = true
    }

    // Written out because MAVLinkEnums exposes no values to QML in this build: moc emits an empty
    // enum list for the generated namespace, so every member of it reads as undefined.
    // LocalGridViewTest pins each number against the MAVLink header.
    readonly property int commandWaypoint:      16  // MAV_CMD_NAV_WAYPOINT
    readonly property int commandLand:          21  // MAV_CMD_NAV_LAND
    readonly property int commandTakeoff:       22  // MAV_CMD_NAV_TAKEOFF
    readonly property int commandConditionYaw: 115  // MAV_CMD_CONDITION_YAW

    /// @return the command of a mission item, or -1 for one that does not carry a settable command
    function waypointCommand(index) {
        const item = _visualItemAt(index)
        return (item && (item.command !== undefined)) ? item.command : -1
    }

    /// @return true when the item at this index puts the aircraft on the ground where it is drawn
    function waypointIsLanding(index) {
        return waypointCommand(index) === commandLand
    }

    /// What to call a mission item in a list of them.
    ///
    /// QGC's own name for the command rather than one built here from the three the type selector
    /// offers. A plan can hold commands the grid cannot create -- one loaded from a file, or written
    /// in the Plan view -- and a row that fell back to "Waypoint" for those would be naming the item
    /// something it is not.
    function waypointCommandName(index) {
        const item = _visualItemAt(index)
        return (item && item.commandName) ? item.commandName : ""
    }

    /// The altitude frame of a waypoint, so the panel can say which datum its number is measured
    /// from rather than showing a bare figure that could mean either
    function waypointAltitudeFrame(index) {
        const item = _visualItemAt(index)
        return (item && (item.altitudeFrame !== undefined)) ? item.altitudeFrame : -1
    }

    /// Gives every placed item the same altitude. A pattern is flown at one height, and comparing
    /// drift at two heights means retyping every waypoint otherwise.
    ///     @return how many items were changed
    function setAllWaypointAltitudes(metres) {
        if (isNaN(metres)) {
            return 0
        }

        // The altitudes as they stand, before one number replaces all of them. This is the widest
        // single tap on the grid, and the values it overwrites are not recoverable from anything
        // still on screen once it has run.
        const previous = []
        const beforePoints = missionPoints
        for (var p = 0; p < beforePoints.length; p++) {
            const beforeFact = waypointAltitudeFact(beforePoints[p].index)
            if (beforeFact) {
                previous.push({ index: beforePoints[p].index, value: beforeFact.rawValue })
            }
        }

        var changed = 0
        const points = missionPoints
        for (var i = 0; i < points.length; i++) {
            const fact = waypointAltitudeFact(points[i].index)
            if (fact) {
                fact.rawValue = metres
                changed++
            }
        }

        if (changed > 0) {
            _recordUndo(qsTr("Undo altitudes"), () => {
                for (var j = 0; j < previous.length; j++) {
                    const restoreFact = waypointAltitudeFact(previous[j].index)
                    if (restoreFact) {
                        restoreFact.rawValue = previous[j].value
                    }
                }
                syncLandingAltitudes()
            })
        }
        return changed
    }

    /// Gives every landing the altitude of the item flown before it.
    ///
    /// A landing's altitude is never flown. ArduPilot's do_land() zeroes the one it is given and
    /// refills it from the vehicle's current altitude, so the aircraft arrives over the landing
    /// point at whatever height the leg before it was flown at and descends from there. Left
    /// carrying its own number the item is a field that changes nothing -- and the plan's profile is
    /// drawn from that number, so the last leg shows a climb or a dive the aircraft will not fly.
    ///
    /// Landings do not pass their altitude on: the one before a landing is what the leg into it is
    /// flown at, so a second item after one is measured from the last waypoint rather than from the
    /// ground.
    ///     @return how many landings were changed
    function syncLandingAltitudes() {
        var changed = 0
        var previousAltitude = NaN
        const points = missionPoints
        for (var i = 0; i < points.length; i++) {
            const index = points[i].index
            const fact = waypointAltitudeFact(index)
            if (!fact) {
                continue
            }
            if (!waypointIsLanding(index)) {
                previousAltitude = fact.rawValue
                continue
            }
            // A landing with nothing before it has no altitude to follow. Left as it is rather than
            // zeroed: the aircraft is on the ground there either way, and rewriting it would be a
            // change the operator did not ask for and cannot see the reason for.
            if (!isNaN(previousAltitude) && (fact.rawValue !== previousAltitude)) {
                fact.rawValue = previousAltitude
                changed++
            }
        }
        return changed
    }

    /// Turns a placed item into a takeoff, a landing or a plain waypoint without deleting and
    /// replacing it -- so the position already typed or dragged into place is kept.
    ///     @return true if the command was changed
    function setWaypointCommand(index, command) {
        const item = _visualItemAt(index)
        if (!item || (item.command === undefined)) {
            return false
        }
        item.command = command
        // Changing the command resets the item's altitude to whatever the new command defaults to,
        // and a landing's is not the operator's to set in the first place
        syncLandingAltitudes()
        return true
    }

    /// Moves a waypoint to a point on the grid, in metres from the origin.
    ///     @return true if it moved
    function moveWaypointTo(index, north, east) {
        const item = _visualItemAt(index)
        if (!item || !originKnown || isNaN(north) || isNaN(east) || _isPinnedItem(item)) {
            return false
        }

        const coordinate = projection.coordinateAt(originCoordinate, north, east)
        if (!coordinate.isValid) {
            return false
        }

        // Read before the write, so undo has somewhere to put it back. Suppressed while a whole-plan
        // operation is running: offsetMission and rotatePlan call this once per item and record a
        // single entry of their own, and per-item entries would overwrite it with the last leg of
        // the loop -- an undo that straightened one waypoint out of a turned pattern.
        const before = _pointForIndex(index)
        if (before && before.onGrid) {
            _recordUndo(qsTr("Undo move"), () => moveWaypointTo(index, before.north, before.east))
        }

        item.coordinate = coordinate
        return true
    }

    /// Moves the whole plan by one offset in metres, keeping its shape.
    ///
    /// The remedy for a drifted frame when the vehicle will not take a correction. The aircraft flies
    /// until its *reported* position reaches each waypoint, so a frame that has slid puts every one of
    /// them out by the same amount and in the same direction -- which means the pattern is still the
    /// right pattern, just in the wrong place, and moving the plan by that offset puts the ground
    /// track back where it was drawn. It changes the plan rather than the aircraft, so it works on
    /// firmware that has no position reset at all.
    ///
    /// The takeoff is left where it is. It is pinned to the origin because a multirotor climbs in
    /// place whatever coordinate is uploaded with it, and moving one drags the planned home position
    /// along with it.
    ///     @return how many items moved
    function offsetMission(northMetres, eastMetres) {
        if (!canPlaceWaypoints || isNaN(northMetres) || isNaN(eastMetres)) {
            return 0
        }

        // One entry for the whole operation, not one per item. The flag stops the per-item writes
        // below from each recording their own and leaving the last leg of the loop as the only thing
        // undo knew about.
        const wasBatching = _batchingUndo
        _batchingUndo = true

        // Walked over a snapshot taken before the first write. missionPoints is a binding on the
        // items' coordinates, so it is rebuilt the moment one of them moves -- and an offset applied
        // to a list that recomputes underneath it would move the second item by the first item's
        // shift as well.
        const points = missionPoints
        var moved = 0
        for (var i = 0; i < points.length; i++) {
            const point = points[i]
            if (point.isPinned || !point.onGrid) {
                continue
            }
            if (moveWaypointTo(point.index, point.north + northMetres, point.east + eastMetres)) {
                moved++
            }
        }

        _batchingUndo = wasBatching
        if (moved > 0) {
            // The exact inverse: shifting back by the negation puts every item on the offsets it
            // came from, with no rounding of its own to accumulate.
            _recordUndo(qsTr("Undo move plan"), () => offsetMission(-northMetres, -eastMetres))
        }
        return moved
    }

    // Read into a property of its own rather than inline below. A binding that reaches for
    // vehicle.armed behind a short-circuit only picks up the dependency on the passes that get that
    // far, so the plan stayed movable after the aircraft armed.
    readonly property bool vehicleArmed: vehicle ? vehicle.armed : false

    /// Where the plan's pattern currently starts, in metres from the origin.
    ///
    /// (0, 0) for a pattern as drawn: the grid lays one out from the origin, which is the point the
    /// aircraft was standing on at the time. reanchorPlanToVehicle moves the pattern and records
    /// where it moved it to, so the next move is the distance the aircraft has covered since rather
    /// than the whole distance from the origin all over again.
    ///
    /// Without this the offer was right exactly once. A third flight moved a pattern that had
    /// already been moved, by the full offset again, and put it twice as far out as the operator
    /// asked for -- in the direction they were least likely to be watching, since the first press
    /// had done exactly what they wanted.
    ///
    /// Kept in settings rather than in this object, because the plan outlives it. A plan moved and
    /// uploaded is on the aircraft; close QGC and open it again and the fly view shows that same
    /// moved plan back from the vehicle, while an anchor held only here would have gone back to the
    /// origin and offered to move it all over again. Stored against the vehicle it describes, so a
    /// different aircraft does not inherit a distance that was measured under this one.
    readonly property real planAnchorNorth: _anchorAppliesToThisVehicle
                                                ? _planAnchorNorthFact.rawValue : 0
    readonly property real planAnchorEast:  _anchorAppliesToThisVehicle
                                                ? _planAnchorEastFact.rawValue  : 0

    readonly property var _flyViewSettings:     QGroundControl.settingsManager.flyViewSettings
    readonly property var _planAnchorIdFact:    _flyViewSettings.localGridPlanAnchorVehicleId
    readonly property var _planAnchorNorthFact: _flyViewSettings.localGridPlanAnchorNorth
    readonly property var _planAnchorEastFact:  _flyViewSettings.localGridPlanAnchorEast

    /// Zero is the stored id for "no anchor", and is not a system id any vehicle carries
    readonly property bool _anchorAppliesToThisVehicle: (vehicle !== null)
                                                            && (_planAnchorIdFact.rawValue !== 0)
                                                            && (_planAnchorIdFact.rawValue === vehicle.id)

    /// Puts the anchor back on the origin, for a plan that is not the one that was moved.
    ///
    /// Called when the plan is cleared, loaded from a file or fetched from the vehicle. Each of
    /// those brings its own coordinates, drawn around the origin of whoever drew them, so what the
    /// last move did to the last plan says nothing about this one.
    function resetPlanAnchor() {
        _planAnchorIdFact.rawValue    = 0
        _planAnchorNorthFact.rawValue = 0
        _planAnchorEastFact.rawValue  = 0
    }

    /// Records where the pattern has just been moved to, against the aircraft it was measured under
    function _storePlanAnchor(north, east) {
        _planAnchorIdFact.rawValue    = vehicle ? vehicle.id : 0
        _planAnchorNorthFact.rawValue = north
        _planAnchorEastFact.rawValue  = east
    }

    /// How far the plan would still move to start from where the aircraft is standing now
    readonly property real reanchorNorthMetres: positionValid ? (vehicleNorth - planAnchorNorth) : NaN
    readonly property real reanchorEastMetres:  positionValid ? (vehicleEast  - planAnchorEast)  : NaN

    /// The shortest move worth making. Under this the pattern already starts where the button would
    /// put it, and the position an estimator reports drifts by more than this while the aircraft
    /// stands still -- so a smaller threshold would offer a move that only shuffles the plan around
    /// inside the noise.
    readonly property real _reanchorMinimumMetres: 0.5

    /// True when the pattern already starts where the aircraft is standing
    readonly property bool planStartsAtVehicle: positionValid
                                                    && (Math.abs(reanchorNorthMetres) < _reanchorMinimumMetres)
                                                    && (Math.abs(reanchorEastMetres) < _reanchorMinimumMetres)

    /// True when there is a pattern here that the grid could move
    readonly property bool _hasMovablePlan: _drawnMissionPoints().length > 0

    /// True when the plan could be moved to start from where the aircraft is standing now
    readonly property bool canReanchorPlan: !vehicleArmed && canPlaceWaypoints && positionValid
                                                && _hasMovablePlan && !planStartsAtVehicle

    /// Why the plan cannot be moved to the aircraft, or an empty string when it can -- and also when
    /// there is no plan at all, since a grid with nothing drawn on it explains itself.
    ///
    /// Said in the panel beside the button. This is a control an operator reaches for after every
    /// flight, and one that goes dead without a reason teaches them the feature is broken rather
    /// than that the aircraft is not ready for it yet.
    readonly property string reanchorBlockedReason: {
        if (canReanchorPlan || !_hasMovablePlan) {
            return ""
        }
        if (!originKnown) {
            return qsTr("The plan can be moved once the estimator has an origin to measure from.")
        }
        if (planSyncInProgress) {
            return qsTr("The plan can be moved once the transfer finishes.")
        }
        if (vehicleArmed) {
            return qsTr("The plan can be moved once the aircraft is disarmed. Moving it under an aircraft already flying it changes where it is going mid-flight.")
        }
        if (!positionValid) {
            return qsTr("The plan can be moved once the aircraft is reporting a position.")
        }
        return qsTr("The plan already starts where the aircraft is standing.")
    }

    /// Moves the whole plan so the pattern starts from where the aircraft is standing now.
    ///
    /// This is what a second flight of the same pattern needs. The plan is held as coordinates, and
    /// those were worked out from the origin -- the point the aircraft was standing on when the
    /// pattern was drawn. After a flight the aircraft is somewhere else, usually at the far end of
    /// the pattern it just flew, and re-flying the plan unchanged sends it back over the same patch
    /// of ground from a start point inside the route rather than at the head of it.
    ///
    /// The remedy operators found for that was to reboot the aircraft: ArduPilot refuses a second
    /// origin, so the only way to move the frame under the plan was to make the estimator take a new
    /// one from scratch. This moves the plan instead, which needs nothing from the firmware, and can
    /// be done between flights without touching the aircraft.
    ///
    /// The takeoff does not move -- it is pinned to the origin because a multirotor climbs in place
    /// whatever coordinate is uploaded with it -- and nothing reaches the vehicle until the plan is
    /// sent.
    ///     @return how many items moved
    function reanchorPlanToVehicle() {
        if (!canReanchorPlan) {
            return 0
        }

        const moved = offsetMission(reanchorNorthMetres, reanchorEastMetres)
        if (moved > 0) {
            // Recorded only when something actually moved. A plan whose every item is pinned or off
            // the grid is unchanged, and an anchor moved anyway would report the next press as
            // unnecessary while the pattern still sat on the origin.
            _storePlanAnchor(vehicleNorth, vehicleEast)
        }
        return moved
    }

    /// True when the pattern could be turned or nudged as it stands.
    ///
    /// The same gate the move to the aircraft answers to, and for the same reasons: nothing is
    /// shaped while a transfer is running, and nothing is shaped under an aircraft that is already
    /// flying it. A reported position is not required though -- these two are measured against the
    /// plan itself rather than against where the aircraft is standing.
    readonly property bool canTransformPlan: !vehicleArmed && canPlaceWaypoints && _hasMovablePlan

    /// Why the pattern cannot be shaped, or an empty string when it can -- and also when there is
    /// nothing drawn, since a grid with no pattern on it explains itself.
    readonly property string transformBlockedReason: {
        if (canTransformPlan || !_hasMovablePlan) {
            return ""
        }
        if (!originKnown) {
            return qsTr("The pattern can be shaped once the estimator has an origin to measure from.")
        }
        if (planSyncInProgress) {
            return qsTr("The pattern can be shaped once the transfer finishes.")
        }
        if (vehicleArmed) {
            return qsTr("The pattern can be shaped once the aircraft is disarmed. Turning it under an aircraft already flying it changes where it is going mid-flight.")
        }
        return ""
    }

    /// Turns the whole pattern clockwise about the point it starts from.
    ///
    /// This is the transform a grid needs and a map does not. Indoors a pattern is flown against
    /// walls, a net or a landing line, and the angle it has to sit at is not one anyone wants to
    /// work out per waypoint. On a map the same job is done by dragging the shape around against
    /// what is underneath it; on a bare grid there is nothing to drag it against.
    ///
    /// Turned about the plan's own anchor rather than about the origin. For a pattern as drawn the
    /// two are the same point. For one already moved to start from where the aircraft is standing
    /// they are not, and turning about the origin would sweep the whole pattern around a point it no
    /// longer has anything to do with -- while the anchor, which is what says where the pattern
    /// starts, would go on describing where it used to start.
    ///
    /// QGC's own rotateMission is deliberately not used. It turns about the planned home position,
    /// which in the fly view is whatever the vehicle last reported -- or nothing at all, in which
    /// case it writes a line to the log and returns, which from the operator's side is a button that
    /// does nothing and says nothing. It also steps over every item that carries no coordinate,
    /// which is exactly the yaw items whose heading has to turn with the pattern.
    ///     @return how many items changed
    function rotatePlan(degreesCW) {
        if (!canTransformPlan || isNaN(degreesCW) || (((degreesCW % 360) + 360) % 360 === 0)) {
            return 0
        }

        // One entry for the whole turn, the same reason offsetMission does it
        const wasBatching = _batchingUndo
        _batchingUndo = true

        const radians = degreesCW * Math.PI / 180
        const cos = Math.cos(radians)
        const sin = Math.sin(radians)
        const pivotNorth = planAnchorNorth
        const pivotEast  = planAnchorEast

        // Walked over a snapshot taken before the first write, the same as offsetMission and for the
        // same reason: missionPoints is a binding on the items' coordinates, so a list that
        // recomputed underneath this loop would turn the second item through the first item's angle
        // as well.
        const points = missionPoints
        var turned = 0
        for (var i = 0; i < points.length; i++) {
            const point = points[i]
            // The takeoff stays where it is. It is pinned to the origin because a multirotor climbs
            // in place whatever coordinate is uploaded with it.
            if (point.isPinned) {
                continue
            }

            // An item with no position still turns -- what turns is the heading it holds. Without
            // this the pattern comes out at the right angle with the nose pointing the old way,
            // which on this aircraft is not a cosmetic difference: the flow sensor measures in the
            // airframe's own frame.
            if (waypointIsYawCommand(point.index)) {
                const heading = waypointYawHeading(point.index)
                if (!isNaN(heading) && setWaypointYawHeading(point.index, heading + degreesCW)) {
                    turned++
                }
                continue
            }

            if (!point.onGrid) {
                continue
            }

            // Clockwise in the grid's own frame, where north is up and east is to the right, so a
            // quarter turn takes a point due north of the pivot to due east of it
            const north = point.north - pivotNorth
            const east  = point.east  - pivotEast
            if (moveWaypointTo(point.index,
                               pivotNorth + (north * cos) - (east * sin),
                               pivotEast  + (east * cos)  + (north * sin))) {
                turned++
            }
        }

        _batchingUndo = wasBatching
        if (turned > 0) {
            // Turned back through the negated angle about the same anchor. Exact for the yaw items,
            // whose heading is set rather than accumulated; for the positions it is a second rotation
            // rather than a stored copy, so a pattern turned and turned back lands within floating
            // point of where it started rather than exactly on it -- far below the metre this grid
            // reads out, and far below what the aircraft flies to.
            _recordUndo(qsTr("Undo turn plan"), () => rotatePlan(-degreesCW))
        }
        return turned
    }

    /// Moves the whole pattern by hand, in metres, keeping its shape and its heading.
    ///
    /// The other half of aligning a pattern to a room: a metre further off the wall, half a metre
    /// clear of the net. Distinct from the two moves that already exist -- the position correction
    /// and the move to the aircraft are both remedies for the frame having shifted under a pattern
    /// that was drawn correctly, and neither is something the operator chose the distance of.
    ///
    /// The anchor moves with it. It records where the pattern starts, and a deliberate move changes
    /// that as surely as an automatic one: left alone, the grid would go on saying the pattern
    /// starts where the aircraft is standing while it sat a metre away from there.
    ///     @return how many items moved
    function nudgePlan(northMetres, eastMetres) {
        if (!canTransformPlan || isNaN(northMetres) || isNaN(eastMetres)) {
            return 0
        }

        const moved = offsetMission(northMetres, eastMetres)
        if (moved > 0) {
            _storePlanAnchor(planAnchorNorth + northMetres, planAnchorEast + eastMetres)
        }
        return moved
    }

    /// Which item of the plan a mission sequence number falls on, counting from the head of the plan.
    ///
    /// The two do not run together. ArduPilot's sequence numbers count the home position and the
    /// DO_CHANGE_SPEED items QGC folds into a waypoint, so a plan of three items runs 1, 2, 4 on the
    /// wire. Answers the last item the number has reached, so a sequence landing on a folded item
    /// reads as the item it belongs to rather than as nothing.
    ///     @return the item's number, or 0 for a sequence that has not reached the first item
    function planItemNumberForSequence(sequence) {
        const points = missionPoints
        var number = 0
        for (var i = 0; i < points.length; i++) {
            if (sequence >= points[i].sequence) {
                number = points[i].number
            }
        }
        return number
    }

    /// Which item of the plan the vehicle would pick up at, counting from the head of it, or 0 when
    /// it would start at the beginning.
    ///
    /// ArduPilot resumes rather than restarts: MIS_RESTART defaults to Resume, so entering Auto
    /// carries on from the item the last flight stopped on. After a flight cut short -- landed by
    /// hand, or switched out of Auto -- the aircraft takes off and then flies to the middle of the
    /// route. This is what says so before the flight rather than during it.
    readonly property int vehicleResumeItemNumber: (vehicleTargetSequence < 0)
                                                        ? 0
                                                        : planItemNumberForSequence(vehicleTargetSequence)

    /// Sends the vehicle back to the head of the plan it is holding, so the next Auto starts there.
    ///
    /// The plan's own first sequence number rather than a literal, because what the firmware counts
    /// differs: ArduPilot keeps the home position at zero and starts the plan at one, and Vehicle
    /// takes the offset off again for firmware that does not.
    ///     @return true when a number was sent
    function restartPlanOnVehicle() {
        const points = missionPoints
        if (!vehicle || vehicleArmed || (points.length === 0)) {
            return false
        }
        vehicle.setCurrentMissionSequence(points[0].sequence)
        return true
    }

    onWidthChanged:  _fitIfUnstarted()
    onHeightChanged: _fitIfUnstarted()
    Component.onCompleted: _fitIfUnstarted()

    on_NorthChanged:        _followAndRepaint()
    on_EastChanged:         _followAndRepaint()
    on_HeadingDegreesChanged:  vehicleCanvas.requestPaint()
    onPositionStaleChanged:    vehicleCanvas.requestPaint()
    onEstimatorSevereChanged:  vehicleCanvas.requestPaint()

    property bool _hasBeenFitted: false

    /// Starts on a box a little larger than the project's own 20 m test pattern, so the first frame
    /// shows a usable amount of ground instead of either a single square or the whole county.
    function _fitIfUnstarted() {
        if (_hasBeenFitted || (width <= 0) || (height <= 0)) {
            return
        }
        transform.zoomToFit(40)
        _hasBeenFitted = true
        _repaintAll()
    }

    function _followAndRepaint() {
        if (followVehicle && positionValid) {
            transform.centreOn(_north, _east)
        }
        // North and east are separate facts, so one LOCAL_POSITION_NED lands as two property
        // changes. Sampling on each of them would record the corner between them -- a point the
        // vehicle never occupied -- turning every diagonal into a staircase and inflating the
        // distance flown towards a Manhattan total. callLater collapses both into one sample.
        Qt.callLater(_sampleTrail)
        vehicleCanvas.requestPaint()
    }

    function _sampleTrail() {
        if (!positionValid) {
            return
        }
        if (trail.addPoint(_north, _east)) {
            vehicleCanvas.requestPaint()
        }
    }

    function _repaintAll() {
        gridCanvas.requestPaint()
        missionCanvas.requestPaint()
        vehicleCanvas.requestPaint()
    }

    function centreOnVehicle() {
        if (positionValid) {
            transform.centreOn(_north, _east)
        }
        followVehicle = true
    }

    function centreOnOrigin() {
        transform.centreOn(0, 0)
        followVehicle = false
    }

    Rectangle {
        anchors.fill: parent
        color:        qgcPal.window
    }

    /// The grid and its labels. Repainted only when the view moves, so telemetry arriving at 10 Hz
    /// does not redraw a screenful of lines and text with it.
    Canvas {
        id:             gridCanvas
        anchors.fill:   parent

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()

            const step = transform.gridStepMetres(_root._targetGridPixels)
            if (!(step > 0)) {
                return
            }

            ctx.font = ScreenTools.smallFontPointSize + "pt sans-serif"
            ctx.textBaseline = "middle"

            // Every fifth line is drawn heavier and labelled, which is what makes squares countable
            // at a glance without labelling every one of them
            const majorEvery = 5

            /// How close to an edge a label may sit before it would be cut off
            const labelEdgeMargin = ScreenTools.defaultFontPixelWidth * 3

            const westEdge = transform.eastForPixelX(0)
            const eastEdge = transform.eastForPixelX(width)
            const southEdge = transform.northForPixelY(height)
            const northEdge = transform.northForPixelY(0)

            var index = Math.floor(westEdge / step)
            for (var east = index * step; east <= eastEdge; east += step, index++) {
                const x = transform.pixelXForEast(east)
                const isMajor = (index % majorEvery) === 0
                _root._drawLine(ctx, x, 0, x, height,
                                east === 0 ? _root._axisColor : (isMajor ? _root._gridMajorColor : _root._gridMinorColor),
                                east === 0 ? 2 : 1)
                // Skipped near the edges, where a centred label would be sliced in half and read as
                // a different number entirely
                if (isMajor && (x > labelEdgeMargin) && (x < (width - labelEdgeMargin))) {
                    ctx.fillStyle = _root._axisColor
                    ctx.textAlign = "center"
                    ctx.fillText(_root._label(east), x, height - (_root._margins * 2))
                }
            }

            index = Math.floor(southEdge / step)
            for (var north = index * step; north <= northEdge; north += step, index++) {
                const y = transform.pixelYForNorth(north)
                const isMajorRow = (index % majorEvery) === 0
                _root._drawLine(ctx, 0, y, width, y,
                                north === 0 ? _root._axisColor : (isMajorRow ? _root._gridMajorColor : _root._gridMinorColor),
                                north === 0 ? 2 : 1)
                if (isMajorRow) {
                    ctx.fillStyle = _root._axisColor
                    ctx.textAlign = "left"
                    ctx.fillText(_root._label(north), _root._margins, y - (_root._margins / 2))
                }
            }
        }
    }

    /// The plan, between the grid and the vehicle so a waypoint never hides the aircraft
    Canvas {
        id:             missionCanvas
        anchors.fill:   parent

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            _root._drawMission(ctx)
        }
    }

    /// The vehicle, redrawn on every position or attitude update
    Canvas {
        id:             vehicleCanvas
        anchors.fill:   parent

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()

            _root._drawTrail(ctx)

            if (!_root.positionValid) {
                return
            }

            const x = transform.pixelXForEast(_root._east)
            const y = transform.pixelYForNorth(_root._north)
            _root._drawVehicle(ctx, x, y, _root._headingDegrees)
        }
    }

    function _label(metres) {
        // Zero is the origin, which is marked rather than numbered
        if (Math.abs(metres) < 1e-6) {
            return ""
        }
        return transform.toDisplay(metres).toFixed(0)
    }

    function _drawLine(ctx, x1, y1, x2, y2, colour, lineWidth) {
        ctx.beginPath()
        ctx.strokeStyle = colour
        ctx.lineWidth = lineWidth
        ctx.moveTo(x1, y1)
        ctx.lineTo(x2, y2)
        ctx.stroke()
    }

    function _drawTrail(ctx) {
        const points = trail.points()
        if (points.length < 1) {
            return
        }

        ctx.beginPath()
        ctx.strokeStyle = qgcPal.colorBlue
        ctx.lineWidth = 2
        ctx.lineJoin = "round"
        ctx.moveTo(transform.pixelXForEast(points[0].east), transform.pixelYForNorth(points[0].north))
        for (var i = 1; i < points.length; i++) {
            ctx.lineTo(transform.pixelXForEast(points[i].east), transform.pixelYForNorth(points[i].north))
        }

        // The trail is sampled by distance, so its newest point lags the aircraft by up to one
        // sample. Closing that gap here keeps the line attached to the vehicle instead of trailing a
        // gap that grows every time the sampling is thinned.
        if (positionValid) {
            ctx.lineTo(transform.pixelXForEast(_east), transform.pixelYForNorth(_north))
        }

        ctx.stroke()
    }

    function _drawMission(ctx) {
        // Only the items with somewhere to be: an item carrying no coordinate has no pixel to run a
        // leg to, and putting one in the path would break the whole route rather than that one leg.
        const points = _drawnMissionPoints()
        if (points.length === 0) {
            return
        }

        // Started at the origin rather than at the first drawn waypoint. The first leg is flown from
        // where the aircraft is standing, legStartFor already measures it from there, and the panel
        // states its bearing and distance -- so leaving it out of the drawing gave a picture that
        // denied a leg the numbers beside it described.
        //
        // It is also the leg most likely to have nothing at its near end to draw from: on ArduPilot
        // a multirotor's takeoff climbs in place and carries no position of its own, so the route
        // appeared to begin in mid-air at the second waypoint.
        ctx.beginPath()
        ctx.strokeStyle = qgcPal.colorOrange
        ctx.lineWidth = 2
        ctx.moveTo(transform.pixelXForEast(0), transform.pixelYForNorth(0))
        for (var i = 0; i < points.length; i++) {
            ctx.lineTo(transform.pixelXForEast(points[i].east), transform.pixelYForNorth(points[i].north))
        }
        ctx.stroke()

        // The markers themselves are items rather than paint, so they can be pointed at. Only the
        // legs between them are drawn here.
    }

    /// A triangle pointing where the nose points. Heading is a compass bearing, so it is turned into
    /// a screen angle here rather than anywhere the reader has to hold both conventions at once.
    function _drawVehicle(ctx, x, y, headingDegrees) {
        const size = ScreenTools.defaultFontPixelHeight * 0.9
        const heading = isNaN(headingDegrees) ? 0 : headingDegrees

        ctx.save()
        ctx.translate(x, y)
        // North is up and bearings run clockwise, which is exactly how canvas rotation runs once the
        // origin is at the vehicle -- so the bearing goes in unmodified.
        ctx.rotate(heading * Math.PI / 180)

        ctx.beginPath()
        ctx.moveTo(0, -size)
        ctx.lineTo(size * 0.6, size * 0.7)
        ctx.lineTo(0, size * 0.35)
        ctx.lineTo(-size * 0.6, size * 0.7)
        ctx.closePath()

        // Hollowed out once the position stops being trustworthy, rather than merely recoloured. A
        // solid marker states a position; an empty outline states where one was last believed to be,
        // which is the only thing still known. The shape is kept so the operator can see it is the
        // same aircraft and where it was last pointing.
        //
        // Outlined in red rather than orange: the mission legs and every waypoint marker on this
        // grid are already orange, and an orange outline among them is a shape the eye has to hunt
        // for. This is the one thing on the grid that must not be missed.
        if (_root.positionStale || _root.estimatorSevere) {
            ctx.strokeStyle = qgcPal.colorRed
            ctx.lineWidth = 2
            ctx.stroke()
        } else {
            ctx.fillStyle = isNaN(headingDegrees) ? qgcPal.colorOrange : qgcPal.colorBlue
            ctx.fill()
            ctx.strokeStyle = qgcPal.text
            ctx.lineWidth = 1
            ctx.stroke()
        }

        ctx.restore()
    }

    // Wheel to zoom, drag to pan. Zooming about the cursor rather than the centre keeps whatever is
    // being examined under the pointer.
    MouseArea {
        id:             dragArea
        anchors.fill:   parent
        acceptedButtons: Qt.LeftButton

        property real _lastX:       0
        property real _lastY:       0
        property bool _hasDragged:  false

        /// Slop before a press counts as a drag rather than a click, so a click that moves a pixel
        /// still places a waypoint and a pan never does.
        ///
        /// Measured against a finger rather than a letter, the same correction the waypoint marker's
        /// own threshold got: a font width is about a millimetre and a half, and a hand holding a
        /// controller at the flight line does not hold still to within that -- so a tap meant to
        /// open the click panel became a pan of the whole grid instead.
        readonly property real _dragThreshold: ScreenTools.minTouchPixels / 2

        /// Where the press that might become a long press went down, and whether it already fired
        property real _pressX:          0
        property real _pressY:          0
        property bool _longPressFired:  false

        onPressed: (mouse) => {
            _lastX = mouse.x
            _lastY = mouse.y
            _pressX = mouse.x
            _pressY = mouse.y
            _hasDragged = false
            _longPressFired = false
            clickPanel.visible = false
            longPressTimer.restart()
        }

        onReleased: longPressTimer.stop()
        onCanceled: longPressTimer.stop()

        onPositionChanged: (mouse) => {
            if (!pressed) {
                return
            }
            const deltaX = mouse.x - _lastX
            const deltaY = mouse.y - _lastY
            if (!_hasDragged && ((Math.abs(deltaX) + Math.abs(deltaY)) < _dragThreshold)) {
                return
            }

            // A press that has started travelling is a pan, not a hold. Stopped rather than left to
            // fire, or panning across the grid would drop a waypoint wherever the finger paused.
            longPressTimer.stop()
            _hasDragged = true
            transform.panByPixels(deltaX, deltaY)
            _lastX = mouse.x
            _lastY = mouse.y
            _root.followVehicle = false
        }

        onClicked: (mouse) => {
            if (_hasDragged || _longPressFired) {
                return
            }
            // Checked before the selection is touched at all. An armed tool places at the point
            // just selected -- that is the whole reason for arming one instead of opening the click
            // panel every time -- and clearing the selection first would turn every placement back
            // into an append, silently, on every click after the first.
            if (_root.armedTool !== "") {
                _root.placeArmedToolAtPixel(mouse.x, mouse.y)
                return
            }
            // A click on bare grid is a click away from whatever waypoint was being worked on
            _root.clearWaypointSelection()
            clickPanel.showAt(mouse.x, mouse.y)
        }

        /// Press and hold to drop a waypoint where the finger is, without going through the panel.
        ///
        /// The gesture a phone offers for "put one here", and the one an operator building a pattern
        /// at the flight line reaches for. It does not replace the click panel: a tap still opens it
        /// and still shows the offsets before anything is committed, which is the careful path. This
        /// is the quick one, and the marker it leaves is selected, so the row that opens carries the
        /// numbers to correct it by.
        Timer {
            id:         longPressTimer
            // Qt's own press-and-hold interval, so the gesture feels like every other one on the
            // platform rather than like a control with its own idea of how long a hold is
            interval:   Application.styleHints.mousePressAndHoldInterval
            onTriggered: {
                if (dragArea._hasDragged || !dragArea.pressed) {
                    return
                }
                dragArea._longPressFired = true
                _root.addWaypointAtPixel(dragArea._pressX, dragArea._pressY)
            }
        }

        onWheel: (wheel) => {
            // A notch is 120 units; one notch is a fifth in or out, which is a comfortable step
            const notches = wheel.angleDelta.y / 120
            transform.zoomBy(Math.pow(0.8, notches), wheel.x, wheel.y)
        }
    }

    PinchArea {
        anchors.fill:   parent
        enabled:        true

        property real _previousScale: 1

        onPinchStarted: { _previousScale = 1 }
        onPinchUpdated: (pinch) => {
            if (pinch.scale <= 0) {
                return
            }
            transform.zoomBy(_previousScale / pinch.scale, pinch.center.x, pinch.center.y)
            _previousScale = pinch.scale
            _root.followVehicle = false
        }
    }

    /// The origin, drawn as an item rather than into the grid canvas so it can be clicked. Kept below
    /// the waypoints: a plan's takeoff is pinned here and has to stay pickable, and the part of this
    /// marker that takes a click is the label beside the ring rather than the ring underneath it.
    LocalGridOriginMarker {
        id:             originMarker
        objectName:     "localGrid_originMarker"
        x:              _root.gridTransform.pixelXForEast(0) - centreX
        y:              _root.gridTransform.pixelYForNorth(0) - centreY
        z:              0
        originKnown:    _root.originKnown

        // The one point on the grid the operator can put the aircraft on by hand and be sure of, so
        // it is offered as the position rather than making them click the exact spot it is drawn at
        onCorrectPositionRequested: _root.showPositionCorrectionDialog(0, 0, qsTr("the origin"))
    }

    /// The waypoints themselves, above the legs drawn on the canvas and above the vehicle's own
    /// canvas so a marker can always be picked up. Positions are bindings on the transform, so they
    /// follow a pan or a zoom without the model being rebuilt.
    Repeater {
        id: waypointRepeater

        // The count rather than the array itself. missionPoints is rebuilt from scratch on every
        // coordinate change, and handing that array over as the model tore down and recreated every
        // delegate with it -- including, mid-drag, the MouseArea holding the grab. The marker was
        // dropped after the first pixel of movement and had to be picked up again for the next one,
        // which is what made dragging on the grid feel nothing like dragging on the map. A plain
        // count only changes when an item is added or removed, so a marker being dragged survives.
        model: _root.missionPoints.length

        LocalGridWaypoint {
            id: waypointMarker

            required property int index

            // Re-read out of the rebuilt array rather than captured, so the delegate follows its
            // waypoint without being replaced. Guarded: the count is applied a beat before the array
            // it came from on the pass where an item is removed.
            readonly property var point: _root.missionPoints[index] ?? null

            /// An item the plan holds but the grid has nowhere to put -- ArduPilot's takeoff, or a
            /// return to launch -- is listed in the panel and left off the grid. Drawing one would
            /// mean inventing a position for it, and a marker standing somewhere the aircraft was
            /// never told to go is worse than no marker at all.
            readonly property bool onGrid: (point !== null) && point.onGrid

            visible:         onGrid
            gridView:        _root
            visualItemIndex: point ? point.index : -1
            sequenceNumber:  point ? point.number : 0
            isVehicleTarget: point ? point.isVehicleTarget : false
            draggable:       point ? !point.isPinned : false
            isSelected:      point ? (_root.selectedWaypointIndex === point.index) : false
            // _root.gridTransform, not the bare id: every Item carries its own `transform` property
            // and it shadows the id inside this delegate, which resolved to a list of graphical
            // transforms and left the markers unplaced.
            x:               onGrid ? (_root.gridTransform.pixelXForEast(point.east) - (width / 2)) : 0
            y:               onGrid ? (_root.gridTransform.pixelYForNorth(point.north) - (height / 2)) : 0
            z:               isSelected ? 2 : 1

            onSelected: _root.selectWaypoint(waypointMarker.visualItemIndex)
            onMovedTo:  (north, east) => _root.moveWaypointTo(waypointMarker.visualItemIndex, north, east)
        }
    }

    // The plan runs down the right edge under the readout, in one column with it. A floating panel
    // showing only the selected item stood here before: it said nothing about the pattern as a
    // whole, so reading a route meant clicking each marker in turn to find out what it was.
    LocalGridMissionList {
        id:                     missionList
        objectName:             "localGrid_missionList"
        anchors.right:          parent.right
        // Follows the airspeed panel when there is one and the readout when there is not, rather
        // than leaving a panel-shaped gap on a vehicle with no pitot fitted.
        anchors.top:            airspeed.visible ? airspeed.bottom : readout.bottom
        anchors.rightMargin:    _root._margins
        anchors.topMargin:      _root._margins
        // Matched to the readout above rather than fixed, so the right edge of the view stays one
        // column of two panels whatever the readout's own contents make it. Held to the same ceiling
        // as the readout on top of that floor, so neither panel alone can widen the column past what
        // the view actually has room for.
        width:                  Math.min(Math.max(ScreenTools.defaultFontPixelWidth * 28, readout.width),
                                         _root._rightColumnMaximumWidth)
        // Anchored at the top and sized to its contents, so a plan of two waypoints gets a panel two
        // rows tall. The limit is what is left down to the bottom edge: past that the rows scroll
        // inside the panel rather than the panel running off the view.
        height:                 implicitHeight
        // Room is reserved for the totals panel below by measuring it rather than by guessing a
        // number tall enough -- the same correction Bagian 1 made to the scale bar. missionStats
        // sizes itself from its own contents and never from this panel, so reading its height here
        // closes no loop.
        maximumHeight:          Math.max(collapsedHeight,
                                         _root.height - y - _root._margins
                                             - _root._inset("bottomEdgeRightInset")
                                             - (missionStats.visible ? missionStats.height + _root._margins : 0))
        z:                      2
        gridView:               _root
    }

    /// What the plan costs to fly, under the plan itself. Last in the right-hand column because it
    /// is read once while a pattern is being built and then not again -- unlike the position above
    /// it, which is read continuously in flight.
    LocalGridMissionStats {
        id:                     missionStats
        objectName:             "localGrid_missionStats"
        anchors.right:          parent.right
        anchors.top:            missionList.bottom
        anchors.rightMargin:    _root._margins
        anchors.topMargin:      visible ? _root._margins : 0
        width:                  missionList.width
        // Folded on a screen too small to carry every panel open at once, the same rule the other
        // three follow. Only the starting value: a deliberate unfold is the operator's and is left
        // alone.
        Component.onCompleted:  collapsed = _root.compact
        z:                      2
        gridView:               _root
    }

    /// What a click on the grid offers. A bare click that added a waypoint outright would turn every
    /// mis-aimed pan into an edit of the plan, and the offsets shown here are the point of placing a
    /// waypoint this way at all -- the operator sees the metres before committing to them.
    LocalGridClickPanel {
        id:                     clickPanel
        gridView:               _root
        z:                      1
        onSetOriginRequested:   _root.showSetOriginDialog()

        onCorrectPositionRequested: (north, east) => _root.showPositionCorrectionDialog(north, east, "")
    }

    /// Built on demand rather than kept alive: it is opened rarely, and once per flight at most.
    function showSetOriginDialog() {
        setOriginDialogFactory.open()
    }

    // The factory rather than a Loader: QGCPopupDialog destroys itself on close, which would leave
    // a Loader holding a dangling item and nothing to open the second time.
    QGCPopupDialogFactory {
        id:                 setOriginDialogFactory
        dialogComponent:    setOriginDialogComponent
    }

    Component {
        id: setOriginDialogComponent

        SetEstimatorOriginDialog {
        }
    }

    /// The global coordinate at a point on the grid, or an invalid one when there is no origin to
    /// measure it from
    function coordinateAtOffsets(north, east) {
        return projection.coordinateAt(originCoordinate, north, east)
    }

    /// Opens the dialog for telling the vehicle it is standing at a point on the grid.
    ///     @param placeName what to call that point, empty when it is only a pair of offsets
    ///
    /// Opened rather than refused while the vehicle is armed. The lock lives in the dialog, next to
    /// the sentence explaining it: an entry point that goes dead in flight teaches the operator that
    /// the feature is broken, where a dialog that opens and says why teaches them when to use it.
    function showPositionCorrectionDialog(north, east, placeName) {
        if (!originKnown) {
            return
        }

        positionCorrectionDialogFactory.open({
            vehicle:    _root.vehicle,
            gridView:   _root,
            north:      north,
            east:       east,
            coordinate: coordinateAtOffsets(north, east),
            placeName:  placeName
        })
    }

    QGCPopupDialogFactory {
        id:                 positionCorrectionDialogFactory
        dialogComponent:    positionCorrectionDialogComponent
    }

    Component {
        id: positionCorrectionDialogComponent

        LocalGridPositionCorrection {
        }
    }

    /// @return the given inset, or 0 when the fly view has not supplied any
    function _inset(name) {
        return toolInsets ? toolInsets[name] : 0
    }

    /// The margins nothing on this view should be placed inside of: the tool strip in the top-left,
    /// and whatever the fly view has anchored to the other three edges -- the virtual joystick or the
    /// guided-action buttons, depending on what is on screen. Exposed so the click panel can keep
    /// itself, and what it opens under a tap near a corner, out from under a button it would then
    /// cover or could not be reached past. The same inset names the standing panels already trust
    /// (leftEdgeBottomInset, bottomEdgeLeftInset) and the same top correction missionActions needed
    /// (topEdgeOffset alongside topEdgeLeftInset -- see the comment there for why).
    readonly property real safeAreaLeft:   _inset("leftEdgeBottomInset")
    readonly property real safeAreaTop:    topEdgeOffset + _inset("topEdgeLeftInset")
    readonly property real safeAreaRight:  _inset("rightEdgeBottomInset")
    readonly property real safeAreaBottom: _inset("bottomEdgeLeftInset")

    /// Takes back the last thing that happened, and exists only while there is something to take
    /// back.
    ///
    /// Standing on its own rather than inside the mission panel, because that panel starts folded on
    /// a small screen and a folded undo is not an undo -- this has to be one tap from the accident
    /// that needs it. Costing nothing when idle is what lets it stand alone: with no recorded action
    /// there is no control, so the chrome budget the responsive tests hold the view to is untouched
    /// in the default state they measure.
    ///
    /// Centred along the bottom, which is the one edge no standing panel of this view claims -- the
    /// scale bar and mission actions are on the left of it, the plan column is up the right.
    QGCButton {
        id:                     undoButton
        objectName:             "localGrid_undoButton"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom:         parent.bottom
        anchors.bottomMargin:   _root._margins + _root._inset("bottomEdgeCenterInset")
        z:                      3
        visible:                _root.canUndo
        text:                   _root.undoLabel
        onClicked:              _root.undoLastAction()
    }

    // Bottom left, the corner the waypoint panel gave up when it moved under the readout. Declared
    // first so missionActions below can sit its bottom margin on this panel's actual measured height
    // rather than on a number guessed to be tall enough -- which stopped being tall enough the moment
    // this panel gained a second row of controls.
    LocalGridScaleBar {
        id:                     scaleBar
        anchors.left:           parent.left
        anchors.bottom:         parent.bottom
        anchors.leftMargin:     _root._margins + _root._inset("leftEdgeBottomInset")
        anchors.bottomMargin:   _root._margins + _root._inset("bottomEdgeLeftInset")
        gridTransform:          transform
    }

    // Sits above the scale bar it is measured from. Its own height is capped rather than left to grow
    // as tall as its content wants: this is the one panel on the grid anchored to the bottom that
    // grows upward, and the tool strip -- anchored to the top of this same left edge -- is what it
    // grows into. topEdgeLeftInset is the tool strip's own bottom edge, published for exactly this: a
    // panel on the same edge knowing where the other one ends.
    //
    // topEdgeOffset is added on top of it for the same reason the readout's topMargin adds it to
    // topEdgeRightInset: the inset is measured in the fly view's own frame, which starts below the
    // toolbar, while this view's frame -- and so this panel's own y -- starts above it. Left out, the
    // tool strip's edge reads a whole toolbar's height higher than it actually sits, and the cap
    // this exists to enforce comes out too generous by exactly that much.
    LocalGridMissionActions {
        objectName:             "localGrid_missionActions"
        anchors.left:           parent.left
        anchors.bottom:         parent.bottom
        anchors.leftMargin:     _root._margins + _root._inset("leftEdgeBottomInset")
        anchors.bottomMargin:   scaleBar.anchors.bottomMargin + scaleBar.height + _root._margins
        height:                 implicitHeight
        maximumHeight:          Math.max(0, (_root.height - anchors.bottomMargin)
                                                - _root.topEdgeOffset - _root._inset("topEdgeLeftInset")
                                                - _root._margins)
        z:                      2
        planMasterController:   _root.planMasterController
        gridView:               _root
    }

    // Pinned to the top right corner, and deliberately not set back by the right edge inset. That
    // inset reserves room for the instrument panel whether or not it is open, which left the readout
    // floating in the middle of the grid with nothing beside it. The left corner is not an option:
    // the tool strip lives there and the non-GPS status panel opens over it, and the operator has
    // that panel open at the same time as this one.
    LocalGridReadout {
        id:                     readout
        objectName:             "localGrid_readout"
        anchors.right:          parent.right
        anchors.top:            parent.top
        // Hugs the right edge, but drops below whatever the fly view has stacked in that corner --
        // the terrain progress bar, or the instrument panel. Only the vertical inset is honoured:
        // the horizontal one reserves the panel's full width whether or not it is showing, which
        // left the readout floating in the middle of the grid.
        anchors.rightMargin:    _root._margins
        anchors.topMargin:      _root.topEdgeOffset + _root._margins + _root._inset("topEdgeRightInset")
        maximumWidth:           _root._rightColumnMaximumWidth
        compactColumns:         _root.compact
        gridView:               _root
        onSetOriginRequested:   _root.showSetOriginDialog()
    }

    /// Between the position numbers and the plan, and only while a pitot is reporting.
    ///
    /// It belongs in this column rather than the instrument panel because it is read against the
    /// numbers directly above it: airspeed beside the ground track this grid draws is what says
    /// whether a leg was flown into wind, and either figure alone says nothing about that.
    LocalGridAirspeed {
        id:                     airspeed
        objectName:             "localGrid_airspeed"
        anchors.right:          parent.right
        anchors.top:            readout.bottom
        anchors.rightMargin:    _root._margins
        anchors.topMargin:      visible ? _root._margins : 0
        width:                  Math.min(Math.max(implicitWidth, readout.width), _root._rightColumnMaximumWidth)
        vehicle:                _root.vehicle
        z:                      2
    }
}
