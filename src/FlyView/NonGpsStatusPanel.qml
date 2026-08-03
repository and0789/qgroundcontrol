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
    implicitWidth:  mainLayout.implicitWidth + (_margins * 2)
    implicitHeight: mainLayout.implicitHeight + (_margins * 2)
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

    property real   _margins:           ScreenTools.defaultFontPixelHeight / 2
    property real   _labelWidth:        ScreenTools.defaultFontPixelWidth * 12
    property real   _valueWidth:        ScreenTools.defaultFontPixelWidth * 12

    // Pass criteria taken from the project's own flow calibration procedure and bench recorder,
    // not from generic defaults
    readonly property int  _minFlowQuality:      50
    readonly property real _vibeWarnThreshold:   30
    readonly property real _vibeBadThreshold:    60

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    NonGpsFlowHealth {
        id:      flowHealth
        vehicle: _root._activeVehicle
    }

    function _qualityColor(quality) {
        if (isNaN(quality)) {
            return qgcPal.text
        }
        return (quality > _minFlowQuality) ? qgcPal.colorGreen : qgcPal.colorRed
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
            text:                   flagRow.fact ? (flagRow.fact.rawValue ? qsTr("OK") : qsTr("NO")) : qsTr("n/a")
            color:                  flagRow.fact
                                        ? (flagRow.fact.rawValue ? qgcPal.colorGreen : qgcPal.colorRed)
                                        : qgcPal.text
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

    ColumnLayout {
        id:                 mainLayout
        anchors.margins:    _margins
        anchors.left:       parent.left
        anchors.top:        parent.top
        spacing:            0

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

        ValueRow { label: qsTr("Down");          fact: _distanceSensors ? _distanceSensors.rotationPitch270 : null }

        SectionHeader { text: qsTr("EKF") }

        FlagRow  { label: qsTr("Horiz Pos");     fact: _estimatorStatus ? _estimatorStatus.goodHorizPosRelEstimate : null }
        FlagRow  { label: qsTr("Horiz Vel");     fact: _estimatorStatus ? _estimatorStatus.goodHorizVelEstimate : null }
        FlagRow  { label: qsTr("Const Pos");     fact: _estimatorStatus ? _estimatorStatus.goodConstPosModeEstimate : null }
        ValueRow { label: qsTr("Vel Ratio");     fact: _estimatorStatus ? _estimatorStatus.velRatio : null }
        ValueRow { label: qsTr("Pos Ratio");     fact: _estimatorStatus ? _estimatorStatus.horizPosRatio : null }
        ValueRow { label: qsTr("HAGL Ratio");    fact: _estimatorStatus ? _estimatorStatus.haglRatio : null }

        SectionHeader { text: qsTr("Local Position") }

        ValueRow { label: qsTr("North");         fact: _localPosition ? _localPosition.x : null }
        ValueRow { label: qsTr("East");          fact: _localPosition ? _localPosition.y : null }
        ValueRow { label: qsTr("Down");          fact: _localPosition ? _localPosition.z : null }
        ValueRow { label: qsTr("Vel North");     fact: _localPosition ? _localPosition.vx : null }
        ValueRow { label: qsTr("Vel East");      fact: _localPosition ? _localPosition.vy : null }

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
