import QtQuick

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView

/// A plan of the ground in the estimator's own frame: metres north and east of the origin, drawn as
/// a measured grid rather than as a map.
///
/// A vehicle navigating without GNSS has no meaningful latitude to plot. Its position is an
/// integration of velocity since the origin was set, and the question the operator actually needs
/// answered -- how far, and in what direction, from where we started -- is one a world map answers
/// badly and this answers directly. The grid is north-up, so a bearing read off it is a bearing that
/// can be flown.
Item {
    id: _root

    property var vehicle: null

    /// Keeps the vehicle in the middle of the view. Dragging turns it off, since the operator has
    /// then said where they want to look.
    property bool followVehicle: true

    readonly property bool positionValid: _localPosition
                                            ? (_localPosition.telemetryAvailable && !isNaN(_north) && !isNaN(_east))
                                            : false
    readonly property real vehicleNorth: _north
    readonly property real vehicleEast:  _east

    property var  _localPosition:   vehicle ? vehicle.localPosition : null
    property real _north:           _localPosition ? _localPosition.x.rawValue : NaN
    property real _east:            _localPosition ? _localPosition.y.rawValue : NaN
    property real _headingDegrees:  vehicle ? vehicle.heading.rawValue : NaN

    property real _margins:         ScreenTools.defaultFontPixelHeight / 2
    /// Roughly how far apart grid lines should sit before the spacing is rounded to a countable one
    property real _targetGridPixels: ScreenTools.defaultFontPixelHeight * 4

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    readonly property color _gridMinorColor: Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.15)
    readonly property color _gridMajorColor: Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.35)
    readonly property color _axisColor:      Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.7)

    LocalGridTransform {
        id:         transform
        viewWidth:  _root.width
        viewHeight: _root.height

        onCentreNorthChanged:   _root._repaintAll()
        onCentreEastChanged:    _root._repaintAll()
        onMetresPerPixelChanged: _root._repaintAll()
        onViewWidthChanged:     _root._repaintAll()
        onViewHeightChanged:    _root._repaintAll()
    }

    /// Exposed so the view can be driven from tests and from the surrounding fly view
    readonly property alias gridTransform: transform

    onWidthChanged:  _fitIfUnstarted()
    onHeightChanged: _fitIfUnstarted()
    Component.onCompleted: _fitIfUnstarted()

    on_NorthChanged:        _followAndRepaint()
    on_EastChanged:         _followAndRepaint()
    on_HeadingDegreesChanged: vehicleCanvas.requestPaint()

    property bool _hasBeenFitted: false

    /// Starts on a box a little larger than the project's own 20 m test pattern, so the first frame
    /// shows a usable amount of ground instead of either a single square or the whole county.
    function _fitIfUnstarted() {
        if (_hasBeenFitted || (width <= 0) || (height <= 0)) {
            return
        }
        transform.zoomToFit(40)
        _hasBeenFitted = true
        _repaintAll()
    }

    function _followAndRepaint() {
        if (followVehicle && positionValid) {
            transform.centreOn(_north, _east)
        }
        vehicleCanvas.requestPaint()
    }

    function _repaintAll() {
        gridCanvas.requestPaint()
        vehicleCanvas.requestPaint()
    }

    function centreOnVehicle() {
        if (positionValid) {
            transform.centreOn(_north, _east)
        }
        followVehicle = true
    }

    function centreOnOrigin() {
        transform.centreOn(0, 0)
        followVehicle = false
    }

    Rectangle {
        anchors.fill: parent
        color:        qgcPal.window
    }

    /// The grid, its labels and the origin. Repainted only when the view moves, so telemetry
    /// arriving at 10 Hz does not redraw a screenful of lines and text with it.
    Canvas {
        id:             gridCanvas
        anchors.fill:   parent

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()

            const step = transform.gridStepMetres(_root._targetGridPixels)
            if (!(step > 0)) {
                return
            }

            ctx.font = ScreenTools.smallFontPointSize + "pt sans-serif"
            ctx.textBaseline = "middle"

            // Every fifth line is drawn heavier and labelled, which is what makes squares countable
            // at a glance without labelling every one of them
            const majorEvery = 5

            const westEdge = transform.eastForPixelX(0)
            const eastEdge = transform.eastForPixelX(width)
            const southEdge = transform.northForPixelY(height)
            const northEdge = transform.northForPixelY(0)

            var index = Math.floor(westEdge / step)
            for (var east = index * step; east <= eastEdge; east += step, index++) {
                const x = transform.pixelXForEast(east)
                const isMajor = (index % majorEvery) === 0
                _root._drawLine(ctx, x, 0, x, height,
                                east === 0 ? _root._axisColor : (isMajor ? _root._gridMajorColor : _root._gridMinorColor),
                                east === 0 ? 2 : 1)
                if (isMajor) {
                    ctx.fillStyle = _root._axisColor
                    ctx.textAlign = "center"
                    ctx.fillText(_root._label(east), x, height - (_root._margins * 2))
                }
            }

            index = Math.floor(southEdge / step)
            for (var north = index * step; north <= northEdge; north += step, index++) {
                const y = transform.pixelYForNorth(north)
                const isMajorRow = (index % majorEvery) === 0
                _root._drawLine(ctx, 0, y, width, y,
                                north === 0 ? _root._axisColor : (isMajorRow ? _root._gridMajorColor : _root._gridMinorColor),
                                north === 0 ? 2 : 1)
                if (isMajorRow) {
                    ctx.fillStyle = _root._axisColor
                    ctx.textAlign = "left"
                    ctx.fillText(_root._label(north), _root._margins, y - (_root._margins / 2))
                }
            }

            _root._drawOrigin(ctx)
        }
    }

    /// The vehicle, redrawn on every position or attitude update
    Canvas {
        id:             vehicleCanvas
        anchors.fill:   parent

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()

            if (!_root.positionValid) {
                return
            }

            const x = transform.pixelXForEast(_root._east)
            const y = transform.pixelYForNorth(_root._north)
            _root._drawVehicle(ctx, x, y, _root._headingDegrees)
        }
    }

    function _label(metres) {
        // Zero is the origin, which is marked rather than numbered
        if (Math.abs(metres) < 1e-6) {
            return ""
        }
        return transform.toDisplay(metres).toFixed(0)
    }

    function _drawLine(ctx, x1, y1, x2, y2, colour, lineWidth) {
        ctx.beginPath()
        ctx.strokeStyle = colour
        ctx.lineWidth = lineWidth
        ctx.moveTo(x1, y1)
        ctx.lineTo(x2, y2)
        ctx.stroke()
    }

    function _drawOrigin(ctx) {
        const x = transform.pixelXForEast(0)
        const y = transform.pixelYForNorth(0)
        const radius = ScreenTools.defaultFontPixelHeight / 2

        ctx.beginPath()
        ctx.strokeStyle = qgcPal.colorGreen
        ctx.lineWidth = 2
        ctx.arc(x, y, radius, 0, 2 * Math.PI)
        ctx.stroke()

        ctx.beginPath()
        ctx.fillStyle = qgcPal.colorGreen
        ctx.arc(x, y, radius / 3, 0, 2 * Math.PI)
        ctx.fill()

        ctx.fillStyle = qgcPal.colorGreen
        ctx.textAlign = "left"
        ctx.fillText(qsTr("ORIGIN"), x + (radius * 1.5), y - radius)
    }

    /// A triangle pointing where the nose points. Heading is a compass bearing, so it is turned into
    /// a screen angle here rather than anywhere the reader has to hold both conventions at once.
    function _drawVehicle(ctx, x, y, headingDegrees) {
        const size = ScreenTools.defaultFontPixelHeight * 0.9
        const heading = isNaN(headingDegrees) ? 0 : headingDegrees

        ctx.save()
        ctx.translate(x, y)
        // North is up and bearings run clockwise, which is exactly how canvas rotation runs once the
        // origin is at the vehicle -- so the bearing goes in unmodified.
        ctx.rotate(heading * Math.PI / 180)

        ctx.beginPath()
        ctx.moveTo(0, -size)
        ctx.lineTo(size * 0.6, size * 0.7)
        ctx.lineTo(0, size * 0.35)
        ctx.lineTo(-size * 0.6, size * 0.7)
        ctx.closePath()

        ctx.fillStyle = isNaN(headingDegrees) ? qgcPal.colorOrange : qgcPal.colorBlue
        ctx.fill()
        ctx.strokeStyle = qgcPal.text
        ctx.lineWidth = 1
        ctx.stroke()

        ctx.restore()
    }

    // Wheel to zoom, drag to pan. Zooming about the cursor rather than the centre keeps whatever is
    // being examined under the pointer.
    MouseArea {
        id:             dragArea
        anchors.fill:   parent
        acceptedButtons: Qt.LeftButton

        property real _lastX: 0
        property real _lastY: 0

        onPressed: (mouse) => {
            _lastX = mouse.x
            _lastY = mouse.y
        }

        onPositionChanged: (mouse) => {
            if (!pressed) {
                return
            }
            transform.panByPixels(mouse.x - _lastX, mouse.y - _lastY)
            _lastX = mouse.x
            _lastY = mouse.y
            _root.followVehicle = false
        }

        onWheel: (wheel) => {
            // A notch is 120 units; one notch is a fifth in or out, which is a comfortable step
            const notches = wheel.angleDelta.y / 120
            transform.zoomBy(Math.pow(0.8, notches), wheel.x, wheel.y)
        }
    }

    PinchArea {
        anchors.fill:   parent
        enabled:        true

        property real _previousScale: 1

        onPinchStarted: { _previousScale = 1 }
        onPinchUpdated: (pinch) => {
            if (pinch.scale <= 0) {
                return
            }
            transform.zoomBy(_previousScale / pinch.scale, pinch.center.x, pinch.center.y)
            _previousScale = pinch.scale
            _root.followVehicle = false
        }
    }

    LocalGridCompassRose {
        id:                 compassRose
        anchors.right:      parent.right
        anchors.top:        parent.top
        anchors.margins:    _root._margins
        headingDegrees:     _root._headingDegrees
        diameter:           ScreenTools.defaultFontPixelHeight * 8
    }

    LocalGridScaleBar {
        anchors.left:       parent.left
        anchors.bottom:     parent.bottom
        anchors.margins:    _root._margins
        gridTransform:      transform
    }

    LocalGridReadout {
        anchors.left:       parent.left
        anchors.top:        parent.top
        anchors.margins:    _root._margins
        gridView:           _root
    }
}
