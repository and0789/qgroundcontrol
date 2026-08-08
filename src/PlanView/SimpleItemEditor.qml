import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

// Editor for Simple mission items
Rectangle {
    required property var missionItem
    required property real availableWidth

    id: root
    width: availableWidth
    height: editorColumn.height + (_margin * 2)
    color: qgcPal.windowShadeDark
    radius: _radius


    property bool _specifiesAltitude: missionItem.specifiesAltitude
    property real _margin: ScreenTools.defaultFontPixelHeight / 2
    property real _altRectMargin: ScreenTools.defaultFontPixelWidth / 2
    property var _controllerVehicle: missionItem.masterController.controllerVehicle
    property int _globalAltFrame: missionItem.masterController.missionController.globalAltitudeFrame
    property bool _globalAltFrameIsMixed: _globalAltFrame == QGroundControl.AltitudeFrameMixed
    property real _radius: ScreenTools.defaultFontPixelWidth / 2
    property real _fieldSpacing: ScreenTools.defaultFontPixelHeight / 2

    property var  _plannedHome: missionItem.masterController.missionController.plannedHomePosition
    property bool _homeValid:   _plannedHome.isValid && missionItem.coordinate.isValid

    // Geodesic, via QtPositioning, rather than a flat-earth approximation: the same maths the rest
    // of the plan uses, so a waypoint typed here lands where a waypoint dragged there would.
    property real _distanceFromHome: _homeValid ? _plannedHome.distanceTo(missionItem.coordinate) : 0
    property real _azimuthFromHome:  _homeValid ? _plannedHome.azimuthTo(missionItem.coordinate) : 0
    property real _northMetres:      _distanceFromHome * Math.cos(_azimuthFromHome * Math.PI / 180)
    property real _eastMetres:       _distanceFromHome * Math.sin(_azimuthFromHome * Math.PI / 180)

    // Shown in whatever horizontal distance unit the user has chosen, like every other distance in
    // QGC. The maths above stays in metres; only the two fields convert.
    property real _displayNorth: QGroundControl.unitsConversion.metersToAppSettingsHorizontalDistanceUnits(_northMetres)
    property real _displayEast:  QGroundControl.unitsConversion.metersToAppSettingsHorizontalDistanceUnits(_eastMetres)

    // Compass convention, matching the vehicle's own heading: 0 is north, 90 east. azimuthTo
    // already returns that, so no conversion is needed -- only a name the operator recognises.
    property real _bearingFromHome:  _azimuthFromHome
    property real _displayDistance:  QGroundControl.unitsConversion.metersToAppSettingsHorizontalDistanceUnits(_distanceFromHome)

    /// The coordinate this waypoint's leg starts from: the nearest earlier item that has one, or
    /// the planned home for the first. Index 0 is the mission settings item, which is home anyway.
    function _findPreviousCoordinate() {
        var items = missionItem.masterController.missionController.visualItems
        if (!items) {
            return _plannedHome
        }
        var myIndex = -1
        for (var i = 0; i < items.count; i++) {
            if (items.get(i) === missionItem) {
                myIndex = i
                break
            }
        }
        for (var j = myIndex - 1; j > 0; j--) {
            var earlier = items.get(j)
            if (earlier.specifiesCoordinate && earlier.coordinate.isValid) {
                return earlier.coordinate
            }
        }
        return _plannedHome
    }

    // Reading missionItem.distance here is deliberate, not a stray statement: it is the controller's
    // own distance-to-previous, recomputed whenever any item in the plan moves. Touching it makes
    // this binding depend on it, so the leg figures refresh when an earlier waypoint is dragged --
    // something a plain function call would never notice.
    property var  _previousCoord: {
        missionItem.distance
        missionItem.coordinate
        return _findPreviousCoordinate()
    }
    property bool _prevValid:        _previousCoord && _previousCoord.isValid && missionItem.coordinate.isValid
    property real _bearingFromPrev:  _prevValid ? _previousCoord.azimuthTo(missionItem.coordinate) : 0
    property real _legDistance:      _prevValid ? _previousCoord.distanceTo(missionItem.coordinate) : 0
    property real _displayLegDistance: QGroundControl.unitsConversion.metersToAppSettingsHorizontalDistanceUnits(_legDistance)

    /// Moves the item so its leg from the previous waypoint has the given bearing and length.
    function _applyLeg(bearingText, distanceText) {
        var bearing  = parseFloat(bearingText)
        var distance = QGroundControl.unitsConversion.appSettingsHorizontalDistanceUnitsToMeters(parseFloat(distanceText))
        if (!_prevValid || isNaN(bearing) || isNaN(distance) || distance < 0) {
            return
        }
        missionItem.coordinate = _previousCoord.atDistanceAndAzimuth(distance, bearing)
    }

    /// Moves the item to a bearing and distance from the planned home. Same destination as the
    /// north/east pair above and the same guards; only the way of saying it differs.
    function _applyPolar(bearingText, distanceText) {
        var bearing  = parseFloat(bearingText)
        var distance = QGroundControl.unitsConversion.appSettingsHorizontalDistanceUnitsToMeters(parseFloat(distanceText))
        if (!_homeValid || isNaN(bearing) || isNaN(distance) || distance < 0) {
            return
        }
        missionItem.coordinate = _plannedHome.atDistanceAndAzimuth(distance, bearing)
    }

    /// Moves the item to the given offsets from the planned home, taking the user's distance unit.
    /// Rejects anything non-finite so a half-typed or cleared field cannot throw the waypoint to an
    /// undefined position.
    function _applyOffsets(northText, eastText) {
        var north = QGroundControl.unitsConversion.appSettingsHorizontalDistanceUnitsToMeters(parseFloat(northText))
        var east  = QGroundControl.unitsConversion.appSettingsHorizontalDistanceUnitsToMeters(parseFloat(eastText))
        if (!_homeValid || isNaN(north) || isNaN(east)) {
            return
        }
        var distance = Math.sqrt((north * north) + (east * east))
        var azimuth  = Math.atan2(east, north) * 180 / Math.PI
        missionItem.coordinate = _plannedHome.atDistanceAndAzimuth(distance, azimuth)
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: root.enabled }

    Column {
        id: editorColumn
        anchors.margins: _margin
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: _margin

        // Takeoff item
        ColumnLayout {
            anchors.margins: _margin
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: _margin
            visible: missionItem.isTakeoffItem && missionItem.wizardMode // Hack special case for takeoff item

            QGCLabel {
                text: qsTr("Move '%1' %2 to the %3 location. %4")
                    .arg(_controllerVehicle.vtol ? qsTr("T") : qsTr("T"))
                    .arg(_controllerVehicle.vtol ? qsTr("Transition Direction") : qsTr("Takeoff"))
                    .arg(_controllerVehicle.vtol ? qsTr("desired") : qsTr("climbout"))
                    .arg(_controllerVehicle.vtol ? (qsTr("Ensure distance from launch to transition direction is far enough to complete transition.")) : "")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: !initialClickLabel.visible
            }

            QGCLabel {
                text: qsTr("Ensure clear of obstacles and into the wind.")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: !initialClickLabel.visible
            }

            QGCButton {
                text: qsTr("Done")
                Layout.fillWidth: true
                visible: !initialClickLabel.visible
                onClicked: {
                    missionItem.wizardMode = false
                }
            }

            QGCLabel {
                id: initialClickLabel
                text: missionItem.launchTakeoffAtSameLocation ?
                                        qsTr("Click in map to set planned Takeoff location.") :
                                        qsTr("Click in map to set planned Launch location.")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: missionItem.isTakeoffItem && !missionItem.launchCoordinate.isValid
            }
        }

        ColumnLayout {
            width: parent.width
            spacing: _fieldSpacing
            visible: !missionItem.wizardMode

            QGCTabBar {
                id: tabBar
                Layout.fillWidth: true
                visible: _multipleTabsVisible()

                property bool showBasicItems:    tabBar.visible ? tabBar.currentIndex === 0 : _basicItemsAvailable
                property bool showCameraItems:   tabBar.visible ? tabBar.currentIndex === 1 : _cameraAvailable
                property bool showAdvancedItems: tabBar.visible ? tabBar.currentIndex === 2 : _advancedItemsAvailable

                property bool _basicItemsAvailable: _specifiesAltitude || missionItem.speedSection.available || missionItem.comboboxFacts.count > 0 || missionItem.textFieldFacts.count > 0 || missionItem.nanFacts.count > 0
                property bool _advancedItemsAvailable: missionItem.comboboxFactsAdvanced.count > 0 || missionItem.textFieldFactsAdvanced.count > 0 || missionItem.nanFactsAdvanced.count > 0
                property bool _cameraAvailable: missionItem.cameraSection.available

                function _multipleTabsVisible() {
                    let visibleCount = 0
                    if (_basicItemsAvailable) visibleCount++
                    if (_cameraAvailable) visibleCount++
                    if (_advancedItemsAvailable) visibleCount++
                    return visibleCount > 1
                }

                Component.onCompleted: {
                    if (_basicItemsAvailable) {
                        tabBar.currentIndex = 0
                    } else if (_cameraAvailable) {
                        tabBar.currentIndex = 1
                    } else if (_advancedItemsAvailable) {
                        tabBar.currentIndex = 2
                    } else {
                        tabBar.currentIndex = -1
                    }
                }

                QGCTabButton {
                    id: basicItemsTab
                    icon.source: "/res/PlanSimpleItemBasic.svg"
                    visible: tabBar._basicItemsAvailable
                }

                QGCTabButton {
                    id: cameraTab
                    icon.source: "/res/PlanSimpleItemCamera.svg"
                    visible: tabBar._cameraAvailable
                }

                QGCTabButton {
                    id: advancedItemsTab
                    icon.source: "/res/PlanSimpleItemAdvanced.svg"
                    visible: tabBar._advancedItemsAvailable
                }
            }
            // QGC otherwise offers only latitude and longitude, so placing a waypoint a known
            // distance away means working out decimal degrees by hand -- which is how a vehicle
            // navigating by dead reckoning is actually flown and measured.
            SectionHeader {
                id:                 positionSection
                Layout.fillWidth:   true
                text:               qsTr("Position From Home")
                visible:            tabBar.showBasicItems && missionItem.specifiesCoordinate && root._homeValid
            }

            GridLayout {
                Layout.fillWidth:   true
                columns:            2
                columnSpacing:      _fieldSpacing
                rowSpacing:         _fieldSpacing
                visible:            positionSection.visible && positionSection.checked

                QGCLabel { text: qsTr("North"); Layout.alignment: Qt.AlignRight }
                QGCTextField {
                    id:                 northField
                    Layout.fillWidth:   true
                    text:               root._displayNorth.toFixed(1)
                    onEditingFinished:  root._applyOffsets(northField.text, eastField.text)
                }

                QGCLabel { text: qsTr("East"); Layout.alignment: Qt.AlignRight }
                QGCTextField {
                    id:                 eastField
                    Layout.fillWidth:   true
                    text:               root._displayEast.toFixed(1)
                    onEditingFinished:  root._applyOffsets(northField.text, eastField.text)
                }

                // The same point said the other way round. A vehicle flying without a map is
                // briefed as "bearing 180, twenty metres", and a compass bearing is what the
                // operator can check against the field they are standing in.
                QGCLabel { text: qsTr("Bearing"); Layout.alignment: Qt.AlignRight }
                QGCTextField {
                    id:                 bearingField
                    Layout.fillWidth:   true
                    text:               root._bearingFromHome.toFixed(1)
                    onEditingFinished:  root._applyPolar(bearingField.text, distanceField.text)
                }

                QGCLabel { text: qsTr("Distance"); Layout.alignment: Qt.AlignRight }
                QGCTextField {
                    id:                 distanceField
                    Layout.fillWidth:   true
                    text:               root._displayDistance.toFixed(1)
                    onEditingFinished:  root._applyPolar(bearingField.text, distanceField.text)
                }
            }

            // The leg that reaches this waypoint, rather than its place in the plan. A route without
            // a map is built one leg at a time -- "from there, ninety degrees for twenty metres" --
            // and each leg is what the vehicle actually flies. Bearings stay measured from north, so
            // they can be read straight off a compass; only the point they start from differs.
            SectionHeader {
                id:                 legSection
                Layout.fillWidth:   true
                text:               qsTr("Leg From Previous")
                visible:            tabBar.showBasicItems && missionItem.specifiesCoordinate && root._prevValid
            }

            GridLayout {
                Layout.fillWidth:   true
                columns:            2
                columnSpacing:      _fieldSpacing
                rowSpacing:         _fieldSpacing
                visible:            legSection.visible && legSection.checked

                QGCLabel { text: qsTr("Bearing"); Layout.alignment: Qt.AlignRight }
                QGCTextField {
                    id:                 legBearingField
                    Layout.fillWidth:   true
                    text:               root._bearingFromPrev.toFixed(1)
                    onEditingFinished:  root._applyLeg(legBearingField.text, legDistanceField.text)
                }

                QGCLabel { text: qsTr("Distance"); Layout.alignment: Qt.AlignRight }
                QGCTextField {
                    id:                 legDistanceField
                    Layout.fillWidth:   true
                    text:               root._displayLegDistance.toFixed(1)
                    onEditingFinished:  root._applyLeg(legBearingField.text, legDistanceField.text)
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: _fieldSpacing
                visible: tabBar.showBasicItems

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: _fieldSpacing
                    visible: _specifiesAltitude

                    RowLayout {
                        Layout.fillWidth: true
                        visible: _globalAltFrameIsMixed

                        QGCLabel {
                            Layout.fillWidth: true
                            text: qsTr("Alt Frame")
                        }

                        AltFrameCombo {
                            altitudeFrame: missionItem.altitudeFrame
                            vehicle: _controllerVehicle
                            onAltitudeFrameChanged: missionItem.altitudeFrame = altitudeFrame
                        }
                    }

                    FactTextFieldSlider {
                        id: altField
                        Layout.fillWidth: true
                        label: qsTr("Altitude%1").arg(_extraLabelText())
                        fact: missionItem.altitude

                        function _extraLabelText() {
                            return qsTr(" (%1)").arg(QGroundControl.altitudeFrameExtraUnits(missionItem.altitudeFrame))
                        }
                    }

                    QGCLabel {
                        font.pointSize: ScreenTools.smallFontPointSize
                        text: qsTr("Actual AMSL alt sent: %1 %2").arg(missionItem.amslAltAboveTerrain.valueString).arg(missionItem.amslAltAboveTerrain.units)
                        visible: missionItem.altitudeFrame === QGroundControl.AltitudeFrameCalcAboveTerrain
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: _fieldSpacing

                    Repeater {
                        model: missionItem.comboboxFacts

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            QGCLabel {
                                font.pointSize: ScreenTools.smallFontPointSize
                                text: object.name
                                visible: object.name !== ""
                            }

                            FactComboBox {
                                Layout.fillWidth: true
                                indexModel: false
                                model: object.enumStrings
                                fact: object
                            }
                        }
                    }
                }

                Repeater {
                    model: missionItem.textFieldFacts

                    FactTextFieldSlider {
                        Layout.fillWidth: true
                        label: object.name
                        fact: object
                        enabled: !object.readOnly
                        warnOnUserMinMaxInvalid: false
                    }
                }

                Repeater {
                    model: missionItem.nanFacts

                    FactTextFieldSlider {
                        Layout.fillWidth: true
                        label: object.name
                        fact: object
                        showEnableCheckbox: true
                        enableCheckBoxChecked: !isNaN(object.rawValue)
                        warnOnUserMinMaxInvalid: false

                        onEnableCheckboxClicked: object.rawValue = enableCheckBoxChecked ? 0 : NaN
                    }
                }

                FactTextFieldSlider {
                    Layout.fillWidth: true
                    label: qsTr("Flight Speed")
                    fact: missionItem.speedSection.flightSpeed
                    showEnableCheckbox: true
                    enableCheckBoxChecked: missionItem.speedSection.specifyFlightSpeed
                    visible: missionItem.speedSection.available

                    onEnableCheckboxClicked: missionItem.speedSection.specifyFlightSpeed = enableCheckBoxChecked
                }
            }

            CameraSection {
                Layout.fillWidth: true
                showSectionHeader: false
                missionItem: root.missionItem
                visible: tabBar.showCameraItems

                Component.onCompleted: checked = missionItem.cameraSection.settingsSpecified
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: _fieldSpacing
                visible: tabBar.showAdvancedItems

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: _fieldSpacing

                    Repeater {
                        model: missionItem.comboboxFactsAdvanced

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            QGCLabel {
                                font.pointSize: ScreenTools.smallFontPointSize
                                text: object.name
                                visible: object.name !== ""
                            }

                            FactComboBox {
                                Layout.fillWidth: true
                                indexModel: false
                                model: object.enumStrings
                                fact: object
                            }
                        }
                    }
                }

                Repeater {
                    model: missionItem.textFieldFactsAdvanced

                    FactTextFieldSlider {
                        Layout.fillWidth: true
                        label: object.name
                        fact: object
                        enabled: !object.readOnly
                        warnOnUserMinMaxInvalid: false
                    }
                }

                Repeater {
                    model: missionItem.nanFactsAdvanced

                    FactTextFieldSlider {
                        Layout.fillWidth: true
                        label: object.name
                        fact: object
                        showEnableCheckbox: true
                        enableCheckBoxChecked: !isNaN(object.rawValue)
                        warnOnUserMinMaxInvalid: false

                        onEnableCheckboxClicked: object.rawValue = enableCheckBoxChecked ? 0 : NaN
                    }
                }
            }
        }
    }
}
