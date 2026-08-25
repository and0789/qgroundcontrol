import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// What is wrong with the picture the grid is drawing, across the top of it.
///
/// These lived in the position readout, in the column down the right, and that turned out to be the
/// one place they could not be shown. A warning is a sentence and the column is 133px wide and about
/// 130 tall on a ground station in landscape -- so a warning arriving either pushed the plan list out
/// of the column, leaving the operator a list header with no rows, or was itself cut off mid-sentence
/// by the ceiling the column had to be held to. "Reported position has moved 8.8 m in 1 min while
/// disarmed. If the aircraft has not been" is not a warning.
///
/// The band across the top is the one part of this view nothing stands in, which is what a sentence
/// needs: at 800 wide it is some 480px across against the column's 133, and it is bounded by the two
/// columns rather than by either one's contents. It is also where the eye goes, which is the right
/// place for the thing that says the numbers cannot be trusted.
///
/// Nothing when there is nothing wrong, so the grid is clear in the state it is in for most of a
/// flight -- the same shape the undo control and the plan hint are built on.
Rectangle {
    id: _root

    /// The grid these describe. Every one of them is a plain reading off it: this holds no state of
    /// its own, which is what made lifting them out of the readout a move rather than a rewrite.
    property var gridView: null

    /// How wide these sentences may be, set from outside by the view that knows where the two columns
    /// are. Negative for no limit.
    property real maximumWidth: -1

    readonly property bool _valid:            gridView ? gridView.positionValid : false
    readonly property bool _stale:            gridView ? gridView.positionStale : false
    readonly property real _ageSeconds:       gridView ? gridView.positionAgeSeconds : NaN

    readonly property bool   _estimatorDegraded: gridView ? gridView.estimatorDegraded : false
    readonly property bool   _estimatorSevere:   gridView ? gridView.estimatorSevere : false
    readonly property string _estimatorWarning:  gridView ? gridView.estimatorWarning : ""

    readonly property bool   _drifting:     gridView ? gridView.positionDrifting : false
    readonly property string _driftWarning: gridView ? gridView.positionDriftWarning : ""

    readonly property string _armingWarning: gridView ? gridView.armingBlockedWarning : ""

    readonly property bool _nearCeiling:  gridView ? gridView.heightNearCeiling : false
    readonly property bool _aboveCeiling: gridView ? gridView.heightAboveCeiling : false
    readonly property real _height:       gridView ? gridView.currentHeightMetres : NaN

    /// True when the vehicle is connected at all. With no vehicle there is nothing to warn about, and
    /// "No local position telemetry" against an empty view is a description rather than a warning.
    readonly property bool _hasVehicle: gridView ? (gridView.vehicle !== null) : false

    readonly property bool _anyWarning: _hasVehicle
                                            && (!_valid || _stale
                                                || (_estimatorDegraded && (_estimatorWarning !== ""))
                                                || _drifting || _nearCeiling || (_armingWarning !== ""))

    readonly property real _margins: ScreenTools.defaultFontPixelHeight / 3

    readonly property string _limitText: (gridView && gridView.altitudeLimitKnown)
                                            ? _distanceText(gridView.altitudeLimitMetres)
                                            : qsTr("--")

    function _distanceText(metres) {
        if (!gridView || !gridView.gridTransform || isNaN(metres)) {
            return qsTr("--")
        }
        return gridView.gridTransform.toDisplay(metres).toFixed(1) + " " + gridView.gridTransform.displayUnits
    }

    visible:        _anyWarning
    implicitWidth:  layout.implicitWidth + (_margins * 2)
    implicitHeight: layout.implicitHeight + (_margins * 2)
    color:          qgcPal.window
    opacity:        0.9
    radius:         ScreenTools.defaultFontPixelHeight / 4
    border.color:   qgcPal.groupBorder
    border.width:   1

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    // Over the grid's own wheel-to-zoom and drag-to-pan, like every other panel of this view
    DeadMouseArea {
        anchors.fill: parent
    }

    ColumnLayout {
        id:                 layout
        anchors.margins:    _root._margins
        anchors.left:       parent.left
        anchors.right:      parent.right
        anchors.top:        parent.top
        spacing:            ScreenTools.defaultFontPixelHeight / 4

        // Ordered worst first, so a stack of them reads down from what has to be acted on now.

        // What the estimator thinks of its own solution. Kept out of the non-GPS status panel: that
        // panel is a separate window the operator cannot watch while flying the grid, and this is the
        // one fact that decides whether anything else on this grid means anything.
        QGCLabel {
            objectName:         "localGrid_estimatorWarning"
            Layout.fillWidth:   true
            visible:            _root._estimatorDegraded && (_root._estimatorWarning !== "")
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
            font.bold:          _root._estimatorSevere
            // Matched to the vehicle marker, which goes to a red outline for the same conditions. Two
            // different colours for one state reads as two different problems.
            color:              _root._estimatorSevere ? qgcPal.colorRed : qgcPal.colorOrange
            text:               _root._estimatorWarning
        }

        // The ceiling the plan was checked against, now checked against where the vehicle actually is.
        // A plan flown exactly as drawn still arrives here when the operator climbs by hand or the
        // ground falls away under a level pattern -- and above the rangefinder's range the estimator
        // has no height source at all.
        QGCLabel {
            objectName:         "localGrid_ceilingWarning"
            Layout.fillWidth:   true
            visible:            _root._nearCeiling
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
            font.bold:          _root._aboveCeiling
            color:              _root._aboveCeiling ? qgcPal.colorRed : qgcPal.colorOrange
            text:               _root._aboveCeiling
                                    ? qsTr("%1 — above the rangefinder's %2 range. The estimator has no height reference. Descend.")
                                        .arg(_root._distanceText(_root._height))
                                        .arg(_root._limitText)
                                    : qsTr("%1 — nearing the rangefinder's %2 range.")
                                        .arg(_root._distanceText(_root._height))
                                        .arg(_root._limitText)
        }

        // The numbers in the readout are the last ones that arrived, and every one of them still reads
        // as a measurement. Said in words with an age against it, because the figures themselves cannot
        // say how old they are -- and a frozen readout is indistinguishable from a steady hover.
        QGCLabel {
            objectName:         "localGrid_staleWarning"
            Layout.fillWidth:   true
            visible:            _root._stale
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
            font.bold:          true
            color:              qgcPal.colorOrange
            text:               qsTr("Position %1 s old — not current").arg(
                                    isNaN(_root._ageSeconds) ? "--" : Math.round(_root._ageSeconds))
        }

        // Said here rather than left to the operator to spot in the Range figure. That figure is as
        // large for an aircraft parked away from the origin as for one whose frame has slid, and only
        // one of those is a fault -- so the number alone cannot raise this, and a warning built on it
        // would fire every flight and be learned away.
        QGCLabel {
            objectName:         "localGrid_driftWarning"
            Layout.fillWidth:   true
            visible:            _root._drifting
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
            color:              qgcPal.colorOrange
            text:               _root._driftWarning
        }

        // Kept out of the banner the fly view already has for it. That banner stands in the middle of
        // the view for thirty-five seconds and then takes the reason away with it, so an operator who
        // was watching the aircraft rather than the screen is told nothing; this line stays for as
        // long as the vehicle is refusing.
        QGCLabel {
            objectName:         "localGrid_armingWarning"
            Layout.fillWidth:   true
            visible:            _root._armingWarning !== ""
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
            // Orange rather than red: the aircraft is on the ground and being kept there, which is the
            // check working. Red here means the picture cannot be trusted.
            color:              qgcPal.colorOrange
            text:               _root._armingWarning
        }

        // Spelled out rather than left as an empty readout: a view with no vehicle data looks exactly
        // like a view of a vehicle sitting on the origin.
        QGCLabel {
            objectName:         "localGrid_noTelemetryWarning"
            Layout.fillWidth:   true
            visible:            !_root._valid
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
            color:              qgcPal.colorOrange
            text:               qsTr("No local position telemetry")
        }
    }
}
