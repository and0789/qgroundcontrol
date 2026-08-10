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

    /// NaN when the vehicle has not reported one, which is a different thing from north
    readonly property real _heading:   gridView ? gridView.vehicleHeadingDegrees : NaN

    readonly property bool _stale:     gridView ? gridView.positionStale : false
    readonly property real _ageSeconds: gridView ? gridView.positionAgeSeconds : NaN

    readonly property bool   _estimatorDegraded: gridView ? gridView.estimatorDegraded : false
    readonly property bool   _estimatorSevere:   gridView ? gridView.estimatorSevere : false
    readonly property string _estimatorWarning:  gridView ? gridView.estimatorWarning : ""

    readonly property bool   _drifting:      gridView ? gridView.positionDrifting : false
    readonly property string _driftWarning:  gridView ? gridView.positionDriftWarning : ""

    readonly property bool _nearCeiling:  gridView ? gridView.heightNearCeiling : false
    readonly property bool _aboveCeiling: gridView ? gridView.heightAboveCeiling : false
    readonly property real _height:       gridView ? gridView.currentHeightMetres : NaN

    readonly property string _limitText: (gridView && gridView.altitudeLimitKnown)
                                            ? _distanceText(gridView.altitudeLimitMetres)
                                            : qsTr("--")

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

            // Next to the bearing, because the pair is what a turn is worked out from: where the
            // nose points against where the operator wants to go. Kept as a number rather than a
            // rose of its own -- the fly view's instrument panel already draws one, and reading a
            // heading off a dial by eye is the estimate this whole grid exists to replace.
            QGCLabel { objectName: "localGrid_headingLabel"; font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Heading") }
            QGCLabel {
                objectName:             "localGrid_headingValue"
                font.pointSize:         ScreenTools.smallFontPointSize
                horizontalAlignment:    Text.AlignRight
                Layout.fillWidth:       true
                // Dashes rather than a zero. The instrument panel's compass reads its heading as
                // zero when the vehicle has not sent one, which points confidently at north; this
                // one says it does not know.
                text:                   isNaN(_root._heading) ? qsTr("--") : Math.round(_root._heading) + "°"
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

        // The numbers above are the last ones that arrived, and every one of them still reads as a
        // measurement. Said in words with an age against it, because the figures themselves cannot
        // say how old they are -- and a frozen readout is indistinguishable from a steady hover.
        QGCLabel {
            Layout.topMargin:       ScreenTools.defaultFontPixelHeight / 4
            Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 22
            visible:                _root._stale
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            font.bold:              true
            color:                  qgcPal.colorOrange
            text:                   qsTr("Position %1 s old — not current").arg(
                                        isNaN(_root._ageSeconds) ? "--" : Math.round(_root._ageSeconds))
        }

        // What the estimator thinks of its own solution. Kept here rather than left to the non-GPS
        // status panel: that panel is a separate window the operator cannot watch while flying the
        // grid, and this is the one fact that decides whether anything else on this grid means
        // anything. Silent while the solution is healthy, so it is never background noise.
        QGCLabel {
            Layout.topMargin:       ScreenTools.defaultFontPixelHeight / 4
            Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 22
            visible:                _root._estimatorDegraded && (_root._estimatorWarning !== "")
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            font.bold:              _root._estimatorSevere
            // Matched to the vehicle marker, which goes to a red outline for the same conditions.
            // Two different colours for one state reads as two different problems.
            color:                  _root._estimatorSevere ? qgcPal.colorRed : qgcPal.colorOrange
            text:                   _root._estimatorWarning
        }

        // Said here rather than left to the operator to spot in the Range figure above. That figure
        // is as large for an aircraft parked away from the origin as for one whose frame has slid,
        // and only one of those is a fault -- so the number alone cannot raise this, and a warning
        // built on it would fire every flight and be learned away.
        QGCLabel {
            objectName:             "localGrid_driftWarning"
            Layout.topMargin:       ScreenTools.defaultFontPixelHeight / 4
            Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 22
            visible:                _root._drifting
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.colorOrange
            text:                   _root._driftWarning
        }

        // The ceiling the plan was checked against, now checked against where the vehicle actually
        // is. A plan flown exactly as drawn still arrives here when the operator climbs by hand or
        // the ground falls away under a level pattern -- and above the rangefinder's range the
        // estimator has no height source at all.
        QGCLabel {
            Layout.topMargin:       ScreenTools.defaultFontPixelHeight / 4
            Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 22
            visible:                _root._nearCeiling
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            font.bold:              _root._aboveCeiling
            color:                  _root._aboveCeiling ? qgcPal.colorRed : qgcPal.colorOrange
            text:                   _root._aboveCeiling
                                        ? qsTr("%1 — above the rangefinder's %2 range. The estimator has no height reference. Descend.")
                                            .arg(_root._distanceText(_root._height))
                                            .arg(_root._limitText)
                                        : qsTr("%1 — nearing the rangefinder's %2 range.")
                                            .arg(_root._distanceText(_root._height))
                                            .arg(_root._limitText)
        }

        // Available whether or not an origin exists. An origin set to the wrong place is not a
        // cosmetic mistake -- the autopilot checks the compass against the magnetic model at that
        // position and refuses to arm when they disagree -- so correcting one has to be possible
        // without reconnecting. Highlighted only while there is none, since that is the state that
        // blocks everything else.
        QGCButton {
            objectName:         "localGrid_setOriginButton"
            Layout.fillWidth:   true
            Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 4
            visible:            _root.gridView !== null
            primary:            _root.gridView ? !_root.gridView.originKnown : false
            text:               (_root.gridView && _root.gridView.originKnown)
                                    ? qsTr("Change Estimator Origin…")
                                    : qsTr("Set Estimator Origin…")
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
