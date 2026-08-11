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

    /// Sequence numbers of every item in the plan whose altitude climbs past that ceiling.
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
                offenders.push(points[i].sequence)
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

    /// Walks the plan and places each item that has a coordinate on the grid. Reading
    /// missionController.visualItems.count and the origin here is deliberate: both are what this
    /// depends on, and touching them makes the binding re-run when a waypoint is added or the
    /// vehicle's origin changes.
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
            if (!item || !item.specifiesCoordinate || !item.coordinate.isValid) {
                continue
            }
            // The plan's first visual item is the mission settings, which carries the planned home
            // position as its coordinate. Drawn as a waypoint it lands on top of the origin marker
            // and, worse, makes a plan that has just been cleared look like it already holds a
            // route -- which is what stopped a new plan from starting with a takeoff.
            if (item.homePosition === true) {
                continue
            }
            const offsets = projection.northEastFrom(originCoordinate, item.coordinate)
            if (!offsets) {
                continue
            }
            points.push({
                north:      offsets.north,
                east:       offsets.east,
                // The index into visualItems, which is what removal and reordering take. It is not
                // the sequence number on the marker's face: the two diverge as soon as the plan
                // holds anything that is not a plain waypoint.
                index:      i,
                sequence:   item.sequenceNumber,
                // Compared against the vehicle's own mission index rather than reading the item's
                // isCurrentItem. Both are written in the fly view: the vehicle advancing sets it,
                // but so does inserting an item, so a freshly placed waypoint marked itself as the
                // one being flown to while the aircraft was still standing on the origin.
                isVehicleTarget: (_root.vehicleTargetSequence >= 0)
                                    && (item.sequenceNumber === _root.vehicleTargetSequence),
                // Takeoffs are drawn but not dragged: this one is anchored to the origin
                isPinned:   _isPinnedItem(item)
            })
        }
        return points
    }

    /// Whether the estimator's frame has slid away from the ground while the aircraft sat on it
    property LocalGridOriginDrift originDrift: LocalGridOriginDrift {
        vehicle: _root.vehicle
    }

    readonly property bool   positionDrifting:    originDrift.drifting
    readonly property string positionDriftWarning: originDrift.warning
    readonly property real   positionDriftMetres:  originDrift.driftMetres

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

    /// Adds a mission item at a point on the grid, in metres from the origin.
    ///     @param kind one of "waypoint", "takeoff", "land"
    ///     @return true if it was added
    ///
    /// The same three calls the Plan view's insert strip makes, so an item added here is the same
    /// item as one added there -- including that a multirotor's "land" is a return to launch, which
    /// is what the Plan view inserts and labels that way.
    function addMissionItemAt(kind, north, east) {
        if (!canPlaceWaypoints) {
            return false
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
        // The plan being empty is the only case, so nothing already built is reinterpreted.
        if (_planIsEmpty()) {
            insertTakeoffAtOrigin()
        }

        // -1 appends, which is what clicking past the end of a route means
        switch (kind) {
        case "land":
            _applyDefaultAltitude(missionController.insertLandItem(coordinate, -1, true /* makeCurrentItem */))
            break
        case "landHere":
            return _insertLandHere(coordinate)
        default:
            _applyDefaultAltitude(missionController.insertSimpleMissionItem(coordinate, -1, true /* makeCurrentItem */))
            break
        }

        _selectNewestItem()
        return true
    }

    /// Puts the takeoff on the origin, which is the point the aircraft is standing on.
    ///     @return true if it was added
    function insertTakeoffAtOrigin() {
        if (!canPlaceWaypoints || !_takeoffAllowed()) {
            return false
        }

        const coordinate = projection.coordinateAt(originCoordinate, 0, 0)
        if (!coordinate.isValid) {
            return false
        }

        const item = missionController.insertTakeoffItem(coordinate, -1, true /* makeCurrentItem */)
        if (!item) {
            return false
        }

        // Placed again after the insertion on purpose. insertTakeoffItem ignores the coordinate it
        // is handed and puts the item on the plan's home position -- where the vehicle reported it
        // launched from, which is not necessarily where the estimator is counting from. On this grid
        // the origin is the only point a distance can be measured against.
        item.coordinate = coordinate

        _applyDefaultAltitude(item)
        _selectNewestItem()
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

    /// Lands the vehicle where it is standing on the grid, rather than flying it home first.
    ///
    /// QGC's own insert strip has no way to say this on a multirotor: its landing button produces a
    /// return to launch. That is the right ending for a mission that starts and finishes in the same
    /// place, and the wrong one for a pattern meant to finish at its far corner.
    ///     @return true if it was added
    function _insertLandHere(coordinate) {
        const item = missionController.insertSimpleMissionItem(coordinate, -1, true /* makeCurrentItem */)
        if (!item) {
            return false
        }

        item.command = commandLand
        _applyDefaultAltitude(item)
        _selectNewestItem()
        return true
    }

    /// @return true if it was added
    function addWaypointAt(north, east) {
        return addMissionItemAt("waypoint", north, east)
    }

    /// True when the plan holds nothing that has been placed on the ground yet. Mission settings and
    /// other item types that carry no coordinate are not part of a route.
    function _planIsEmpty() {
        return missionPoints.length === 0
    }

    function _takeoffAllowed() {
        return missionController.isInsertTakeoffValid === true
    }

    /// Selects whatever the plan just gained, so its altitude can be set straight away rather than
    /// found afterwards. The item is appended, so it is the last one drawn.
    function _selectNewestItem() {
        const points = missionPoints
        if (points.length > 0) {
            selectWaypoint(points[points.length - 1].index)
        }
    }

    /// Adds a waypoint under a point on screen, which is what a click on the grid means
    function addWaypointAtPixel(x, y) {
        return addWaypointAt(transform.northForPixelY(y), transform.eastForPixelX(x))
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

    function selectWaypoint(index) {
        if (!_visualItemAt(index)) {
            return
        }
        selectedWaypointIndex = index
        clickPanel.visible = false
    }

    function clearWaypointSelection() {
        selectedWaypointIndex = -1
    }

    /// @return true if a waypoint was removed
    function removeSelectedWaypoint() {
        const index = selectedWaypointIndex
        if (!_visualItemAt(index)) {
            return false
        }

        // Cleared first. Removal renumbers everything after it, so a selection held across the call
        // would name a different waypoint than the one the operator was looking at.
        clearWaypointSelection()
        missionController.removeVisualItem(index)
        return true
    }

    /// Where the leg that reaches a waypoint starts, in grid metres: the waypoint drawn before it,
    /// or the origin for the first one, since that is where the vehicle starts.
    ///     @return { north, east }, or null when the index names no drawn waypoint
    function legStartFor(index) {
        const points = missionPoints
        for (var i = 0; i < points.length; i++) {
            if (points[i].index !== index) {
                continue
            }
            return (i > 0)
                ? { north: points[i - 1].north, east: points[i - 1].east }
                : { north: 0, east: 0 }
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

    // Written out because MAVLinkEnums exposes no values to QML in this build: moc emits an empty
    // enum list for the generated namespace, so every member of it reads as undefined.
    // LocalGridViewTest pins each number against the MAVLink header.
    readonly property int commandWaypoint:  16  // MAV_CMD_NAV_WAYPOINT
    readonly property int commandLand:      21  // MAV_CMD_NAV_LAND
    readonly property int commandTakeoff:   22  // MAV_CMD_NAV_TAKEOFF

    /// @return the command of a mission item, or -1 for one that does not carry a settable command
    function waypointCommand(index) {
        const item = _visualItemAt(index)
        return (item && (item.command !== undefined)) ? item.command : -1
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

        var changed = 0
        const points = missionPoints
        for (var i = 0; i < points.length; i++) {
            const fact = waypointAltitudeFact(points[i].index)
            if (fact) {
                fact.rawValue = metres
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

        // Walked over a snapshot taken before the first write. missionPoints is a binding on the
        // items' coordinates, so it is rebuilt the moment one of them moves -- and an offset applied
        // to a list that recomputes underneath it would move the second item by the first item's
        // shift as well.
        const points = missionPoints
        var moved = 0
        for (var i = 0; i < points.length; i++) {
            const point = points[i]
            if (point.isPinned) {
                continue
            }
            if (moveWaypointTo(point.index, point.north + northMetres, point.east + eastMetres)) {
                moved++
            }
        }
        return moved
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
        const points = missionPoints
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
        /// still places a waypoint and a pan never does
        readonly property real _dragThreshold: ScreenTools.defaultFontPixelWidth

        onPressed: (mouse) => {
            _lastX = mouse.x
            _lastY = mouse.y
            _hasDragged = false
            clickPanel.visible = false
        }

        onPositionChanged: (mouse) => {
            if (!pressed) {
                return
            }
            const deltaX = mouse.x - _lastX
            const deltaY = mouse.y - _lastY
            if (!_hasDragged && ((Math.abs(deltaX) + Math.abs(deltaY)) < _dragThreshold)) {
                return
            }

            _hasDragged = true
            transform.panByPixels(deltaX, deltaY)
            _lastX = mouse.x
            _lastY = mouse.y
            _root.followVehicle = false
        }

        onClicked: (mouse) => {
            if (_hasDragged) {
                return
            }
            // A click on bare grid is a click away from whatever waypoint was being worked on
            _root.clearWaypointSelection()
            clickPanel.showAt(mouse.x, mouse.y)
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

            visible:         point !== null
            gridView:        _root
            visualItemIndex: point ? point.index : -1
            sequenceNumber:  point ? point.sequence : 0
            isVehicleTarget: point ? point.isVehicleTarget : false
            draggable:       point ? !point.isPinned : false
            isSelected:      point ? (_root.selectedWaypointIndex === point.index) : false
            // _root.gridTransform, not the bare id: every Item carries its own `transform` property
            // and it shadows the id inside this delegate, which resolved to a list of graphical
            // transforms and left the markers unplaced.
            x:               point ? (_root.gridTransform.pixelXForEast(point.east) - (width / 2)) : 0
            y:               point ? (_root.gridTransform.pixelYForNorth(point.north) - (height / 2)) : 0
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
        anchors.top:            readout.bottom
        anchors.rightMargin:    _root._margins
        anchors.topMargin:      _root._margins
        // Matched to the readout above rather than fixed, so the right edge of the view stays one
        // column of two panels whatever the readout's own contents make it
        width:                  Math.max(ScreenTools.defaultFontPixelWidth * 28, readout.width)
        // Anchored at the top and sized to its contents, so a plan of two waypoints gets a panel two
        // rows tall. The limit is what is left down to the bottom edge: past that the rows scroll
        // inside the panel rather than the panel running off the view.
        height:                 implicitHeight
        maximumHeight:          Math.max(collapsedHeight,
                                         _root.height - y - _root._margins
                                             - _root._inset("bottomEdgeRightInset"))
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

    // Bottom left, the corner the waypoint panel gave up when it moved under the readout. It sits
    // above the scale bar, which owns the very corner.
    LocalGridMissionActions {
        anchors.left:           parent.left
        anchors.bottom:         parent.bottom
        anchors.leftMargin:     _root._margins + _root._inset("leftEdgeBottomInset")
        anchors.bottomMargin:   _root._margins + (ScreenTools.defaultFontPixelHeight * 2.5)
        z:                      2
        planMasterController:   _root.planMasterController
        gridView:               _root
    }

    LocalGridScaleBar {
        anchors.left:           parent.left
        anchors.bottom:         parent.bottom
        anchors.leftMargin:     _root._margins + _root._inset("leftEdgeBottomInset")
        anchors.bottomMargin:   _root._margins + _root._inset("bottomEdgeLeftInset")
        gridTransform:          transform
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
        gridView:               _root
        onSetOriginRequested:   _root.showSetOriginDialog()
    }
}
