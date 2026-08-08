import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

Rectangle {
    id: _root

    required property var missionController

    width:  parent ? parent.width : 0
    height: mainColumn.height + (_margins * 2)
    color:  QGroundControl.globalPalette.windowShadeDark

    property real _margins:        ScreenTools.defaultFontPixelWidth / 2
    property real _textFieldWidth: ScreenTools.defaultFontPixelWidth * 20
    property real _labelWidth:     ScreenTools.defaultFontPixelWidth * 14
    property bool _hasHome:        missionController ? missionController.plannedHomePosition.isValid : false

    // The origin the vehicle is actually using. A plan anchored anywhere else flies somewhere else,
    // and nothing on screen says so: a stale plan uploads cleanly, flies smoothly, and simply lands
    // in the wrong place. Two field flights on 8 Aug were lost that way, 1.5 m and 4.2 m out.
    property var  _activeVehicle:    globals.activeVehicle
    property var  _estimatorOrigin:  _activeVehicle ? _activeVehicle.estimatorOrigin : null
    property bool _originValid:      _estimatorOrigin ? _estimatorOrigin.isValid : false

    TransformPositionController {
        id: positionController
        Component.onCompleted: {
            if (_hasHome) {
                coordinate = _root.missionController.plannedHomePosition
                initValues()
            }
        }
    }

    Connections {
        target: _root.missionController
        function onPlannedHomePositionChanged() {
            positionController.coordinate = _root.missionController.plannedHomePosition
            positionController.initValues()
        }
    }

    ColumnLayout {
        id:              mainColumn
        anchors.left:    parent.left
        anchors.right:   parent.right
        anchors.top:     parent.top
        anchors.margins: _margins
        spacing:         ScreenTools.defaultFontPixelHeight * 0.5

        // ── Offset Mission ──
        SectionHeader {
            id:               offsetSection
            Layout.fillWidth: true
            text:             qsTr("Offset Mission")
            checked:          false
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing:          _margins
            visible:          offsetSection.checked

            LabelledFactTextField {
                id:                      eastField
                label:                   qsTr("East")
                fact:                    positionController.offsetEast
                textFieldPreferredWidth: _textFieldWidth
                Layout.fillWidth:        true
            }

            LabelledFactTextField {
                id:                      northField
                label:                   qsTr("North")
                fact:                    positionController.offsetNorth
                textFieldPreferredWidth: _textFieldWidth
                Layout.fillWidth:        true
            }

            LabelledFactTextField {
                id:                      upField
                label:                   qsTr("Up")
                fact:                    positionController.offsetUp
                textFieldPreferredWidth: _textFieldWidth
                Layout.fillWidth:        true
            }

            QGCCheckBox {
                id:   offsetTakeoffCheck
                text: qsTr("Also move takeoff items")
            }

            QGCCheckBox {
                id:   offsetLandingCheck
                text: qsTr("Also move landing items")
            }

            QGCLabel {
                Layout.fillWidth:    true
                Layout.maximumWidth: _labelWidth + _textFieldWidth
                wrapMode:            Text.WordWrap
                font.pointSize:      ScreenTools.smallFontPointSize
                text:                qsTr("Note: Home altitude is not modified.")
            }

            QGCButton {
                Layout.alignment: Qt.AlignHCenter
                text:             qsTr("Apply Offset")
                enabled:          !eastField.textField.validationError
                                  && !northField.textField.validationError
                                  && !upField.textField.validationError

                onClicked: {
                    _root.missionController.offsetMission(
                        positionController.offsetEast.rawValue,
                        positionController.offsetNorth.rawValue,
                        positionController.offsetUp.rawValue,
                        offsetTakeoffCheck.checked,
                        offsetLandingCheck.checked
                    )
                }
            }
        }

        // ── Reposition Mission ──
        SectionHeader {
            id:               repositionSection
            Layout.fillWidth: true
            text:             qsTr("Reposition Mission")
            checked:          false
        }

        ColumnLayout {
            id:               repositionContent
            Layout.fillWidth: true
            spacing:          _margins
            visible:          repositionSection.checked

            QGCLabel {
                Layout.fillWidth: true
                wrapMode:         Text.WordWrap
                font.pointSize:   ScreenTools.smallFontPointSize
                text:             qsTr("Home position must be set to reposition the mission.")
                visible:          !_hasHome
            }

            property bool _showGeographic: coordinateSystemCombo.currentIndex === 0
            property bool _showUTM:        coordinateSystemCombo.currentIndex === 1
            property bool _showMGRS:       coordinateSystemCombo.currentIndex === 2
            property bool _showVehicle:    coordinateSystemCombo.currentIndex === 3

            ColumnLayout {
                Layout.fillWidth: true
                spacing:          0

                QGCLabel {
                    text: qsTr("Coordinate System")
                }

                QGCComboBox {
                    id:               coordinateSystemCombo
                    Layout.fillWidth: true
                    model:            globals.activeVehicle
                                      ? [ qsTr("Geographic"), qsTr("Universal Transverse Mercator"), qsTr("Military Grid Reference"), qsTr("Vehicle Position") ]
                                      : [ qsTr("Geographic"), qsTr("Universal Transverse Mercator"), qsTr("Military Grid Reference") ]
                }
            }

            LabelledFactTextField {
                id:                      latitudeField
                label:                   qsTr("Latitude")
                fact:                    positionController.latitude
                textFieldPreferredWidth: _textFieldWidth
                Layout.fillWidth:        true
                visible:                 repositionContent._showGeographic
            }

            LabelledFactTextField {
                id:                      longitudeField
                label:                   qsTr("Longitude")
                fact:                    positionController.longitude
                textFieldPreferredWidth: _textFieldWidth
                Layout.fillWidth:        true
                visible:                 repositionContent._showGeographic
            }

            QGCButton {
                Layout.alignment: Qt.AlignHCenter
                text:             qsTr("Move to Position")
                enabled:          _hasHome && !latitudeField.textField.validationError && !longitudeField.textField.validationError
                visible:          repositionContent._showGeographic
                onClicked: {
                    positionController.setFromGeo()
                    _root.missionController.repositionMission(positionController.coordinate)
                }
            }

            LabelledFactTextField {
                id:                      zoneField
                label:                   qsTr("Zone")
                fact:                    positionController.zone
                textFieldPreferredWidth: _textFieldWidth
                Layout.fillWidth:        true
                visible:                 repositionContent._showUTM
            }

            LabelledFactComboBox {
                label:            qsTr("Hemisphere")
                fact:             positionController.hemisphere
                indexModel:       false
                Layout.fillWidth: true
                visible:          repositionContent._showUTM
            }

            LabelledFactTextField {
                id:                      eastingField
                label:                   qsTr("Easting")
                fact:                    positionController.easting
                textFieldPreferredWidth: _textFieldWidth
                Layout.fillWidth:        true
                visible:                 repositionContent._showUTM
            }

            LabelledFactTextField {
                id:                      northingField
                label:                   qsTr("Northing")
                fact:                    positionController.northing
                textFieldPreferredWidth: _textFieldWidth
                Layout.fillWidth:        true
                visible:                 repositionContent._showUTM
            }

            QGCButton {
                Layout.alignment: Qt.AlignHCenter
                text:             qsTr("Move to Position")
                enabled:          _hasHome && !zoneField.textField.validationError && !eastingField.textField.validationError && !northingField.textField.validationError
                visible:          repositionContent._showUTM
                onClicked: {
                    positionController.setFromUTM()
                    _root.missionController.repositionMission(positionController.coordinate)
                }
            }

            LabelledFactTextField {
                id:                      mgrsField
                label:                   qsTr("MGRS")
                fact:                    positionController.mgrs
                textFieldPreferredWidth: _textFieldWidth
                Layout.fillWidth:        true
                visible:                 repositionContent._showMGRS
            }

            QGCButton {
                Layout.alignment: Qt.AlignHCenter
                text:             qsTr("Move to Position")
                enabled:          _hasHome && !mgrsField.textField.validationError
                visible:          repositionContent._showMGRS
                onClicked: {
                    positionController.setFromMGRS()
                    _root.missionController.repositionMission(positionController.coordinate)
                }
            }

            QGCButton {
                Layout.alignment: Qt.AlignHCenter
                text:             qsTr("Move to Vehicle Position")
                enabled:          _hasHome
                visible:          repositionContent._showVehicle
                onClicked: {
                    positionController.setFromVehicle()
                    _root.missionController.repositionMission(positionController.coordinate)
                }
            }

            // Deliberately not behind the coordinate system selector above. For a vehicle flying
            // without GNSS this is not one way of repositioning among several -- it is the only
            // anchor that makes the mission fly where it was drawn, because the autopilot converts
            // every waypoint into an offset from this origin.
            QGCButton {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: _margins
                text:             qsTr("Move to Estimator Origin")
                enabled:          _hasHome
                visible:          _root._originValid
                onClicked:        _root.missionController.repositionMission(_root._estimatorOrigin)
            }

            QGCLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                visible:          _root._originValid
                wrapMode:         Text.WordWrap
                font.pointSize:   ScreenTools.smallFontPointSize
                // Guarded rather than relying on visible: QML evaluates a binding whether or not
                // the item is shown, so reading latitude off a null origin would error every time
                // the panel is built for a vehicle that has none.
                text:             _root._originValid
                                    ? qsTr("Origin: %1, %2")
                                        .arg(_root._estimatorOrigin.latitude.toFixed(7))
                                        .arg(_root._estimatorOrigin.longitude.toFixed(7))
                                    : ""
            }
        }

        // ── Rotate Mission ──
        SectionHeader {
            id:               rotateSection
            Layout.fillWidth: true
            text:             qsTr("Rotate Mission")
            checked:          false
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing:          _margins
            visible:          rotateSection.checked

            QGCLabel {
                Layout.fillWidth: true
                wrapMode:         Text.WordWrap
                font.pointSize:   ScreenTools.smallFontPointSize
                text:             qsTr("Home position must be set to rotate the mission.")
                visible:          !_hasHome
            }

            LabelledFactTextField {
                id:                      degreesCWField
                label:                   qsTr("Clockwise")
                fact:                    positionController.rotateDegreesCW
                textFieldPreferredWidth: _textFieldWidth
                Layout.fillWidth:        true
            }

            QGCCheckBox {
                id:   rotateTakeoffCheck
                text: qsTr("Also move takeoff items")
            }

            QGCCheckBox {
                id:   rotateLandingCheck
                text: qsTr("Also move landing items")
            }

            QGCLabel {
                Layout.fillWidth:    true
                Layout.maximumWidth: _labelWidth + _textFieldWidth
                wrapMode:            Text.WordWrap
                font.pointSize:      ScreenTools.smallFontPointSize
                text:                qsTr("Note: Complex items are rotated by moving their reference coordinate: their geometry and orientation are not changed.")
            }

            QGCButton {
                Layout.alignment: Qt.AlignHCenter
                text:             qsTr("Apply Rotation")
                enabled:          _hasHome && !degreesCWField.textField.validationError

                onClicked: {
                    _root.missionController.rotateMission(
                        positionController.rotateDegreesCW.rawValue,
                        rotateTakeoffCheck.checked,
                        rotateLandingCheck.checked
                    )
                }
            }
        }
    }
}
