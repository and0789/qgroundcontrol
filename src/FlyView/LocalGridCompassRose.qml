import QtQuick

import QGroundControl
import QGroundControl.Controls

/// A bearing protractor for the local grid, with a needle showing where the nose points.
///
/// The grid itself is north-up and never rotates, so this is not there to say which way is north --
/// it is there so a bearing can be read off the picture and flown, and so the heading the estimator
/// is steering by can be seen against it. Bearings run clockwise from north, the way they are
/// briefed and the way the autopilot takes them.
Item {
    id: _root

    property real headingDegrees: NaN
    property real diameter:       ScreenTools.defaultFontPixelHeight * 8

    implicitWidth:  diameter
    implicitHeight: diameter

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    onHeadingDegreesChanged: canvas.requestPaint()
    onDiameterChanged:       canvas.requestPaint()

    Canvas {
        id:             canvas
        anchors.fill:   parent

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()

            const centreX = width / 2
            const centreY = height / 2
            const radius = (Math.min(width, height) / 2) - (ScreenTools.defaultFontPixelHeight * 0.75)
            if (radius <= 0) {
                return
            }

            ctx.beginPath()
            ctx.fillStyle = Qt.rgba(qgcPal.window.r, qgcPal.window.g, qgcPal.window.b, 0.75)
            ctx.arc(centreX, centreY, radius, 0, 2 * Math.PI)
            ctx.fill()

            ctx.beginPath()
            ctx.strokeStyle = qgcPal.text
            ctx.lineWidth = 1
            ctx.arc(centreX, centreY, radius, 0, 2 * Math.PI)
            ctx.stroke()

            ctx.font = ScreenTools.smallFontPointSize + "pt sans-serif"
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"

            for (var bearing = 0; bearing < 360; bearing += 10) {
                const major = (bearing % 30) === 0
                const tickLength = radius * (major ? 0.18 : 0.09)

                const outer = _root._pointAt(centreX, centreY, radius, bearing)
                const inner = _root._pointAt(centreX, centreY, radius - tickLength, bearing)

                ctx.beginPath()
                ctx.strokeStyle = qgcPal.text
                ctx.lineWidth = major ? 2 : 1
                ctx.moveTo(outer.x, outer.y)
                ctx.lineTo(inner.x, inner.y)
                ctx.stroke()

                if ((bearing % 90) === 0) {
                    const labelPoint = _root._pointAt(centreX, centreY, radius + (ScreenTools.defaultFontPixelHeight * 0.45), bearing)
                    ctx.fillStyle = qgcPal.text
                    ctx.fillText(_root._cardinal(bearing), labelPoint.x, labelPoint.y)
                }
            }

            if (!isNaN(_root.headingDegrees)) {
                const nose = _root._pointAt(centreX, centreY, radius * 0.8, _root.headingDegrees)
                const tail = _root._pointAt(centreX, centreY, radius * 0.25, _root.headingDegrees + 180)

                ctx.beginPath()
                ctx.strokeStyle = qgcPal.colorBlue
                ctx.lineWidth = 3
                ctx.moveTo(tail.x, tail.y)
                ctx.lineTo(nose.x, nose.y)
                ctx.stroke()

                ctx.beginPath()
                ctx.fillStyle = qgcPal.colorBlue
                ctx.arc(nose.x, nose.y, ScreenTools.defaultFontPixelWidth * 0.6, 0, 2 * Math.PI)
                ctx.fill()
            }
        }
    }

    QGCLabel {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter:   parent.verticalCenter
        font.pointSize:           ScreenTools.smallFontPointSize
        text:                     isNaN(_root.headingDegrees) ? qsTr("--") : Math.round(_root.headingDegrees) + "°"
    }

    /// Converts a compass bearing into a point on the rose. Bearings are measured clockwise from
    /// north while canvas angles run anticlockwise from east, so this is where the two conventions
    /// are reconciled -- once, rather than at every call site.
    function _pointAt(centreX, centreY, radius, bearingDegrees) {
        const radians = (bearingDegrees - 90) * Math.PI / 180
        return Qt.point(centreX + (radius * Math.cos(radians)),
                        centreY + (radius * Math.sin(radians)))
    }

    function _cardinal(bearing) {
        switch (bearing) {
        case 0:     return qsTr("N")
        case 90:    return qsTr("E")
        case 180:   return qsTr("S")
        case 270:   return qsTr("W")
        default:    return ""
        }
    }
}
