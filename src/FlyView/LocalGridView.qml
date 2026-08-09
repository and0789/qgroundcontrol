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
                isCurrent:  item.isCurrentItem
            })
        }
        return points
    }

    onMissionPointsChanged: missionCanvas.requestPaint()

    /// Exposed so the view can be driven from tests and from the surrounding fly view
    readonly property alias gridTransform: transform
    readonly property alias trailPointCount:   trail.pointCount
    readonly property alias trailLengthMetres: trail.pathLengthMetres

    /// A trail from the previous aircraft drawn against this one's origin is a picture of a flight
    /// that never happened
    onVehicleChanged: trail.reset()

    function clearTrail() {
        trail.reset()
        vehicleCanvas.requestPaint()
    }

    /// True when a waypoint placed on the grid would land where it was drawn. Without an origin
    /// there is no mapping between this frame and the coordinates a mission is stored in, and a
    /// waypoint invented from a guessed origin uploads cleanly and flies somewhere else.
    readonly property bool canPlaceWaypoints: originKnown && (missionController !== null)

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

        const coordinate = projection.coordinateAt(originCoordinate, north, east)
        if (!coordinate.isValid) {
            return false
        }

        // -1 appends, which is what clicking past the end of a route means
        switch (kind) {
        case "takeoff":
            missionController.insertTakeoffItem(coordinate, -1, true /* makeCurrentItem */)
            break
        case "land":
            missionController.insertLandItem(coordinate, -1, true /* makeCurrentItem */)
            break
        case "landHere":
            return _insertLandHere(coordinate)
        default:
            missionController.insertSimpleMissionItem(coordinate, -1, true /* makeCurrentItem */)
            break
        }
        return true
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

        // 21 is MAV_CMD_NAV_LAND, written out because MAVLinkEnums exposes no values to QML in this
        // build -- moc emits an empty enum list for the generated namespace, so every member of it
        // reads as undefined. LocalGridViewTest pins the number.
        item.command = 21
        return true
    }

    /// @return true if it was added
    function addWaypointAt(north, east) {
        return addMissionItemAt("waypoint", north, east)
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

    /// Moves a waypoint to a point on the grid, in metres from the origin.
    ///     @return true if it moved
    function moveWaypointTo(index, north, east) {
        const item = _visualItemAt(index)
        if (!item || !originKnown || isNaN(north) || isNaN(east)) {
            return false
        }

        const coordinate = projection.coordinateAt(originCoordinate, north, east)
        if (!coordinate.isValid) {
            return false
        }

        item.coordinate = coordinate
        return true
    }

    onWidthChanged:  _fitIfUnstarted()
    onHeightChanged: _fitIfUnstarted()
    Component.onCompleted: _fitIfUnstarted()

    on_NorthChanged:        _followAndRepaint()
    on_EastChanged:         _followAndRepaint()
    on_HeadingDegreesChanged: vehicleCanvas.requestPaint()

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

    /// The grid, its labels and the origin. Repainted only when the view moves, so telemetry
    /// arriving at 10 Hz does not redraw a screenful of lines and text with it.
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

            _root._drawOrigin(ctx)
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

    function _drawOrigin(ctx) {
        const x = transform.pixelXForEast(0)
        const y = transform.pixelYForNorth(0)
        const radius = ScreenTools.defaultFontPixelHeight / 2

        ctx.beginPath()
        ctx.strokeStyle = qgcPal.colorGreen
        ctx.lineWidth = 2
        ctx.arc(x, y, radius, 0, 2 * Math.PI)
        ctx.stroke()

        ctx.beginPath()
        ctx.fillStyle = qgcPal.colorGreen
        ctx.arc(x, y, radius / 3, 0, 2 * Math.PI)
        ctx.fill()

        ctx.fillStyle = qgcPal.colorGreen
        ctx.textAlign = "left"
        ctx.fillText(qsTr("ORIGIN"), x + (radius * 1.5), y - radius)
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

        if (points.length > 1) {
            ctx.beginPath()
            ctx.strokeStyle = qgcPal.colorOrange
            ctx.lineWidth = 2
            ctx.moveTo(transform.pixelXForEast(points[0].east), transform.pixelYForNorth(points[0].north))
            for (var i = 1; i < points.length; i++) {
                ctx.lineTo(transform.pixelXForEast(points[i].east), transform.pixelYForNorth(points[i].north))
            }
            ctx.stroke()
        }

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

        ctx.fillStyle = isNaN(headingDegrees) ? qgcPal.colorOrange : qgcPal.colorBlue
        ctx.fill()
        ctx.strokeStyle = qgcPal.text
        ctx.lineWidth = 1
        ctx.stroke()

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

    /// The waypoints themselves, above the legs drawn on the canvas and above the vehicle's own
    /// canvas so a marker can always be picked up. Positions are bindings on the transform, so they
    /// follow a pan or a zoom without the model being rebuilt.
    Repeater {
        id:     waypointRepeater
        model:  _root.missionPoints

        LocalGridWaypoint {
            required property var modelData

            gridView:        _root
            visualItemIndex: modelData.index
            sequenceNumber:  modelData.sequence
            isCurrentItem:   modelData.isCurrent
            isSelected:      _root.selectedWaypointIndex === modelData.index
            // _root.gridTransform, not the bare id: every Item carries its own `transform` property
            // and it shadows the id inside this delegate, which resolved to a list of graphical
            // transforms and left the markers unplaced.
            x:               _root.gridTransform.pixelXForEast(modelData.east) - (width / 2)
            y:               _root.gridTransform.pixelYForNorth(modelData.north) - (height / 2)
            z:               isSelected ? 2 : 1

            onSelected:             _root.selectWaypoint(modelData.index)
            onMovedTo:              (north, east) => _root.moveWaypointTo(modelData.index, north, east)
        }
    }

    // Directly under the local position readout, sharing its edge. The bottom left corner it used to
    // occupy is where the non-GPS status panel runs down the screen, and the two were landing on top
    // of each other. Keeping both readouts in one column also means one place to look.
    LocalGridWaypointPanel {
        id:                 waypointPanel
        anchors.right:      readout.right
        anchors.top:        readout.bottom
        anchors.topMargin:  _root._margins
        z:                  2
        gridView:           _root

        visualItemIndex:    _root.selectedWaypointIndex
        sequenceNumber:     _root._selectedPoint ? _root._selectedPoint.sequence : 0
        north:              _root._selectedPoint ? _root._selectedPoint.north : NaN
        east:               _root._selectedPoint ? _root._selectedPoint.east : NaN

        onDeleteRequested:  _root.removeSelectedWaypoint()
        onCloseRequested:   _root.clearWaypointSelection()
    }

    /// The selected entry out of missionPoints, so the panel follows a waypoint that is being dragged
    readonly property var _selectedPoint: {
        const points = missionPoints
        for (var i = 0; i < points.length; i++) {
            if (points[i].index === selectedWaypointIndex) {
                return points[i]
            }
        }
        return null
    }

    /// What a click on the grid offers. A bare click that added a waypoint outright would turn every
    /// mis-aimed pan into an edit of the plan, and the offsets shown here are the point of placing a
    /// waypoint this way at all -- the operator sees the metres before committing to them.
    LocalGridClickPanel {
        id:                     clickPanel
        gridView:               _root
        z:                      1
        onSetOriginRequested:   _root.showSetOriginDialog()
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

    /// @return the given inset, or 0 when the fly view has not supplied any
    function _inset(name) {
        return toolInsets ? toolInsets[name] : 0
    }

    // Top centre rather than a corner. The corners of the fly view are all spoken for -- tool strip,
    // instrument panel, telemetry bar -- and their insets are wide enough that honouring them would
    // push a rose this size into the middle of the grid, on top of the aircraft it is describing.
    LocalGridCompassRose {
        id:                         compassRose
        anchors.horizontalCenter:   parent.horizontalCenter
        anchors.top:                parent.top
        // Doubled so the ring of cardinal labels clears the toolbar rather than starting against it
        anchors.topMargin:          _root.topEdgeOffset + _root._margins + _root._inset("topEdgeCenterInset")
        headingDegrees:             _root._headingDegrees
        diameter:                   ScreenTools.defaultFontPixelHeight * 8
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
