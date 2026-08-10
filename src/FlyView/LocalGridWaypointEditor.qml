import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

/// What a selected waypoint is, in the terms it will be flown in.
///
/// The same fields the Plan view offers, said in the frame the grid is already in. Adjusting a
/// mission is mostly nudging a leg by a few metres, and leaving the grid to do it means losing sight
/// of the aircraft and the pattern it is flying.
///
/// Every field describes the same point from a different place, and any of them can be typed into:
/// where it sits relative to the origin, and what the leg reaching it looks like from the waypoint
/// before. A route flown without a map is briefed the second way -- "from there, ninety degrees for
/// twenty metres" -- and each leg is what the vehicle actually flies.
///
/// Carries no frame of its own -- no title, no border, no way to dismiss it. Those belong to
/// whatever is showing the item, which is a floating panel today and a row in a list of items next.
ColumnLayout {
    id: _root

    property var gridView: null

    /// Index into the mission's visual items, or -1 when nothing is selected
    property int  visualItemIndex: -1
    property real north:           NaN
    property real east:            NaN

    spacing: ScreenTools.defaultFontPixelHeight / 5

    property real _labelWidth:  ScreenTools.defaultFontPixelWidth * 8
    property real _fieldWidth:  ScreenTools.defaultFontPixelWidth * 15

    /// How wide a paragraph may run before it wraps: the two columns plus the gap between them, so
    /// prose lines up with the fields it is explaining
    readonly property real _textWidth: _labelWidth + _fieldWidth + ScreenTools.defaultFontPixelWidth

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    readonly property var _transform: gridView ? gridView.gridTransform : null
    readonly property var _altitudeFact: (gridView && (visualItemIndex >= 0))
                                            ? gridView.waypointAltitudeFact(visualItemIndex)
                                            : null

    readonly property string _distanceUnits: _transform ? _transform.displayUnits : ""

    readonly property int    _altitudeFrame: (gridView && (visualItemIndex >= 0))
                                                ? gridView.waypointAltitudeFrame(visualItemIndex)
                                                : -1
    readonly property string _altitudeFrameLabel: (_altitudeFrame >= 0)
                                                    ? QGroundControl.altitudeFrameExtraUnits(_altitudeFrame)
                                                    : qsTr("?")

    /// Warned about only while the rangefinder really is the height source, so a vehicle on the
    /// barometer is not told its range matters
    readonly property bool _altitudeAboveLimit: (gridView && _altitudeFact)
                                                    ? gridView.altitudeExceedsLimit(_altitudeFact.rawValue)
                                                    : false
    readonly property string _limitText: (gridView && gridView.altitudeLimitKnown && _transform)
                                            ? (_transform.toDisplay(gridView.altitudeLimitMetres).toFixed(1)
                                               + " " + _distanceUnits)
                                            : qsTr("--")

    /// True for an item anchored where it is -- the takeoff, which sits on the origin because that
    /// is where the aircraft is standing. Its altitude is still the operator's to set.
    readonly property bool _isPinned: (gridView && (visualItemIndex >= 0))
                                        ? gridView.waypointIsPinned(visualItemIndex)
                                        : false

    /// Said out loud when an altitude has just been brought back under the ceiling, so a number
    /// changing under the operator's cursor is explained rather than merely surprising. Cleared when
    /// the selection moves, since it describes one edit to one item.
    property string _clampedNote: ""

    // Held to the ceiling rather than only warned about. Above the rangefinder's range the estimator
    // has no height source at all, so this is not a preference to be overridden -- it is the
    // altitude the vehicle can be flown at.
    Connections {
        target:  _root._altitudeFact
        enabled: _root._altitudeFact !== null

        function onRawValueChanged(value) {
            if (!_root.gridView) {
                return
            }
            const capped = _root.gridView.clampAltitude(value)
            if (capped === value) {
                return
            }
            _root._altitudeFact.rawValue = capped
            _root._clampedNote = qsTr("Held to %1 — the rangefinder only reaches %2.")
                                    .arg(_root._altitudeText(capped))
                                    .arg(_root._limitText)
        }
    }

    function _altitudeText(metres) {
        if (!_transform || isNaN(metres)) {
            return qsTr("--")
        }
        return _transform.toDisplay(metres).toFixed(1) + " " + _distanceUnits
    }

    function _applyAltitudeToAll() {
        if (gridView && _altitudeFact) {
            gridView.setAllWaypointAltitudes(_altitudeFact.rawValue)
        }
    }

    /// atan2 of east over north, rather than the usual y over x, is what turns a maths angle into a
    /// bearing measured clockwise from north
    function _bearingBetween(fromNorth, fromEast, toNorth, toEast) {
        return ((Math.atan2(toEast - fromEast, toNorth - fromNorth) * 180 / Math.PI) + 360) % 360
    }

    function _distanceBetween(fromNorth, fromEast, toNorth, toEast) {
        return Math.sqrt(Math.pow(toNorth - fromNorth, 2) + Math.pow(toEast - fromEast, 2))
    }

    // Refilled whenever the selection changes or the waypoint moves, so dragging a marker updates
    // the numbers and typing a number moves the marker. The field being edited is left alone, or the
    // operator's half-typed value would be overwritten under their cursor.
    onNorthChanged:             _refillFields()
    onEastChanged:              _refillFields()
    Component.onCompleted:      _refillFields()

    onVisualItemIndexChanged: {
        // The note describes one edit to one item, so it goes when the selection does
        _clampedNote = ""
        _refillFields()
    }

    /// Everything shown is derived here rather than from bound properties. A change handler runs
    /// before the bindings that depend on the same value have been recomputed, so reading a derived
    /// property from one leaves the fields a step behind -- which showed as bearing and distance
    /// reading NaN while north and east beside them were already correct.
    function _refillFields() {
        if (!_transform || isNaN(north) || isNaN(east)) {
            return
        }

        if (!northRow.field.activeFocus && !eastRow.field.activeFocus) {
            northRow.field.text = _transform.toDisplay(north).toFixed(1)
            eastRow.field.text = _transform.toDisplay(east).toFixed(1)
        }

        if (!bearingRow.field.activeFocus && !distanceRow.field.activeFocus) {
            bearingRow.field.text = _bearingBetween(0, 0, north, east).toFixed(1)
            distanceRow.field.text = _transform.toDisplay(_distanceBetween(0, 0, north, east)).toFixed(1)
        }

        if (!legBearingRow.field.activeFocus && !legDistanceRow.field.activeFocus) {
            const legStart = (gridView && (visualItemIndex >= 0)) ? gridView.legStartFor(visualItemIndex) : null
            if (legStart) {
                legBearingRow.field.text =
                    _bearingBetween(legStart.north, legStart.east, north, east).toFixed(1)
                legDistanceRow.field.text =
                    _transform.toDisplay(_distanceBetween(legStart.north, legStart.east, north, east)).toFixed(1)
            } else {
                legBearingRow.field.text = ""
                legDistanceRow.field.text = ""
            }
        }
    }

    function _applyOffsets() {
        if (!gridView || !_transform) {
            return
        }
        const newNorth = parseFloat(northRow.field.text)
        const newEast = parseFloat(eastRow.field.text)
        if (isNaN(newNorth) || isNaN(newEast)) {
            _refillFields()
            return
        }
        gridView.moveWaypointTo(visualItemIndex, _transform.fromDisplay(newNorth), _transform.fromDisplay(newEast))
    }

    function _applyPolar() {
        if (!gridView || !_transform) {
            return
        }
        const bearing = parseFloat(bearingRow.field.text)
        const distance = parseFloat(distanceRow.field.text)
        if (isNaN(bearing) || isNaN(distance) || (distance < 0)) {
            _refillFields()
            return
        }
        gridView.moveWaypointToPolar(visualItemIndex, bearing, _transform.fromDisplay(distance))
    }

    function _applyLeg() {
        if (!gridView || !_transform) {
            return
        }
        const bearing = parseFloat(legBearingRow.field.text)
        const distance = parseFloat(legDistanceRow.field.text)
        if (isNaN(bearing) || isNaN(distance) || (distance < 0)) {
            _refillFields()
            return
        }
        gridView.moveWaypointToLeg(visualItemIndex, bearing, _transform.fromDisplay(distance))
    }

    component SectionHeader: QGCLabel {
        Layout.fillWidth:   true
        Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 3
        font.pointSize:     ScreenTools.smallFontPointSize
        font.bold:          true
        color:              qgcPal.text
    }

    /// One labelled entry field, so every row lines up without repeating the widths at each one
    component EntryRow: RowLayout {
        id:         entryRow
        spacing:    ScreenTools.defaultFontPixelWidth

        property string label
        property alias  field: entryField
        property string units
        signal applied()

        QGCLabel {
            Layout.preferredWidth:  _root._labelWidth
            horizontalAlignment:    Text.AlignRight
            font.pointSize:         ScreenTools.smallFontPointSize
            text:                   entryRow.label
        }

        QGCTextField {
            id:                     entryField
            Layout.preferredWidth:  _root._fieldWidth
            font.pointSize:         ScreenTools.smallFontPointSize
            unitsLabel:             entryRow.units
            onEditingFinished:      entryRow.applied()
        }
    }

    // The takeoff's position is not the operator's to set. A multirotor climbs in place whatever
    // coordinate is uploaded with NAV_TAKEOFF, so a takeoff drawn anywhere but where the
    // aircraft is standing would be a picture of a departure it will not fly -- and moving one
    // drags the plan's home position along with it.
    QGCLabel {
        Layout.topMargin:       ScreenTools.defaultFontPixelHeight / 3
        Layout.maximumWidth:    _root._textWidth
        visible:                _root._isPinned
        wrapMode:               Text.WordWrap
        font.pointSize:         ScreenTools.smallFontPointSize
        color:                  qgcPal.colorGrey
        text:                   qsTr("Held on the origin, where the aircraft is standing. Set its altitude below.")
    }

    SectionHeader {
        visible: !_root._isPinned
        text:    qsTr("Position From Origin")
    }

    EntryRow {
        id:         northRow
        visible:    !_root._isPinned
        label:      qsTr("North")
        units:      _root._distanceUnits
        onApplied:  _root._applyOffsets()
    }

    EntryRow {
        id:         eastRow
        visible:    !_root._isPinned
        label:      qsTr("East")
        units:      _root._distanceUnits
        onApplied:  _root._applyOffsets()
    }

    EntryRow {
        id:         bearingRow
        visible:    !_root._isPinned
        label:      qsTr("Bearing")
        units:      "°"
        onApplied:  _root._applyPolar()
    }

    EntryRow {
        id:         distanceRow
        visible:    !_root._isPinned
        label:      qsTr("Distance")
        units:      _root._distanceUnits
        onApplied:  _root._applyPolar()
    }

    SectionHeader {
        visible: !_root._isPinned
        text:    qsTr("Leg From Previous")
    }

    EntryRow {
        id:         legBearingRow
        visible:    !_root._isPinned
        label:      qsTr("Bearing")
        units:      "°"
        onApplied:  _root._applyLeg()
    }

    EntryRow {
        id:         legDistanceRow
        visible:    !_root._isPinned
        label:      qsTr("Distance")
        units:      _root._distanceUnits
        onApplied:  _root._applyLeg()
    }

    // Only for items carrying an altitude of their own. A complex item may not, and the row is
    // dropped rather than shown holding nothing.
    RowLayout {
        Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 3
        visible:            _root._altitudeFact !== null
        spacing:            ScreenTools.defaultFontPixelWidth

        QGCLabel {
            Layout.preferredWidth:  _root._labelWidth
            horizontalAlignment:    Text.AlignRight
            font.pointSize:         ScreenTools.smallFontPointSize
            // Which datum the number is measured from. Without it a bare figure could mean
            // height above the launch point or height above sea level, and on this aircraft the
            // launch point is the estimator origin.
            text:                   qsTr("Alt (%1)").arg(_root._altitudeFrameLabel)
        }

        FactTextField {
            Layout.preferredWidth:  _root._fieldWidth
            font.pointSize:         ScreenTools.smallFontPointSize
            fact:                   _root._altitudeFact
        }
    }

    // Why the number just changed. An altitude corrected without a word looks like a field that
    // did not take what was typed into it.
    QGCLabel {
        Layout.maximumWidth:    _root._textWidth
        visible:                _root._clampedNote !== ""
        wrapMode:               Text.WordWrap
        font.pointSize:         ScreenTools.smallFontPointSize
        color:                  qgcPal.colorOrange
        text:                   _root._clampedNote
    }

    // The failure this catches is silent: the plan uploads cleanly and the aircraft climbs out
    // of the rangefinder's range in flight, taking the estimator's height reference with it.
    // Still reachable: the ceiling is held on what is typed here, but a plan arriving from a
    // file or from the vehicle is not quietly rewritten under the operator.
    QGCLabel {
        Layout.maximumWidth:    _root._textWidth
        visible:                _root._altitudeAboveLimit
        wrapMode:               Text.WordWrap
        font.pointSize:         ScreenTools.smallFontPointSize
        color:                  qgcPal.colorOrange
        text:                   qsTr("Above the rangefinder's %1 range. The estimator takes its height from it, and loses the reference above that.")
                                    .arg(_root._limitText)
    }

    QGCButton {
        objectName:         "localGrid_applyAltitudeToAllButton"
        Layout.fillWidth:   true
        visible:            _root._altitudeFact !== null
        text:               qsTr("Set this altitude on all")
        onClicked:          _root._applyAltitudeToAll()
    }

    QGCLabel {
        Layout.topMargin:       ScreenTools.defaultFontPixelHeight / 3
        Layout.maximumWidth:    _root._textWidth
        wrapMode:               Text.WordWrap
        font.pointSize:         ScreenTools.smallFontPointSize
        visible:                !_root._isPinned
        color:                  qgcPal.colorGrey
        text:                   qsTr("Drag the marker, or type into any field.")
    }
}
