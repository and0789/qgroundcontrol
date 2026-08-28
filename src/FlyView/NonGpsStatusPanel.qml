import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView

/// Read-only readout of the values needed to fly without GPS and to calibrate an optical flow
/// sensor. Those values live in several different fact groups, so watching them during a flight
/// otherwise means building the list by hand in the telemetry values bar. Toggled from the fly
/// view tool strip.
Item {
    id:             _root
    objectName:     "flyView_nonGpsStatusPanel"
    implicitWidth:  mainLayout.implicitWidth + (_margins * 2)
    implicitHeight: Math.min(mainLayout.implicitHeight, _bodyMaximumHeight) + (_margins * 2)

    /// The most room this panel may take, set from outside. Negative for no limit.
    ///
    /// It is a long column of live values and it had no ceiling: on a short window the last few
    /// sections -- the EKF innovation ratios among them, which is what an operator opens this for --
    /// were drawn past the bottom edge and could not be reached at all. Past the ceiling the rows
    /// scroll, which is the same answer the local grid's own panels give.
    ///
    /// Negative rather than zero for the reason LocalGridMissionActions carries: zero is what a caller
    /// works out when there is genuinely no room, and read as "no limit" it turns the ceiling off in
    /// the one state it exists for.
    property real maximumHeight: -1

    readonly property real _bodyMaximumHeight: (maximumHeight >= 0)
                                                ? Math.max(0, maximumHeight - (_margins * 2))
                                                : Number.POSITIVE_INFINITY
    // Shown whenever toggled on, even with no vehicle. The rows then read "n/a", which tells the
    // user the toggle worked and the data is missing, rather than looking like a dead button.
    visible:        _showPanel

    property var    _activeVehicle:     QGroundControl.multiVehicleManager.activeVehicle
    property bool   _showPanel:         QGroundControl.settingsManager.flyViewSettings.showNonGpsStatusPanel.rawValue

    property var    _opticalFlow:       _activeVehicle ? _activeVehicle.opticalFlow : null
    property var    _estimatorStatus:   _activeVehicle ? _activeVehicle.estimatorStatus : null
    property var    _distanceSensors:   _activeVehicle ? _activeVehicle.distanceSensors : null
    property var    _vibration:         _activeVehicle ? _activeVehicle.vibration : null
    property var    _localPosition:     _activeVehicle ? _activeVehicle.localPosition : null
    property var    _estimatorOrigin:   _activeVehicle ? _activeVehicle.estimatorOrigin : null
    property bool   _originIsSet:       _estimatorOrigin ? _estimatorOrigin.isValid : false

    property real   _margins:           ScreenTools.defaultFontPixelHeight / 2
    property real   _labelWidth:        ScreenTools.defaultFontPixelWidth * 12
    property real   _valueWidth:        ScreenTools.defaultFontPixelWidth * 12

    // Pass criteria taken from the project's own flow calibration procedure and bench recorder,
    // not from generic defaults
    readonly property int  _minFlowQuality:      50
    readonly property real _vibeWarnThreshold:   30
    readonly property real _vibeBadThreshold:    60

    // EKF innovation test ratios are normalised, so 1.0 is the gate: above it the estimator is
    // rejecting the measurement outright. The warning level is ArduPilot's own FS_EKF_THRESH
    // default, the point at which Copter declares an EKF failsafe.
    readonly property real _ekfRatioWarnThreshold: 0.8
    readonly property real _ekfRatioBadThreshold:  1.0

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    NonGpsFlowHealth {
        id:      flowHealth
        vehicle: _root._activeVehicle
    }

    NonGpsAidingReason {
        id:             aidingReason
        vehicle:        _root._activeVehicle
        minFlowQuality: _root._minFlowQuality
        badRatio:       _root._ekfRatioBadThreshold
    }

    function _qualityColor(quality) {
        if (isNaN(quality)) {
            return qgcPal.text
        }
        return (quality > _minFlowQuality) ? qgcPal.colorGreen : qgcPal.colorRed
    }

    function _ekfRatioColor(ratio) {
        if (isNaN(ratio)) {
            return qgcPal.text
        }
        if (ratio > _ekfRatioBadThreshold) {
            return qgcPal.colorRed
        }
        return (ratio > _ekfRatioWarnThreshold) ? qgcPal.colorOrange : qgcPal.colorGreen
    }

    /// Nothing received already reads as "--" and stays in the ordinary colour. A reading of zero or
    /// less is a rangefinder that is streaming without getting a return, which the pre-flight check
    /// and the waypoint altitude ceiling both already refuse to treat as a height -- this row was the
    /// one place left rendering it as though it were a measurement.
    function _rangefinderColor(distance) {
        if (isNaN(distance)) {
            return qgcPal.text
        }
        return (distance > 0) ? qgcPal.colorGreen : qgcPal.colorRed
    }

    function _vibeColor(vibe) {
        if (isNaN(vibe)) {
            return qgcPal.text
        }
        if (vibe > _vibeBadThreshold) {
            return qgcPal.colorRed
        }
        return (vibe > _vibeWarnThreshold) ? qgcPal.colorOrange : qgcPal.colorGreen
    }

    component SectionHeader: QGCLabel {
        Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 3
        font.pointSize:     ScreenTools.smallFontPointSize
        font.bold:          true
        color:              qgcPal.text
    }

    component ValueRow: RowLayout {
        id:         valueRow
        spacing:    ScreenTools.defaultFontPixelWidth

        property string label
        property var    fact
        property color  valueColor: qgcPal.text

        QGCLabel {
            Layout.preferredWidth:  _root._labelWidth
            text:                   valueRow.label
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.text
        }

        QGCLabel {
            Layout.preferredWidth:  _root._valueWidth
            horizontalAlignment:    Text.AlignRight
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  valueRow.valueColor
            // Fact substitutes a placeholder for NaN on its own, so unpopulated telemetry reads as "--"
            text:                   valueRow.fact
                                        ? valueRow.fact.valueString + (valueRow.fact.units.length ? " " + valueRow.fact.units : "")
                                        : qsTr("n/a")
        }
    }

    /// A row whose value is computed rather than read straight off a fact
    component TextRow: RowLayout {
        id:         textRow
        spacing:    ScreenTools.defaultFontPixelWidth

        property string label
        property string value
        property color  valueColor: qgcPal.text

        QGCLabel {
            Layout.preferredWidth:  _root._labelWidth
            text:                   textRow.label
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.text
        }

        QGCLabel {
            Layout.preferredWidth:  _root._valueWidth
            horizontalAlignment:    Text.AlignRight
            text:                   textRow.value
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  textRow.valueColor
        }
    }

    /// A row for an EKF health flag, where the flag being set is what "good" means
    component FlagRow: RowLayout {
        id:         flagRow
        spacing:    ScreenTools.defaultFontPixelWidth

        property string label
        property var    fact
        /// These facts default to false, so a group that has never received telemetry looks
        /// identical to one reporting a real failure. Without this the panel reports a broken EKF
        /// when the truth is that EKF_STATUS_REPORT is not being streamed on this link at all.
        property bool   received: _root._estimatorStatus ? _root._estimatorStatus.telemetryAvailable : false
        /// Constant position mode is the estimator giving up and assuming the vehicle is still, so
        /// the flag being set is the failure. QGC names that fact "good..." like the others, and
        /// colouring it the same way paints the one healthy answer red.
        property bool   healthyWhenSet: true

        readonly property bool _healthy: flagRow.fact && (flagRow.fact.rawValue === flagRow.healthyWhenSet)

        QGCLabel {
            Layout.preferredWidth:  _root._labelWidth
            text:                   flagRow.label
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.text
        }

        QGCLabel {
            Layout.preferredWidth:  _root._valueWidth
            horizontalAlignment:    Text.AlignRight
            font.pointSize:         ScreenTools.smallFontPointSize
            // Always reads as "is this healthy", so no row needs the reader to remember which flag
            // is inverted
            text:                   (!flagRow.fact || !flagRow.received)
                                        ? qsTr("no data")
                                        : (flagRow._healthy ? qsTr("OK") : qsTr("NO"))
            color:                  (!flagRow.fact || !flagRow.received)
                                        ? qgcPal.text
                                        : (flagRow._healthy ? qgcPal.colorGreen : qgcPal.colorRed)
        }
    }

    Rectangle {
        anchors.fill:   parent
        color:          qgcPal.window
        opacity:        0.8
        radius:         ScreenTools.defaultFontPixelHeight / 2
    }

    DeadMouseArea {
        anchors.fill: parent
    }

    QGCFlickable {
        id:                 bodyFlickable
        anchors.fill:       parent
        anchors.margins:    _margins
        contentWidth:       width
        contentHeight:      mainLayout.implicitHeight

        ColumnLayout {
            id:                 mainLayout
            width:              bodyFlickable.width
            spacing:            0

            SectionHeader { text: qsTr("Estimator Origin") }

            TextRow {
                label:      qsTr("Status")
                value:      _root._originIsSet ? qsTr("Set") : qsTr("NOT SET")
                valueColor: _root._originIsSet ? qgcPal.colorGreen : qgcPal.colorRed
            }

            TextRow {
                label:      qsTr("Position")
                value:      _root._originIsSet
                                ? _root._estimatorOrigin.latitude.toFixed(7) + ", " + _root._estimatorOrigin.longitude.toFixed(7)
                                : qsTr("—")
                visible:    _root._originIsSet
            }

            // Spelled out because the consequence is invisible in flight: without an origin the vehicle
            // has no home, an altitude relative to home cannot be resolved, and an auto takeoff climbs
            // and then hangs forever on the mission's first item with nothing reported to the operator.
            QGCLabel {
                Layout.preferredWidth:  _root._labelWidth + _root._valueWidth + ScreenTools.defaultFontPixelWidth
                visible:                !_root._originIsSet
                wrapMode:               Text.WordWrap
                font.pointSize:         ScreenTools.smallFontPointSize
                color:                  qgcPal.colorRed
                text:                   qsTr("Missions cannot run. Set an origin from the local grid, or click the map and choose 'Set Estimator Origin'.")
            }

            // The line above is read by an operator standing over an aircraft they have just
            // rebooted, and what it says next depends on an answer QGC asked for seconds ago. Asking
            // again is the whole repair, and it is worth a button of its own: everything that
            // follows from the origin -- the grid, the plan, whether the set-origin control is even
            // offered -- goes wrong quietly when this line is out of date.
            QGCButton {
                objectName:             "nonGpsStatus_recheckOriginButton"
                Layout.topMargin:       ScreenTools.defaultFontPixelHeight / 2
                text:                   qsTr("Re-check origin")
                enabled:                _root._activeVehicle
                onClicked:              _root._activeVehicle.requestEstimatorOrigin()
            }

            SectionHeader { text: qsTr("Optical Flow") }

            ValueRow {
                label:      qsTr("Quality")
                fact:       _opticalFlow ? _opticalFlow.quality : null
                valueColor: _qualityColor(_opticalFlow ? _opticalFlow.quality.rawValue : NaN)
            }

            ValueRow {
                label:      qsTr("Flow |x,y|")
                fact:       _opticalFlow ? _opticalFlow.flowCompMagnitude : null
                // Red once the EKF would be discarding this reading
                valueColor: flowHealth.rejectingNow ? qgcPal.colorRed : qgcPal.text
            }

            ValueRow { label: qsTr("Flow X");        fact: _opticalFlow ? _opticalFlow.flowCompX : null }
            ValueRow { label: qsTr("Flow Y");        fact: _opticalFlow ? _opticalFlow.flowCompY : null }
            ValueRow { label: qsTr("Flow Height");   fact: _opticalFlow ? _opticalFlow.groundDistance : null }

            SectionHeader { text: qsTr("Flow Accepted by EKF") }

            TextRow {
                label: qsTr("EKF Limit")
                value: flowHealth.limitKnown
                           ? flowHealth.flowLimit.toFixed(2) + " " + qsTr("rad/s")
                           : qsTr("n/a")
            }

            TextRow {
                label:      qsTr("Rejected")
                value:      flowHealth.hasSamples
                                ? flowHealth.rejectedPercent.toFixed(0) + "% (" + flowHealth.rejectedCount + "/" + flowHealth.sampleCount + ")"
                                : qsTr("no data")
                valueColor: !flowHealth.hasSamples || !flowHealth.limitKnown
                                ? qgcPal.text
                                : (flowHealth.rejectedCount > 0 ? qgcPal.colorRed : qgcPal.colorGreen)
            }

            TextRow {
                label: qsTr("Mean |x,y|")
                value: flowHealth.hasSamples ? flowHealth.averageMagnitude.toFixed(3) + " " + qsTr("rad/s") : qsTr("no data")
            }

            TextRow {
                label:      qsTr("Peak |x,y|")
                value:      flowHealth.hasSamples ? flowHealth.peakMagnitude.toFixed(3) + " " + qsTr("rad/s") : qsTr("no data")
                valueColor: flowHealth.limitKnown && flowHealth.hasSamples && (flowHealth.peakMagnitude > flowHealth.flowLimit)
                                ? qgcPal.colorRed
                                : qgcPal.text
            }

            SectionHeader { text: qsTr("Rangefinder") }

            ValueRow {
                label:      qsTr("Down")
                fact:       _distanceSensors ? _distanceSensors.rotationPitch270 : null
                valueColor: _rangefinderColor(_distanceSensors ? _distanceSensors.rotationPitch270.rawValue : NaN)
            }

            // Optical flow gives velocity but no direction, so with EK3_SRC1_YAW=1 the compass is the
            // only thing telling the estimator which way that velocity points. A heading error does not
            // show up as a bad position -- it shows up as a track rotated away from the one planned,
            // which looks like ordinary drift in the log unless the heading was being watched.
            SectionHeader { text: qsTr("Compass") }

            ValueRow { label: qsTr("Heading");       fact: _activeVehicle ? _activeVehicle.heading : null }

            // The estimator's verdict on the compass rather than the magnetometer's own. A sensor can
            // report itself perfectly healthy while disagreeing with the rest of the solution, and it
            // is the disagreement that turns into a rotated track. Unlike the EKF health flags this
            // fact starts as NaN, so a link carrying no EKF status reads as "--" rather than as a
            // flawless compass.
            ValueRow {
                label:      qsTr("Mag Ratio")
                fact:       _estimatorStatus ? _estimatorStatus.magRatio : null
                valueColor: _ekfRatioColor(_estimatorStatus ? _estimatorStatus.magRatio.rawValue : NaN)
            }

            SectionHeader { text: qsTr("EKF") }

            FlagRow  { label: qsTr("Horiz Pos");     fact: _estimatorStatus ? _estimatorStatus.goodHorizPosRelEstimate : null }
            FlagRow  { label: qsTr("Horiz Vel");     fact: _estimatorStatus ? _estimatorStatus.goodHorizVelEstimate : null }
            FlagRow {
                label:          qsTr("Aiding")
                fact:           _estimatorStatus ? _estimatorStatus.goodConstPosModeEstimate : null
                // Set means the estimator fell back to assuming the vehicle is stationary
                healthyWhenSet: false
            }

            // "NO" names the state but not the cause, and only the cause can be acted on. The evidence
            // is already on this panel -- flow quality, the rangefinder, the EKF's own ratios -- but
            // reading it off six rows takes longer than the failure gives you.
            QGCLabel {
                Layout.preferredWidth:  _root._labelWidth + _root._valueWidth + ScreenTools.defaultFontPixelWidth
                visible:                aidingReason.aidingLost
                wrapMode:               Text.WordWrap
                font.pointSize:         ScreenTools.smallFontPointSize
                color:                  qgcPal.colorRed
                text:                   aidingReason.reason
            }

            ValueRow { label: qsTr("Vel Ratio");     fact: _estimatorStatus ? _estimatorStatus.velRatio : null }
            ValueRow { label: qsTr("Pos Ratio");     fact: _estimatorStatus ? _estimatorStatus.horizPosRatio : null }
            ValueRow { label: qsTr("HAGL Ratio");    fact: _estimatorStatus ? _estimatorStatus.haglRatio : null }

            SectionHeader { text: qsTr("Local Position") }

            ValueRow { label: qsTr("North");         fact: _localPosition ? _localPosition.x : null }
            ValueRow { label: qsTr("East");          fact: _localPosition ? _localPosition.y : null }
            ValueRow { label: qsTr("Down");          fact: _localPosition ? _localPosition.z : null }
            ValueRow { label: qsTr("Vel North");     fact: _localPosition ? _localPosition.vx : null }
            ValueRow { label: qsTr("Vel East");      fact: _localPosition ? _localPosition.vy : null }
            ValueRow { label: qsTr("Vel Down");      fact: _localPosition ? _localPosition.vz : null }

            SectionHeader { text: qsTr("Vibration") }

            ValueRow {
                label:      qsTr("Vibe X")
                fact:       _vibration ? _vibration.xAxis : null
                valueColor: _vibeColor(_vibration ? _vibration.xAxis.rawValue : NaN)
            }

            ValueRow {
                label:      qsTr("Vibe Y")
                fact:       _vibration ? _vibration.yAxis : null
                valueColor: _vibeColor(_vibration ? _vibration.yAxis.rawValue : NaN)
            }

            ValueRow {
                label:      qsTr("Vibe Z")
                fact:       _vibration ? _vibration.zAxis : null
                valueColor: _vibeColor(_vibration ? _vibration.zAxis.rawValue : NaN)
            }
        }
    }
}
