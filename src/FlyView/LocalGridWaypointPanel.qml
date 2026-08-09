import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

/// What a selected waypoint is, in the terms it will be flown in, and what can be done to it.
///
/// The offsets are the point of selecting one at all: a waypoint on this grid is briefed and
/// measured as so many metres north and east of the origin, and until now reading that back meant
/// leaving for the Plan view.
Rectangle {
    id: _root

    property var gridView: null

    /// Index into the mission's visual items, or -1 when nothing is selected
    property int visualItemIndex: -1
    property int sequenceNumber:  0
    property real north:          NaN
    property real east:           NaN

    signal deleteRequested()
    signal closeRequested()

    visible:        visualItemIndex >= 0
    implicitWidth:  layout.implicitWidth + (_margins * 2)
    implicitHeight: layout.implicitHeight + (_margins * 2)
    color:          qgcPal.window
    radius:         ScreenTools.defaultFontPixelHeight / 4
    border.color:   qgcPal.text
    border.width:   1

    property real _margins:     ScreenTools.defaultFontPixelHeight / 3
    property real _fieldWidth:  ScreenTools.defaultFontPixelWidth * 11

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    readonly property var _transform: gridView ? gridView.gridTransform : null
    readonly property var _altitudeFact: (gridView && (visualItemIndex >= 0))
                                            ? gridView.waypointAltitudeFact(visualItemIndex)
                                            : null

    /// Compass bearing and range from the origin, the pair a leg is briefed on
    readonly property real _range:   (isNaN(north) || isNaN(east)) ? NaN : Math.sqrt((north * north) + (east * east))
    readonly property real _bearing: (isNaN(north) || isNaN(east))
                                        ? NaN
                                        : ((Math.atan2(east, north) * 180 / Math.PI) + 360) % 360

    // Refilled whenever the selection changes or the waypoint moves, so dragging a marker updates
    // the numbers and typing a number moves the marker. Skipped for the field being edited, or the
    // operator's half-typed value would be overwritten under their cursor.
    onNorthChanged:             _refillFields()
    onEastChanged:              _refillFields()
    onVisualItemIndexChanged:   _refillFields()
    Component.onCompleted:      _refillFields()

    function _refillFields() {
        if (!_transform || isNaN(north) || isNaN(east)) {
            return
        }
        if (!northField.activeFocus && !eastField.activeFocus) {
            northField.text = _transform.toDisplay(north).toFixed(1)
            eastField.text = _transform.toDisplay(east).toFixed(1)
        }
        if (!bearingField.activeFocus && !distanceField.activeFocus) {
            bearingField.text = _bearing.toFixed(1)
            distanceField.text = _transform.toDisplay(_range).toFixed(1)
        }
    }

    function _applyOffsets() {
        if (!gridView || !_transform) {
            return
        }
        const newNorth = parseFloat(northField.text)
        const newEast = parseFloat(eastField.text)
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
        const bearing = parseFloat(bearingField.text)
        const distance = parseFloat(distanceField.text)
        if (isNaN(bearing) || isNaN(distance) || (distance < 0)) {
            _refillFields()
            return
        }
        gridView.moveWaypointToPolar(visualItemIndex, bearing, _transform.fromDisplay(distance))
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
            text:           qsTr("Waypoint %1").arg(_root.sequenceNumber)
        }

        // The same four fields the Plan view offers, in the frame the grid is already in. Adjusting
        // a mission is mostly nudging a leg by a few metres, and leaving the grid to do it means
        // losing sight of the aircraft and the pattern it is flying.
        GridLayout {
            Layout.fillWidth:   true
            columns:            2
            columnSpacing:      ScreenTools.defaultFontPixelWidth
            rowSpacing:         ScreenTools.defaultFontPixelHeight / 6

            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("North") }
            QGCTextField {
                id:                     northField
                Layout.preferredWidth:  _root._fieldWidth
                font.pointSize:         ScreenTools.smallFontPointSize
                onEditingFinished:      _root._applyOffsets()
            }

            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("East") }
            QGCTextField {
                id:                     eastField
                Layout.preferredWidth:  _root._fieldWidth
                font.pointSize:         ScreenTools.smallFontPointSize
                onEditingFinished:      _root._applyOffsets()
            }

            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Bearing") }
            QGCTextField {
                id:                     bearingField
                Layout.preferredWidth:  _root._fieldWidth
                font.pointSize:         ScreenTools.smallFontPointSize
                onEditingFinished:      _root._applyPolar()
            }

            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Distance") }
            QGCTextField {
                id:                     distanceField
                Layout.preferredWidth:  _root._fieldWidth
                font.pointSize:         ScreenTools.smallFontPointSize
                onEditingFinished:      _root._applyPolar()
            }

            // Only for items that have an altitude of their own. A complex item may not, and the
            // row is dropped rather than shown holding nothing.
            QGCLabel {
                visible:        _root._altitudeFact !== null
                font.pointSize: ScreenTools.smallFontPointSize
                text:           qsTr("Altitude")
            }
            FactTextField {
                Layout.preferredWidth:  _root._fieldWidth
                visible:                _root._altitudeFact !== null
                font.pointSize:         ScreenTools.smallFontPointSize
                fact:                   _root._altitudeFact
            }
        }

        QGCLabel {
            Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 24
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            text:                   qsTr("Drag the marker to move it, or type a position here.")
        }

        RowLayout {
            Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 6
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCButton {
                objectName: "localGrid_deleteWaypointButton"
                text:       qsTr("Delete")
                onClicked:  _root.deleteRequested()
            }

            QGCButton {
                text:       qsTr("Close")
                onClicked:  _root.closeRequested()
            }
        }
    }
}
