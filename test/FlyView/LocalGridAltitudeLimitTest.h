#pragma once

#include "BaseClasses/VehicleTest.h"

/// Tests the ceiling a rangefinder puts on a plan.
///
/// A vehicle flying without GNSS commonly takes its altitude from a rangefinder, and above that
/// rangefinder's range the estimator has no height source at all. Nothing refuses the plan: it
/// uploads cleanly and the aircraft climbs out of range in flight. The check is only meaningful
/// where the rangefinder really is the height source, so it must stay quiet everywhere else.
class LocalGridAltitudeLimitTest : public VehicleTestAPM
{
    Q_OBJECT

public:
    explicit LocalGridAltitudeLimitTest(QObject *parent = nullptr);

private slots:
    void _rangefinderSource_reportsTheRangeAsTheCeiling_test();
    void _flowVelocitySource_keepsTheCeiling_test();
    void _neitherSource_warnsAboutNothing_test();
};
