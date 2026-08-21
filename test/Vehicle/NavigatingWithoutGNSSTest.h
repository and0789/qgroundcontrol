#pragma once

#include "BaseClasses/VehicleTest.h"

/// Tests Vehicle::navigatingWithoutGNSS on ArduPilot, where the answer is read from the EKF3 source
/// parameters rather than guessed from whether a GPS is fitted.
class NavigatingWithoutGNSSTest : public VehicleTestAPM
{
    Q_OBJECT

public:
    explicit NavigatingWithoutGNSSTest(QObject *parent = nullptr);

private slots:
    void _gpsPositionSource_reportsNavigatingWithGNSS_test();
    void _noPositionSource_reportsNavigatingWithoutGNSS_test();
    void _sourceChanged_emitsChangedSignal_test();
};

/// Tests the fallback used for firmware whose estimator sources QGC cannot read.
class NavigatingWithoutGNSSFallbackTest : public VehicleTest
{
    Q_OBJECT

public:
    explicit NavigatingWithoutGNSSFallbackTest(QObject *parent = nullptr);

private slots:
    void _noEstimatorSourceParameters_fallsBackToGpsPresence_test();
};
