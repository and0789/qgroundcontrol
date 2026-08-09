import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

/// What a selected waypoint is, in the terms it will be flown in, and what can be done to it.
///
/// The same fields the Plan view offers, said in the frame the grid is already in. Adjusting a
/// mission is mostly nudging a leg by a few metres, and leaving the grid to do it means losing sight
/// of the aircraft and the pattern it is flying.
///
/// Every field describes the same point from a different place, and any of them can be typed into:
/// where it sits relative to the origin, and what the leg reaching it looks like from the waypoint
/// before. A route flown without a map is briefed the second way -- "from there, ninety degrees for
/// twenty metres" -- and each leg is what the vehicle actually flies.
Rectangle {
    id: _root

    property var gridView: null

    /// Index into the mission's visual items, or -1 when nothing is selected
    property int  visualItemIndex: -1
    property int  sequenceNumber:  0
    property real north:           NaN
    property real east:            NaN

    signal deleteRequested()
    signal closeRequested()

    visible:        visualItemIndex >= 0
    implicitWidth:  layout.implicitWidth + (_margins * 2)
    implicitHeight: layout.implicitHeight + (_margins * 2)
    color:          qgcPal.window
    radius:         ScreenTools.defaultFontPixelHeight / 4
    border.color:   qgcPal.text
    border.width:   1

    property real _margins:     ScreenTools.defaultFontPixelHeight / 2
    property real _labelWidth:  ScreenTools.defaultFontPixelWidth * 8
    property real _fieldWidth:  ScreenTools.defaultFontPixelWidth * 15

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    readonly property var _transform: gridView ? gridView.gridTransform : null
    readonly property var _altitudeFact: (gridView && (visualItemIndex >= 0))
                                            ? gridView.waypointAltitudeFact(visualItemIndex)
                                            : null

    readonly property string _distanceUnits: _transform ? _transform.displayUnits : ""

    /// The item's current command, or -1 for one whose type cannot be changed
    readonly property int  _command:      (gridView && (visualItemIndex >= 0)) ? gridView.waypointCommand(visualItemIndex) : -1
    readonly property bool _commandKnown: _command >= 0

    /// Position in the type list, so the box shows what the item already is rather than always
    /// reading "Waypoint" and inviting a change nobody asked for
    function _typeIndexFor(command) {
        if (!gridView) {
            return 0
        }
        switch (command) {
        case gridView.commandTakeoff:   return 1
        case gridView.commandLand:      return 2
        default:                        return 0
        }
    }

    function _commandForTypeIndex(index) {
        if (!gridView) {
            return -1
        }
        switch (index) {
        case 1:     return gridView.commandTakeoff
        case 2:     return gridView.commandLand
        default:    return gridView.commandWaypoint
        }
    }

    function _applyType(index) {
        const command = _commandForTypeIndex(index)
        if ((command >= 0) && gridView) {
            gridView.setWaypointCommand(visualItemIndex, command)
        }
    }

    on_CommandChanged: typeCombo.currentIndex = _typeIndexFor(_command)

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
    onVisualItemIndexChanged:   _refillFields()
    Component.onCompleted:      _refillFields()

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

    ColumnLayout {
        id:                 layout
        anchors.margins:    _root._margins
        anchors.left:       parent.left
        anchors.top:        parent.top
        spacing:            ScreenTools.defaultFontPixelHeight / 5

        QGCLabel {
            font.bold:  true
            text:       qsTr("Item %1").arg(_root.sequenceNumber)
        }

        // Changing the type in place rather than deleting and re-adding, so the position already
        // dragged or typed into place survives the change. Turning the last waypoint of a pattern
        // into a landing is the common edit, and rebuilding it from scratch to do that loses the
        // offsets that were the point of placing it.
        RowLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCLabel {
                Layout.preferredWidth:  _root._labelWidth
                horizontalAlignment:    Text.AlignRight
                font.pointSize:         ScreenTools.smallFontPointSize
                text:                   qsTr("Type")
            }

            QGCComboBox {
                id:                     typeCombo
                Layout.preferredWidth:  _root._fieldWidth
                font.pointSize:         ScreenTools.smallFontPointSize
                enabled:                _root._commandKnown
                model:                  [ qsTr("Waypoint"), qsTr("Takeoff"), qsTr("Land") ]

                onActivated: (index) => _root._applyType(index)
            }
        }

        SectionHeader { text: qsTr("Position From Origin") }

        EntryRow {
            id:         northRow
            label:      qsTr("North")
            units:      _root._distanceUnits
            onApplied:  _root._applyOffsets()
        }

        EntryRow {
            id:         eastRow
            label:      qsTr("East")
            units:      _root._distanceUnits
            onApplied:  _root._applyOffsets()
        }

        EntryRow {
            id:         bearingRow
            label:      qsTr("Bearing")
            units:      "°"
            onApplied:  _root._applyPolar()
        }

        EntryRow {
            id:         distanceRow
            label:      qsTr("Distance")
            units:      _root._distanceUnits
            onApplied:  _root._applyPolar()
        }

        SectionHeader { text: qsTr("Leg From Previous") }

        EntryRow {
            id:         legBearingRow
            label:      qsTr("Bearing")
            units:      "°"
            onApplied:  _root._applyLeg()
        }

        EntryRow {
            id:         legDistanceRow
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
                text:                   qsTr("Altitude")
            }

            FactTextField {
                Layout.preferredWidth:  _root._fieldWidth
                font.pointSize:         ScreenTools.smallFontPointSize
                fact:                   _root._altitudeFact
            }
        }

        QGCLabel {
            Layout.topMargin:       ScreenTools.defaultFontPixelHeight / 3
            Layout.maximumWidth:    _root._labelWidth + _root._fieldWidth + ScreenTools.defaultFontPixelWidth
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.colorGrey
            text:                   qsTr("Drag the marker, or type into any field.")
        }

        RowLayout {
            Layout.fillWidth:   true
            Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 4
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCButton {
                objectName:         "localGrid_deleteWaypointButton"
                Layout.fillWidth:   true
                text:               qsTr("Delete")
                onClicked:          _root.deleteRequested()
            }

            QGCButton {
                Layout.fillWidth:   true
                text:               qsTr("Close")
                onClicked:          _root.closeRequested()
            }
        }
    }
}
