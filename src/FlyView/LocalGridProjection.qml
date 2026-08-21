import QtQuick
import QtPositioning

/// Converts between the global coordinates a mission is stored in and the metres north and east the
/// estimator flies in.
///
/// Missions are held as latitude and longitude even for a vehicle that never sees a satellite: the
/// autopilot converts each item into an offset from its estimator origin on arrival. This does the
/// same conversion the other way so a plan can be drawn on the grid it will actually be flown on.
///
/// Kept separate and tested because the failure is silent. Swapping north for east, or losing a
/// sign, produces a plan that uploads cleanly, flies smoothly, and lands somewhere else.
QtObject {
    id: _root

    /// @return { north, east } in metres from origin, or null when either coordinate is unusable
    function northEastFrom(origin, coordinate) {
        if (!origin || !coordinate || !origin.isValid || !coordinate.isValid) {
            return null
        }

        // Geodesic, via QtPositioning, rather than a flat-earth approximation -- the same maths the
        // plan itself uses, so a waypoint drawn here lands where the plan says it is.
        const distance = origin.distanceTo(coordinate)
        const azimuth = origin.azimuthTo(coordinate)
        const radians = azimuth * Math.PI / 180

        return {
            north: distance * Math.cos(radians),
            east:  distance * Math.sin(radians)
        }
    }

    /// @return the global coordinate at the given offsets, or an invalid coordinate when the origin
    /// is unusable or the offsets are not numbers
    function coordinateAt(origin, north, east) {
        if (!origin || !origin.isValid || isNaN(north) || isNaN(east)) {
            return QtPositioning.coordinate()
        }

        const distance = Math.sqrt((north * north) + (east * east))
        // atan2 of east over north, rather than the usual y over x, is what turns a maths angle into
        // a bearing measured clockwise from north
        const azimuth = Math.atan2(east, north) * 180 / Math.PI

        return origin.atDistanceAndAzimuth(distance, azimuth)
    }
}
