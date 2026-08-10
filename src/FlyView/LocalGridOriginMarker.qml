import QtQuick

import QGroundControl
import QGroundControl.Controls

/// The point the grid is measured from, drawn as something that can be pointed at.
///
/// It was painted into the grid canvas before, and a painted marker cannot be clicked. The click is
/// the reason this exists: the origin is the one place on the whole grid an operator can put the
/// aircraft on by hand and be sure of, so "it is standing on the origin" is the position correction
/// they will reach for after carrying a drifted aircraft back to where it took off from.
///
/// Only the label takes the click. The ring is left alone because a plan's takeoff marker is pinned to
/// the origin and sits directly over it, and a hit target under that marker would be one the operator
/// can reach on an empty plan and not on a real one.
Item {
    id: _root

    /// True when there is an origin to correct a position against. The marker is still drawn without
    /// one -- the grid's (0,0) is where it is either way -- but there is nothing to claim.
    property bool originKnown: false

    signal correctPositionRequested()

    /// Where inside this item the ring's centre sits. The view lines this up with the pixel the grid
    /// puts (0,0) at, rather than the item's corner, which is what keeps the ring on the crossing of
    /// the two zero lines while the label hangs off to the side of it.
    readonly property real centreX: _diameter / 2
    readonly property real centreY: height / 2

    implicitWidth:  _diameter + _labelGap + label.implicitWidth
    implicitHeight: Math.max(_diameter, label.implicitHeight)

    readonly property real _diameter: ScreenTools.defaultFontPixelHeight
    readonly property real _labelGap: ScreenTools.defaultFontPixelWidth / 2

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    Rectangle {
        id:                     ring
        anchors.verticalCenter: parent.verticalCenter
        x:                      0
        width:                  _root._diameter
        height:                 _root._diameter
        radius:                 width / 2
        color:                  "transparent"
        border.color:           qgcPal.colorGreen
        border.width:           2

        Rectangle {
            anchors.centerIn:   parent
            width:              _root._diameter / 3
            height:             width
            radius:             width / 2
            color:              qgcPal.colorGreen
        }
    }

    QGCLabel {
        id:                     label
        objectName:             "localGrid_originMarkerLabel"
        anchors.left:           ring.right
        anchors.leftMargin:     _root._labelGap
        anchors.verticalCenter: ring.verticalCenter
        color:                  qgcPal.colorGreen
        // The only thing marking this as more than a caption. There is no room beside it for a button
        // and nothing on the grid to open a menu from, so the label has to carry its own invitation.
        font.underline:         _root.originKnown
        text:                   qsTr("ORIGIN")

        MouseArea {
            anchors.fill:       parent
            // Reachable on a trackpad at arm's length in the field, which is where this is used
            anchors.margins:    -ScreenTools.defaultFontPixelWidth / 2
            enabled:            _root.originKnown
            onClicked:          _root.correctPositionRequested()
        }
    }
}
