import QtQuick

/// The path the vehicle has flown, in metres north and east of the estimator origin.
///
/// For a vehicle navigating by dead reckoning the trail is not decoration -- it is the measurement.
/// A box pattern that closes back on the origin and one that closes two metres to the west look the
/// same on the instruments and completely different here, and the distance flown along the way is
/// the denominator drift is quoted against.
///
/// Points are sampled by distance rather than by time, so a hover does not fill the buffer with
/// thousands of coincident points and hide the transit that follows.
QtObject {
    id: _root

    /// How far the vehicle must move before another point is kept
    property real minSampleMetres: 0.1

    /// Once this many points are held, the trail is thinned rather than truncated. Dropping the
    /// oldest points would throw away the departure from the origin, which is the end of the flight
    /// a return-to-home error is measured against.
    property int maxPoints: 2000

    readonly property int  pointCount:       _pointCount
    readonly property real pathLengthMetres: _pathLength

    /// Spacing actually in force, which doubles each time the trail is thinned
    readonly property real effectiveSampleMetres: _effectiveSample

    property var  _points:          []
    property int  _pointCount:      0
    property real _pathLength:      0
    property real _effectiveSample: minSampleMetres

    onMinSampleMetresChanged: _effectiveSample = minSampleMetres

    /// @return true if the point was kept
    function addPoint(north, east) {
        if (isNaN(north) || isNaN(east)) {
            return false
        }

        const points = _points
        if (points.length > 0) {
            const last = points[points.length - 1]
            const step = Math.sqrt(((north - last.north) * (north - last.north))
                                   + ((east - last.east) * (east - last.east)))
            if (step < _effectiveSample) {
                return false
            }
            // Measured along the kept points, so it is the length of the line actually drawn rather
            // than a separate number that could drift away from it
            _pathLength += step
        }

        points.push({ north: north, east: east })
        _points = points
        _pointCount = points.length

        if (points.length > maxPoints) {
            _thin()
        }
        return true
    }

    function reset() {
        _points = []
        _pointCount = 0
        _pathLength = 0
        _effectiveSample = minSampleMetres
    }

    /// Keeps every second point and doubles the spacing, halving the buffer while leaving the shape
    /// of the whole flight intact. The last point is always kept so the trail still ends where the
    /// vehicle is.
    function _thin() {
        const points = _points
        var thinned = []
        for (var i = 0; i < points.length; i += 2) {
            thinned.push(points[i])
        }
        if (thinned[thinned.length - 1] !== points[points.length - 1]) {
            thinned.push(points[points.length - 1])
        }

        _points = thinned
        _pointCount = thinned.length
        _effectiveSample = _effectiveSample * 2
    }

    /// Direct access for the drawing, which walks the whole trail on every repaint and would not
    /// survive copying it each time
    function points() {
        return _points
    }
}
