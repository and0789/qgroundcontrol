import QtQuick

import QGroundControl

/// Maps the vehicle's local NED frame onto a view, and back again.
///
/// The frame is the one the estimator works in: metres north and east of the estimator origin, which
/// is also what LOCAL_POSITION_NED reports. North is up and east is right, so the view is a plan of
/// the field rather than a projection of the globe -- there is no map behind it and no latitude to
/// distort, which is the whole point of flying this way.
///
/// Kept apart from the drawing so the arithmetic can be exercised on its own. A grid that is subtly
/// off by a scale factor still looks like a perfectly good grid.
QtObject {
    id: _root

    /// Size of the view being drawn into, in pixels
    property real viewWidth:    0
    property real viewHeight:   0

    /// The point at the centre of the view, in metres from the origin
    property real centreNorth:  0
    property real centreEast:   0

    /// Zoom, expressed as metres of ground per pixel. Larger means further out.
    property real metresPerPixel: 0.25

    /// Limits chosen from the flying this view is for: in far enough to see a 2 cm position wobble
    /// on a hover, out far enough to hold a 20 km leg on screen.
    readonly property real minMetresPerPixel: 0.01
    readonly property real maxMetresPerPixel: 20

    /// Distances are shown in whatever horizontal unit the operator has chosen, like everywhere else
    /// in QGC. All the arithmetic here stays in metres; only labels convert.
    readonly property real   displayPerMetre: QGroundControl.unitsConversion.metersToAppSettingsHorizontalDistanceUnits(1)
    readonly property string displayUnits:    QGroundControl.unitsConversion.appSettingsHorizontalDistanceUnitsString

    function pixelXForEast(east) {
        return (viewWidth / 2) + ((east - centreEast) / metresPerPixel)
    }

    function pixelYForNorth(north) {
        // Screen y grows downwards and north grows upwards, so this is the one axis that flips
        return (viewHeight / 2) - ((north - centreNorth) / metresPerPixel)
    }

    function eastForPixelX(x) {
        return centreEast + ((x - (viewWidth / 2)) * metresPerPixel)
    }

    function northForPixelY(y) {
        return centreNorth - ((y - (viewHeight / 2)) * metresPerPixel)
    }

    function toDisplay(metres) {
        return metres * displayPerMetre
    }

    function fromDisplay(display) {
        return display / displayPerMetre
    }

    /// Spacing between grid lines, in metres, chosen so the lines land on round numbers in the unit
    /// being displayed rather than on round numbers of pixels. A grid labelled 3.7, 7.4, 11.1 is a
    /// grid nobody can count squares on.
    ///     @param targetPixels roughly how far apart the lines should be on screen
    function gridStepMetres(targetPixels) {
        const roughDisplay = toDisplay(targetPixels * metresPerPixel)
        if (!(roughDisplay > 0) || !isFinite(roughDisplay)) {
            return 0
        }

        const magnitude = Math.pow(10, Math.floor(Math.log(roughDisplay) / Math.LN10))
        const normalised = roughDisplay / magnitude

        // 1, 2, 5, 10 -- the steps a person can add up in their head while watching an aircraft
        var nice
        if (normalised < 1.5) {
            nice = 1
        } else if (normalised < 3.5) {
            nice = 2
        } else if (normalised < 7.5) {
            nice = 5
        } else {
            nice = 10
        }

        return fromDisplay(nice * magnitude)
    }

    function clampZoom(candidate) {
        return Math.max(minMetresPerPixel, Math.min(maxMetresPerPixel, candidate))
    }

    /// Zooms about a point on screen, leaving the ground under that point where it is. Zooming about
    /// the view centre instead would slide whatever the operator was looking at out from under the
    /// cursor.
    function zoomBy(factor, pivotX, pivotY) {
        const groundEast = eastForPixelX(pivotX)
        const groundNorth = northForPixelY(pivotY)

        metresPerPixel = clampZoom(metresPerPixel * factor)

        centreEast = groundEast - ((pivotX - (viewWidth / 2)) * metresPerPixel)
        centreNorth = groundNorth + ((pivotY - (viewHeight / 2)) * metresPerPixel)
    }

    /// Drags the view by a screen distance, so the ground follows the cursor rather than opposing it
    function panByPixels(deltaX, deltaY) {
        centreEast -= deltaX * metresPerPixel
        centreNorth += deltaY * metresPerPixel
    }

    function centreOn(north, east) {
        centreNorth = north
        centreEast = east
    }

    /// Sets the zoom so a square of the given size fits the smaller side of the view, with a margin
    /// so the edges of the square are not against the edges of the screen.
    function zoomToFit(metresAcross) {
        const shorterSide = Math.min(viewWidth, viewHeight)
        if (!(metresAcross > 0) || !(shorterSide > 0)) {
            return
        }
        metresPerPixel = clampZoom((metresAcross * 1.2) / shorterSide)
    }
}
