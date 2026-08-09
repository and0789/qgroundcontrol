import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Says in numbers what the grid says in a picture: where the vehicle is, and how far and on what
/// bearing that is from the origin.
///
/// The bearing and range pair is the one a return leg is flown on, and reading it off a picture by
/// eye is exactly the estimate this whole way of flying is trying to replace.
Rectangle {
    id: _root

    property var gridView: null

    /// Raised when the operator asks to set an origin, handled by the view that owns this readout
    signal setOriginRequested()

    implicitWidth:  layout.implicitWidth + (_margins * 2)
    implicitHeight: layout.implicitHeight + (_margins * 2)
    color:          qgcPal.window
    opacity:        0.8
    radius:         ScreenTools.defaultFontPixelHeight / 4

    property real _margins: ScreenTools.defaultFontPixelHeight / 3

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    readonly property var  _transform: gridView ? gridView.gridTransform : null
    readonly property bool _valid:     gridView ? gridView.positionValid : false
    readonly property real _north:     gridView ? gridView.vehicleNorth : NaN
    readonly property real _east:      gridView ? gridView.vehicleEast : NaN

    readonly property real _range:   _valid ? Math.sqrt((_north * _north) + (_east * _east)) : NaN
    /// Compass bearing from the origin to the vehicle. atan2 takes east over north, not the usual
    /// y over x, which is what turns a maths angle into a bearing measured clockwise from north.
    readonly property real _bearing: _valid ? ((Math.atan2(_east, _north) * 180 / Math.PI) + 360) % 360 : NaN

    function _distanceText(metres) {
        if (!_transform || isNaN(metres)) {
            return qsTr("--")
        }
        return _transform.toDisplay(metres).toFixed(1) + " " + _transform.displayUnits
    }

    ColumnLayout {
        id:                 layout
        anchors.margins:    _root._margins
        anchors.left:       parent.left
        anchors.top:        parent.top
        spacing:            0

        QGCLabel {
            font.pointSize: ScreenTools.smallFontPointSize
            font.bold:      true
            text:           qsTr("Local Position")
        }

        GridLayout {
            columns:        2
            columnSpacing:  ScreenTools.defaultFontPixelWidth
            rowSpacing:     0

            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("North") }
            QGCLabel {
                font.pointSize:         ScreenTools.smallFontPointSize
                horizontalAlignment:    Text.AlignRight
                Layout.fillWidth:       true
                text:                   _root._distanceText(_root._north)
            }

            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("East") }
            QGCLabel {
                font.pointSize:         ScreenTools.smallFontPointSize
                horizontalAlignment:    Text.AlignRight
                Layout.fillWidth:       true
                text:                   _root._distanceText(_root._east)
            }

            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Range") }
            QGCLabel {
                font.pointSize:         ScreenTools.smallFontPointSize
                horizontalAlignment:    Text.AlignRight
                Layout.fillWidth:       true
                text:                   _root._distanceText(_root._range)
            }

            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Bearing") }
            QGCLabel {
                font.pointSize:         ScreenTools.smallFontPointSize
                horizontalAlignment:    Text.AlignRight
                Layout.fillWidth:       true
                text:                   isNaN(_root._bearing) ? qsTr("--") : Math.round(_root._bearing) + "°"
            }

            // Distance along the trail rather than from the origin. Drift on this kind of navigation
            // accumulates with ground covered, so this is the denominator the error is quoted
            // against -- and "range 0.4 m after flying 80 m" is a very different result from
            // "range 0.4 m after hovering".
            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Flown") }
            QGCLabel {
                font.pointSize:         ScreenTools.smallFontPointSize
                horizontalAlignment:    Text.AlignRight
                Layout.fillWidth:       true
                text:                   _root.gridView ? _root._distanceText(_root.gridView.trailLengthMetres) : qsTr("--")
            }
        }

        // Spelled out rather than left as an empty grid: a view with no vehicle data looks exactly
        // like a view of a vehicle sitting on the origin.
        QGCLabel {
            Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 4
            visible:            !_root._valid
            font.pointSize:     ScreenTools.smallFontPointSize
            color:              qgcPal.colorOrange
            text:               qsTr("No local position telemetry")
        }

        // Only while there is no origin. Once one is set this is the least interesting control here,
        // and re-setting it mid-flight moves the frame every reading above is measured in.
        QGCButton {
            objectName:         "localGrid_setOriginButton"
            Layout.fillWidth:   true
            Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 4
            visible:            _root.gridView ? !_root.gridView.originKnown : false
            primary:            true
            text:               qsTr("Set Estimator Origin…")
            onClicked:          _root.setOriginRequested()
        }

        RowLayout {
            Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 4
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCButton {
                text:       qsTr("Vehicle")
                enabled:    _root._valid && _root.gridView && !_root.gridView.followVehicle
                onClicked:  _root.gridView.centreOnVehicle()
            }

            QGCButton {
                text:       qsTr("Origin")
                enabled:    _root.gridView !== null
                onClicked:  _root.gridView.centreOnOrigin()
            }

            QGCButton {
                text:       qsTr("Clear trail")
                enabled:    _root.gridView && (_root.gridView.trailPointCount > 0)
                onClicked:  _root.gridView.clearTrail()
            }
        }
    }
}
