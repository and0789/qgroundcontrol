#pragma once

#include "BaseClasses/VehicleTest.h"

/// Tests OpticalFlowCalibrator. Its output is written permanently to the vehicle, so the cases
/// that matter most are the ones that must refuse to produce a value: a sensor mounted rotated,
/// and a run with no usable samples.
class OpticalFlowCalibratorTest : public VehicleTest
{
    Q_OBJECT

public:
    explicit OpticalFlowCalibratorTest(QObject *parent = nullptr);

private slots:
    void _accurateScaleNeedsNoChange_test();
    void _underreadingSuggestsNewScaler_test();
    void _invertedSensorFails_test();
    void _crossAxisDominanceFails_test();
    void _lowQualityAndYawDiscarded_test();
    void _noSamplesFails_test();
};
