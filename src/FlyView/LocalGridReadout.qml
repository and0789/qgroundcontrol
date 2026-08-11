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

    /// The same width as the mission list stacked under it, so the right edge of the view is one
    /// column of two panels rather than two boxes of different widths. Sized to its content before,
    /// this panel came out narrower than the list and the pair looked ragged; with the numbers in two
    /// columns there is something to fill the width with.
    implicitWidth:  ScreenTools.defaultFontPixelWidth * 28
    implicitHeight: layout.implicitHeight + (_margins * 2)
    color:          qgcPal.window
    opacity:        0.8
    radius:         ScreenTools.defaultFontPixelHeight / 4
    // Outlined like the mission list beside it, and for the reason that panel found: folded away,
    // an unbordered header is a line of text floating on the grid with nothing to say it is a panel
    // or that clicking it brings the numbers back.
    border.color:   qgcPal.groupBorder
    border.width:   1

    /// Folded to leave the grid clear, keeping the header and its summary so it can be found again.
    ///
    /// Starts folded, because a panel that opens before there is any telemetry stands over the grid
    /// showing six dashes and a line saying it has nothing -- which is the largest this panel ever
    /// gets and the least it ever says.
    property bool collapsed: true

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
        // Rounded to the shown precision first, then nudged off negative zero: adding zero to -0
        // gives +0, while toFixed alone would print "-0.0". Without it a vehicle a millimetre south
        // of the origin reads as minus nothing, and on a panel whose whole job is telling north from
        // south, a minus sign carrying no distance is a direction the operator has to talk
        // themselves out of.
        const rounded = Math.round(_transform.toDisplay(metres) * 10) / 10
        return (rounded + 0).toFixed(1) + " " + _transform.displayUnits
    }

    /// Opens itself when telemetry starts arriving and folds away again when there is none, so the
    /// panel stands in front of the grid only while it has something to say. A toggle in between is
    /// the operator's and is left alone.
    property bool _wasValid: false

    on_ValidChanged: {
        if (_valid !== _wasValid) {
            collapsed = !_valid
            _wasValid = _valid
        }
    }

    /// What the header carries while folded: the pair a return leg is flown on. Without it, folding
    /// costs a click on the very thing the operator most wants to see, and the panel would be opened
    /// again every time it was closed.
    readonly property string _summary: _valid && !isNaN(_range) && !isNaN(_bearing)
                                        ? qsTr("%1 @ %2°").arg(_distanceText(_range)).arg(Math.round(_bearing))
                                        : qsTr("--")

    ColumnLayout {
        id:                 layout
        anchors.margins:    _root._margins
        anchors.left:       parent.left
        anchors.right:      parent.right
        anchors.top:        parent.top
        spacing:            0

        // The row is wrapped so the mouse area covering it has a sibling to anchor to. Anchored
        // straight onto the RowLayout it would be an anchored child of a layout, which Qt calls
        // undefined behaviour and warns about on every build of the grid.
        Item {
            id:                 headerBlock
            Layout.fillWidth:   true
            implicitHeight:     headerRow.implicitHeight

            RowLayout {
                id:                     headerRow
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
                    font.pointSize:     ScreenTools.smallFontPointSize
                    font.bold:          true
                    text:               qsTr("Local Position")
                }

                // Only while folded. Open, the same pair is two rows below in full, and repeating it
                // in the header reads as a second measurement rather than the same one.
                QGCLabel {
                    objectName:         "localGrid_readoutSummary"
                    Layout.alignment:   Qt.AlignVCenter
                    Layout.fillWidth:   true
                    horizontalAlignment: Text.AlignRight
                    visible:            _root.collapsed
                    font.pointSize:     ScreenTools.smallFontPointSize
                    color:              qgcPal.colorGrey
                    text:               _root._summary
                }
            }

            QGCMouseArea {
                objectName: "localGrid_readoutHeader"
                fillItem:   parent
                onClicked:  _root.collapsed = !_root.collapsed
            }
        }

        GridLayout {
            visible:        !_root.collapsed
            // Two pairs to a row rather than six rows of one. The first two rows each hold one idea
            // whole: where the vehicle is in the frame's own axes, then the same position said as the
            // range and bearing a return leg is flown on. The third pairs the two that are left over
            // and means nothing by being together -- which is the price of the halved height, and
            // cheap at six numbers.
            Layout.fillWidth: true
            columns:        4
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

            // Beside the range, because the two are read as one figure: how far, and which way.
            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Bearing") }
            QGCLabel {
                font.pointSize:         ScreenTools.smallFontPointSize
                horizontalAlignment:    Text.AlignRight
                Layout.fillWidth:       true
                text:                   isNaN(_root._bearing) ? qsTr("--") : Math.round(_root._bearing) + "°"
            }

            // Kept as a number rather than a rose of its own -- the fly view's instrument panel
            // already draws one, and reading a heading off a dial by eye is the estimate this whole
            // grid exists to replace.
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
            objectName:             "localGrid_staleWarning"
            Layout.topMargin:       ScreenTools.defaultFontPixelHeight / 4
            Layout.fillWidth:       true
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
            Layout.fillWidth:       true
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
            Layout.fillWidth:       true
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
            Layout.fillWidth:       true
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

        // Only while there is no origin, which is the one state where nothing else on this view means
        // anything -- so it earns the width and the highlight. Once an origin exists, changing it is
        // a rare and consequential thing that moves the frame every position and waypoint is measured
        // in, and it moves to the correction dialog on the origin marker rather than standing in the
        // middle of a panel of live numbers.
        QGCButton {
            objectName:         "localGrid_setOriginButton"
            Layout.fillWidth:   true
            Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 4
            visible:            _root.gridView ? !_root.gridView.originKnown : false
            primary:            true
            text:               qsTr("Set Estimator Origin…")
            onClicked:          _root.setOriginRequested()
        }

        // Folded away with the numbers. These aim the camera and clear a drawn line -- nothing about
        // them is urgent, and an operator who has folded the panel to see the grid is not looking for
        // them. They stay here rather than joining the mission strip in the far corner: that panel
        // already carries a Clear that wipes the flight plan, and a Clear trail beside it would be two
        // buttons a glance apart with very different consequences.
        RowLayout {
            visible:            !_root.collapsed
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
