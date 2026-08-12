import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// What the pitot is measuring, shown only while a sensor is actually reporting.
///
/// Deliberately separate from the fly view's Air Speed value, which is read out of VFR_HUD and on a
/// multirotor is the estimator's opinion rather than the sensor's. These numbers come from the
/// AIRSPEED message, which the autopilot sends for every enabled sensor whether or not the estimator
/// has been told to use one -- so this panel says something even with ARSPD_USE left at zero, which
/// is where it belongs on a multirotor.
Rectangle {
    id: _root

    property var vehicle: null

    /// Collapsed to nothing rather than merely hidden, so the panel below closes the gap. An
    /// invisible item keeps its height in QML, and a vehicle with no airspeed sensor would otherwise
    /// fly the grid with a panel-shaped hole in the right-hand column.
    visible:        _available
    implicitWidth:  visible ? layout.implicitWidth + (_margins * 2) : 0
    implicitHeight: visible ? layout.implicitHeight + (_margins * 2) : 0

    color:          qgcPal.window
    opacity:        0.8
    radius:         ScreenTools.defaultFontPixelHeight / 4
    border.color:   qgcPal.groupBorder
    border.width:   1

    property real _margins: ScreenTools.defaultFontPixelHeight / 3

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    readonly property var _sensor: vehicle ? vehicle.airspeedSensor : null

    /// The sensor is reporting now, not merely at some point since this vehicle connected.
    ///
    /// The fact group works this out from the gap between AIRSPEED messages rather than from
    /// FactGroup.telemetryAvailable, which never goes back to false -- an unplugged pitot would
    /// otherwise leave this panel standing with the last numbers it gave.
    readonly property bool _available: _sensor ? _sensor.available : false

    readonly property bool _healthy: (_sensor && _available) ? _sensor.healthy.rawValue : false
    readonly property bool _inUse:   (_sensor && _available) ? _sensor.inUse.rawValue : false

    /// Unfolded by default, unlike the position readout beside it. That panel starts folded because
    /// it appears whether or not there is anything to say; this one appears only when a sensor is
    /// talking, so it always has numbers on arrival.
    property bool collapsed: false

    readonly property real _warningWidth: ScreenTools.defaultFontPixelWidth * 34

    function _factText(fact) {
        if (!_available || !fact || isNaN(fact.rawValue)) {
            return qsTr("--")
        }
        return fact.valueString + " " + fact.units
    }

    ColumnLayout {
        id:                 layout
        anchors.margins:    _root._margins
        anchors.left:       parent.left
        anchors.top:        parent.top
        spacing:            0

        // Wrapped so the mouse area has a sibling to anchor to rather than being an anchored child
        // of a layout, which Qt warns about on every build.
        Item {
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
                    text:               qsTr("Airspeed")
                }

                // Folded, the one number worth keeping in view. Without it, folding this panel costs
                // the reading it exists for.
                QGCLabel {
                    objectName:             "localGrid_airspeedSummary"
                    Layout.alignment:       Qt.AlignVCenter
                    Layout.fillWidth:       true
                    horizontalAlignment:    Text.AlignRight
                    visible:                _root.collapsed
                    font.pointSize:         ScreenTools.smallFontPointSize
                    color:                  qgcPal.colorGrey
                    text:                   _root._sensor ? _root._factText(_root._sensor.airspeed) : qsTr("--")
                }
            }

            QGCMouseArea {
                objectName: "localGrid_airspeedHeader"
                fillItem:   parent
                onClicked:  _root.collapsed = !_root.collapsed
            }
        }

        GridLayout {
            objectName:         "localGrid_airspeedNumbers"
            visible:            !_root.collapsed
            Layout.fillWidth:   true
            // Four columns, matching the position readout directly above. Two pairs to a row is what
            // keeps the right-hand column one rhythm rather than two: at two columns a single pair is
            // stretched across a panel sized to the readout, and the gap between a label and its own
            // number ends up wider than the number itself.
            columns:            4
            columnSpacing:      ScreenTools.defaultFontPixelWidth
            rowSpacing:         0

            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Airspeed") }
            QGCLabel {
                objectName:             "localGrid_airspeedValue"
                font.pointSize:         ScreenTools.smallFontPointSize
                horizontalAlignment:    Text.AlignRight
                Layout.fillWidth:       true
                text:                   _root._sensor ? _root._factText(_root._sensor.airspeed) : qsTr("--")
            }

            // Beside the airspeed, because one is derived from the other. A pitot that is blocked or
            // plumbed backwards shows here before it shows there, and on a rig being commissioned
            // that makes the pair worth reading together.
            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Diff press") }
            QGCLabel {
                objectName:             "localGrid_airspeedDiffPressure"
                font.pointSize:         ScreenTools.smallFontPointSize
                horizontalAlignment:    Text.AlignRight
                Layout.fillWidth:       true
                text:                   _root._sensor ? _root._factText(_root._sensor.diffPressure) : qsTr("--")
            }

            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Temp") }
            QGCLabel {
                objectName:             "localGrid_airspeedTemperature"
                font.pointSize:         ScreenTools.smallFontPointSize
                horizontalAlignment:    Text.AlignRight
                Layout.fillWidth:       true
                text:                   _root._sensor ? _root._factText(_root._sensor.temperature) : qsTr("--")
            }

            // A field rather than a sentence. On a multirotor the estimator never uses this sensor,
            // so the state it reports is permanent -- and a wrapped line of prose that will read the
            // same on every flight for the life of the aircraft is height spent saying nothing new.
            // As a field it fills the slot the fourth pair would leave empty and costs no rows.
            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Status") }
            QGCLabel {
                objectName:             "localGrid_airspeedStatus"
                font.pointSize:         ScreenTools.smallFontPointSize
                horizontalAlignment:    Text.AlignRight
                Layout.fillWidth:       true
                color:                  _root._healthy ? qgcPal.colorGrey : qgcPal.colorOrange
                text:                   !_root._healthy ? qsTr("Unhealthy")
                                            : (_root._inUse ? qsTr("In use") : qsTr("Measured only"))
            }
        }

        // Kept as prose only for the state that is neither permanent nor self-explanatory. "Unhealthy"
        // in the field above says which sensor is wrong; this says what it means for the number
        // sitting above it, which still looks like a measurement.
        QGCLabel {
            objectName:             "localGrid_airspeedUnhealthy"
            Layout.topMargin:       ScreenTools.defaultFontPixelHeight / 4
            Layout.fillWidth:       true
            Layout.maximumWidth:    _root._warningWidth
            visible:                !_root._healthy
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.colorOrange
            text:                   qsTr("The autopilot is reporting this sensor unhealthy — the reading above is not trustworthy")
        }
    }
}
