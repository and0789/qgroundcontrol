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
    ///
    /// Measured against the size of a finger rather than the size of a letter. A font width is
    /// around a millimetre and a half on a phone, and a finger never holds still to within that --
    /// so on a touch screen every attempt to select a waypoint was already a drag by the time the
    /// press was released, and the plan moved a little each time it was read. Half a touch target is
    /// the honest line: a press that has not travelled half the size of the control it started on
    /// has not left it, and is still a tap.
    property real dragThreshold: ScreenTools.minTouchPixels / 2

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

    // QGCMouseArea rather than a plain one, which is the whole of the fix here: given a fillItem it
    // already grows the press area to ScreenTools.minTouchPixels on a touch build, and it draws the
    // area when showTouchAreas is on so the result can be looked at rather than trusted. Every other
    // control on this grid was already using it -- the panel headers, the row's delete icon -- and
    // this marker, the one thing on the grid that is aimed at most often, was the exception.
    //
    // The disc itself is deliberately not grown with it. The two are different problems: a marker
    // has to stay small enough that a twenty-metre pattern still reads as a pattern rather than as a
    // row of overlapping blobs, while the area that takes the press has to be reachable by a gloved
    // finger. Sizing them together forced a choice between a legible grid and a usable one.
    //
    // Where two markers sit closer together than a touch target their areas overlap, and the one
    // drawn on top takes the press -- which is the selected one, since selection raises z. The way
    // out is the same as on any map: zoom in, and they separate.
    QGCMouseArea {
        objectName:         "localGrid_waypointTouchArea"
        fillItem:           parent
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
