#pragma once

#include "UnitTest.h"

/// Tests the flown path the local grid draws.
///
/// On a vehicle navigating by dead reckoning the trail is the measurement, not decoration: a box
/// pattern that closes on the origin and one that closes two metres west look identical on the
/// instruments. A trail that quietly drops its oldest points would throw away the departure the
/// return is measured against, and the picture would still look like a complete flight.
class LocalGridTrailTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _sampledByDistanceNotByUpdate_test();
    void _pathLengthFollowsTheLineDrawn_test();
    void _thinningKeepsBothEndsOfTheFlight_test();
    void _nonFinitePositionIsRejected_test();
    void _resetClearsEverything_test();
};
