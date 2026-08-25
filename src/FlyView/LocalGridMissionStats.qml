import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// What the plan on the grid costs to fly: how far, how long, how many items.
///
/// The numbers come from MissionController's own flight-status calculation rather than from
/// measuring across the grid's points, so a plan holding items the grid cannot draw is still
/// measured correctly. That calculation is not one of the passes QGC skips in the fly view, and it
/// reads each waypoint's own speed -- which this grid writes onto every waypoint it places. So these
/// describe the plan that was drawn rather than a guess from the vehicle's parameters.
///
/// Sized like the panels above it and folded away by default. A plan's total is read once while
/// building it and then not again, so it does not earn standing room on a grid the operator is
/// flying from.
Rectangle {
    id: _root

    property var gridView: null

    /// Collapsed to nothing rather than merely hidden, so the panel below closes the gap. An
    /// invisible item keeps its height in QML.
    visible:        _hasPlan
    implicitWidth:  visible ? layout.implicitWidth + (_margins * 2) : 0
    implicitHeight: visible ? layout.implicitHeight + (_margins * 2) : 0

    color:          qgcPal.window
    opacity:        0.8
    radius:         ScreenTools.defaultFontPixelHeight / 4
    border.color:   qgcPal.groupBorder
    border.width:   1

    property real _margins: ScreenTools.defaultFontPixelHeight / 3

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    /// Folded away by default, and folded again by the compact rule the rest of the overlay follows,
    /// so a phone does not spend its width on a number read once per plan.
    property bool collapsed: true

    /// What this panel takes while folded, so the panels above it in the column can reserve room for
    /// it without asking how tall its contents would be -- which is what would close the loop, since
    /// its own position comes from theirs.
    readonly property real collapsedHeight: visible ? (headerBlock.implicitHeight + (_margins * 2)) : 0

    readonly property var  _transform: gridView ? gridView.gridTransform : null
    readonly property int  _itemCount: gridView ? gridView.missionPoints.length : 0
    readonly property bool _hasPlan:   _itemCount > 0
    readonly property bool _known:     gridView ? gridView.missionStatsKnown : false

    readonly property real _distanceMetres: gridView ? gridView.missionDistanceMetres : 0
    readonly property real _durationSeconds: gridView ? gridView.missionDurationSeconds : 0
    readonly property real _holdSeconds: gridView ? gridView.missionHoldSeconds : 0

    function _distanceText() {
        if (!_known || !_transform) {
            return qsTr("--")
        }
        return _transform.toDisplay(_distanceMetres).toFixed(0) + " " + _transform.displayUnits
    }

    /// Minutes and seconds rather than a bare count of seconds. A plan is sized against a battery,
    /// and "7:20" is the form that comparison is made in.
    function _durationText() {
        if (!_known || (_durationSeconds <= 0)) {
            return qsTr("--")
        }
        const total = Math.round(_durationSeconds)
        const minutes = Math.floor(total / 60)
        const seconds = total % 60
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    ColumnLayout {
        id:                 layout
        anchors.margins:    _root._margins
        anchors.left:       parent.left
        anchors.right:      parent.right
        anchors.top:        parent.top
        spacing:            ScreenTools.defaultFontPixelHeight / 6

        // The header stays visible while the panel is folded, so there is a way back to it. Same
        // idiom as the readout and the plan list above.
        Item {
            id:                 headerBlock
            Layout.fillWidth:   true
            implicitWidth:      headerRow.implicitWidth
            implicitHeight:     headerRow.implicitHeight

            RowLayout {
                id:         headerRow
                spacing:    ScreenTools.defaultFontPixelWidth / 2

                QGCColoredImage {
                    Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight * 0.6
                    Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 0.6
                    sourceSize.height:      ScreenTools.defaultFontPixelHeight * 0.6
                    fillMode:               Image.PreserveAspectFit
                    mipmap:                 true
                    smooth:                 true
                    color:                  qgcPal.text
                    source:                 _root.collapsed ? "/InstrumentValueIcons/cheveron-right.svg"
                                                            : "/InstrumentValueIcons/cheveron-down.svg"
                }

                QGCLabel {
                    font.pointSize: ScreenTools.smallFontPointSize
                    font.bold:      true
                    text:           qsTr("Plan Totals")
                }
            }

            QGCMouseArea {
                objectName: "localGrid_missionStatsHeader"
                fillItem:   parent
                onClicked:  _root.collapsed = !_root.collapsed
            }
        }

        GridLayout {
            objectName:         "localGrid_missionStatsNumbers"
            Layout.fillWidth:   true
            visible:            !_root.collapsed
            columns:            2
            columnSpacing:      ScreenTools.defaultFontPixelWidth
            rowSpacing:         0

            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Distance") }
            QGCLabel {
                objectName:             "localGrid_missionStatsDistance"
                font.pointSize:         ScreenTools.smallFontPointSize
                horizontalAlignment:    Text.AlignRight
                Layout.fillWidth:       true
                text:                   _root._distanceText()
            }

            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Time") }
            QGCLabel {
                objectName:             "localGrid_missionStatsDuration"
                font.pointSize:         ScreenTools.smallFontPointSize
                horizontalAlignment:    Text.AlignRight
                Layout.fillWidth:       true
                text:                   _root._durationText()
            }

            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Items") }
            QGCLabel {
                font.pointSize:         ScreenTools.smallFontPointSize
                horizontalAlignment:    Text.AlignRight
                Layout.fillWidth:       true
                text:                   _root._itemCount
            }
        }

        // Said only when there is a wait to say it about. QGC's own calculation has no hold term, so
        // the time above is the flying plus the waits added on here -- worth stating, because it is
        // the one number on this panel that does not come from QGC as-is.
        QGCLabel {
            objectName:             "localGrid_missionStatsHoldNote"
            Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 24
            visible:                !_root.collapsed && (_root._holdSeconds > 0)
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.colorGrey
            text:                   qsTr("Includes %1 s of waypoint waits.").arg(Math.round(_root._holdSeconds))
        }
    }
}
