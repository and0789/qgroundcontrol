import QtQuick

import QGroundControl
import QGroundControl.Controls

/// One mission waypoint on the local grid: a numbered marker that can be picked up and moved.
///
/// Drawn as an item rather than painted into the grid canvas because a painted waypoint cannot be
/// pointed at. Selecting and moving one is the difference between a picture of the plan and a plan
/// that can be worked on where it will be flown.
Item {
    id: _root

    /// Index into the mission's visual items -- not the sequence number shown on the face. Removing
    /// and reordering are done by index, and the two diverge as soon as the plan holds anything that
    /// is not a plain waypoint.
    property int  visualItemIndex: -1
    property int  sequenceNumber:  0

    /// The waypoint the vehicle is flying to, which is not the one the operator has selected to
    /// edit. Named for what it is: the plan's own isCurrentItem flag also gets set when an item is
    /// inserted, so it cannot be trusted to mean this.
    property bool isVehicleTarget: false
    property bool isSelected:      false

    /// False for a marker that is anchored where it is -- the takeoff, which belongs on the origin.
    /// It can still be picked to be edited; only the dragging is refused.
    property bool draggable:       true

    /// The view this marker sits on, used to turn a pointer position back into metres. Passed in
    /// rather than reached for through parent, so the marker states what it depends on.
    property var  gridView: null

    /// How far the pointer must travel before a press becomes a move rather than a pick. Without it
    /// every selection nudges the waypoint it selected.
    property real dragThreshold: ScreenTools.defaultFontPixelWidth

    signal selected()
    /// Emitted continuously while dragging, in metres north and east of the origin
    signal movedTo(real north, real east)

    implicitWidth:  _diameter
    implicitHeight: _diameter

    readonly property real _diameter: ScreenTools.defaultFontPixelHeight * 1.6

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    Rectangle {
        anchors.fill:   parent
        radius:         width / 2
        color:          _root.isVehicleTarget ? qgcPal.colorGreen : qgcPal.colorOrange
        // The selected one is outlined rather than recoloured, so it can be seen which waypoint is
        // being worked on without losing which one the vehicle is flying to
        border.color:   qgcPal.text
        border.width:   _root.isSelected ? 2 : 0

        QGCLabel {
            anchors.centerIn:   parent
            font.pointSize:     ScreenTools.smallFontPointSize
            color:              qgcPal.window
            text:               _root.sequenceNumber
        }
    }

    MouseArea {
        anchors.fill:       parent
        // A little beyond the marker, so it can still be grabbed on a trackpad at arm's length in
        // the field
        anchors.margins:    -ScreenTools.defaultFontPixelWidth / 2
        preventStealing:    true

        property real _pressX:      0
        property real _pressY:      0
        property bool _isDragging:  false

        onPressed: (mouse) => {
            _pressX = mouse.x
            _pressY = mouse.y
            _isDragging = false
            _root.selected()
        }

        onPositionChanged: (mouse) => {
            if (!pressed || !_root.draggable) {
                return
            }
            if (!_isDragging
                    && ((Math.abs(mouse.x - _pressX) + Math.abs(mouse.y - _pressY)) < _root.dragThreshold)) {
                return
            }
            _isDragging = true

            if (!_root.gridView) {
                return
            }

            // Reported as a position under the cursor rather than as a delta applied to the marker.
            // The marker is itself following the waypoint being moved, so accumulating deltas
            // against it would chase its own tail.
            const viewPoint = mapToItem(_root.gridView, mouse.x, mouse.y)
            const transform = _root.gridView.gridTransform
            _root.movedTo(transform.northForPixelY(viewPoint.y), transform.eastForPixelX(viewPoint.x))
        }
    }
}
