#pragma once

#include "UnitTest.h"

/// Tests the conversion between the coordinates a mission is stored in and the metres the estimator
/// flies in.
///
/// This is the conversion that decides where a waypoint drawn on the grid actually ends up. Swapping
/// north for east, or losing a sign, produces a plan that uploads cleanly, flies smoothly, and lands
/// somewhere else -- with nothing on screen saying so.
class LocalGridProjectionTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _northAndEastMatchTheirBearings_test();
    void _offsetsAndCoordinatesRoundTrip_test();
    void _invalidOriginYieldsNothing_test();
};
