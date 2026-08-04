#include "OpticalFlowCalibratorTest.h"

#include <QtTest/QTest>

#include "OpticalFlowCalibrator.h"
#include "ParameterManager.h"
#include "Vehicle.h"

namespace {

// MockLink carries no ArduPilot FLOW_F?SCALER, so the tests point the calibrator at PX4 parameters
// the mock vehicle does have and drive the arithmetic from their values.
constexpr const char *kXScalerParameter = "MPC_XY_VEL_MAX";
constexpr const char *kYScalerParameter = "MPC_Z_VEL_MAX_UP";

constexpr int kGoodQuality = 200;
constexpr int kSampleCount = 120;

/// Sends one OPTICAL_FLOW carrying the given body and flow rates. flow_comp is filled the way the
/// flight code fills it, with (flowRate - bodyRate), which is what the calibrator inverts.
void sendFlow(Vehicle *vehicle, double bodyX, double flowX, double bodyY, double flowY, int quality)
{
    mavlink_optical_flow_t opticalFlow{};
    opticalFlow.quality = static_cast<uint8_t>(quality);
    opticalFlow.flow_rate_x = static_cast<float>(flowX);
    opticalFlow.flow_rate_y = static_cast<float>(flowY);
    opticalFlow.flow_comp_m_x = static_cast<float>(flowX - bodyX);
    opticalFlow.flow_comp_m_y = static_cast<float>(flowY - bodyY);
    opticalFlow.ground_distance = 1.0F;

    mavlink_message_t message{};
    (void) mavlink_msg_optical_flow_encode(vehicle->id(), MAV_COMP_ID_AUTOPILOT1, &message, &opticalFlow);
    emit vehicle->mavlinkMessageReceived(message);
}

void sendYawRate(Vehicle *vehicle, double yawRate)
{
    mavlink_attitude_t attitude{};
    attitude.yawspeed = static_cast<float>(yawRate);

    mavlink_message_t message{};
    (void) mavlink_msg_attitude_encode(vehicle->id(), MAV_COMP_ID_AUTOPILOT1, &message, &attitude);
    emit vehicle->mavlinkMessageReceived(message);
}

/// Sweeps both axes in turn, with the flow on each axis scaled by the given factor
void sweepBothAxes(Vehicle *vehicle, double scale)
{
    for (int i = 0; i < kSampleCount; i++) {
        const double bodyRate = 0.2 + (0.01 * (i % 20));
        sendFlow(vehicle, bodyRate, bodyRate * scale, 0.0, 0.0, kGoodQuality);
    }
    for (int i = 0; i < kSampleCount; i++) {
        const double bodyRate = 0.2 + (0.01 * (i % 20));
        sendFlow(vehicle, 0.0, 0.0, bodyRate, bodyRate * scale, kGoodQuality);
    }
}

OpticalFlowCalibrator *startCalibrator(Vehicle *vehicle)
{
    OpticalFlowCalibrator *const calibrator = vehicle->opticalFlowCalibrator();
    calibrator->setProperty("xScalerParameterName", QString::fromLatin1(kXScalerParameter));
    calibrator->setProperty("yScalerParameterName", QString::fromLatin1(kYScalerParameter));
    calibrator->start();
    return calibrator;
}

} // namespace

OpticalFlowCalibratorTest::OpticalFlowCalibratorTest(QObject *parent) : VehicleTest(parent)
{
    setWaitForParameters(true);
}

void OpticalFlowCalibratorTest::_accurateScaleNeedsNoChange_test()
{
    QVERIFY(vehicle());
    OpticalFlowCalibrator *const calibrator = startCalibrator(vehicle());

    // Flow matching the body rate exactly means the scale is already right
    sweepBothAxes(vehicle(), 1.0);

    QCOMPARE(calibrator->rollSampleCount(), kSampleCount);
    QCOMPARE(calibrator->pitchSampleCount(), kSampleCount);

    calibrator->finish();

    QVERIFY2(calibrator->succeeded(), qPrintable(calibrator->resultSummary()));
    QVERIFY2(!calibrator->hasSuggestions(), "an accurate sensor must not be given a new scaler");
    QVERIFY(calibrator->resultSummary().contains(QStringLiteral("PASSED")));

    calibrator->cancel();
}

void OpticalFlowCalibratorTest::_underreadingSuggestsNewScaler_test()
{
    QVERIFY(vehicle());
    OpticalFlowCalibrator *const calibrator = startCalibrator(vehicle());

    // Flow reading 10% low is the case the calibration exists to correct
    sweepBothAxes(vehicle(), 0.90);
    calibrator->finish();

    QVERIFY2(calibrator->succeeded(), qPrintable(calibrator->resultSummary()));
    QVERIFY(calibrator->hasSuggestions());

    // scale = 1 + 0.001 * scaler, and the samples already carry the old scaler, so the new scaler
    // follows from oldScale / slope
    const double oldScaler = vehicle()->parameterManager()
                                 ->getParameter(-1, QString::fromLatin1(kXScalerParameter))->rawValue().toDouble();
    const int expected = qRound(1000.0 * (((1.0 + (0.001 * oldScaler)) / 0.90) - 1.0));
    QVERIFY2(calibrator->suggestionSummary().contains(QString::number(expected)),
             qPrintable(calibrator->suggestionSummary()));

    calibrator->cancel();
}

void OpticalFlowCalibratorTest::_invertedSensorFails_test()
{
    QVERIFY(vehicle());
    OpticalFlowCalibrator *const calibrator = startCalibrator(vehicle());

    // Flow opposing the gyro means the sensor is mounted rotated by 180 degrees
    sweepBothAxes(vehicle(), -1.0);
    calibrator->finish();

    QVERIFY2(!calibrator->succeeded(), "an inverted sensor must not pass");
    QVERIFY2(!calibrator->hasSuggestions(), "a rotated sensor must not be given a scaler");
    QVERIFY(calibrator->resultSummary().contains(QStringLiteral("180")));

    calibrator->cancel();
}

void OpticalFlowCalibratorTest::_crossAxisDominanceFails_test()
{
    QVERIFY(vehicle());
    OpticalFlowCalibrator *const calibrator = startCalibrator(vehicle());

    // Roll rotation showing up mostly on the Y flow means the sensor is mounted rotated by 90
    // degrees. Scaling that would bake the mounting error into the scale factor.
    for (int i = 0; i < kSampleCount; i++) {
        const double bodyRate = 0.2 + (0.01 * (i % 20));
        sendFlow(vehicle(), bodyRate, bodyRate * 0.1, 0.0, bodyRate, kGoodQuality);
    }
    for (int i = 0; i < kSampleCount; i++) {
        const double bodyRate = 0.2 + (0.01 * (i % 20));
        sendFlow(vehicle(), 0.0, 0.0, bodyRate, bodyRate, kGoodQuality);
    }
    calibrator->finish();

    QVERIFY2(!calibrator->succeeded(), "a 90 degree mounting error must not pass");
    QVERIFY(calibrator->resultSummary().contains(QStringLiteral("90")));

    calibrator->cancel();
}

void OpticalFlowCalibratorTest::_lowQualityAndYawDiscarded_test()
{
    QVERIFY(vehicle());
    OpticalFlowCalibrator *const calibrator = startCalibrator(vehicle());

    // Images the sensor cannot track give flow values that are not measurements of anything
    sendFlow(vehicle(), 0.5, 0.5, 0.0, 0.0, OpticalFlowCalibrator::kMinQuality - 1);
    QCOMPARE(calibrator->rejectedQualityCount(), 1);
    QCOMPARE(calibrator->rollSampleCount(), 0);

    // Yaw turns the image rather than shifting it, so it never appears in the body rate
    sendYawRate(vehicle(), OpticalFlowCalibrator::kMaxYawRate * 2);
    sendFlow(vehicle(), 0.5, 0.5, 0.0, 0.0, kGoodQuality);
    QCOMPARE(calibrator->rejectedYawCount(), 1);
    QCOMPARE(calibrator->rollSampleCount(), 0);

    // With yaw settled the same sample is accepted
    sendYawRate(vehicle(), 0.0);
    sendFlow(vehicle(), 0.5, 0.5, 0.0, 0.0, kGoodQuality);
    QCOMPARE(calibrator->rollSampleCount(), 1);

    calibrator->cancel();
}

void OpticalFlowCalibratorTest::_noSamplesFails_test()
{
    QVERIFY(vehicle());
    OpticalFlowCalibrator *const calibrator = startCalibrator(vehicle());

    calibrator->finish();

    QVERIFY2(!calibrator->succeeded(), "a run with no samples must not report success");
    QVERIFY(!calibrator->hasSuggestions());
    QVERIFY(calibrator->resultSummary().contains(QStringLiteral("FAILED")));

    calibrator->cancel();
}

void OpticalFlowCalibratorTest::_noisyFitPassesWithWarnings_test()
{
    QVERIFY(vehicle());
    OpticalFlowCalibrator *const calibrator = startCalibrator(vehicle());

    // A clean sweep first, to show the same shape without warnings
    sweepBothAxes(vehicle(), 0.90);
    calibrator->finish();
    QVERIFY(calibrator->succeeded());
    QVERIFY2(!calibrator->hasWarnings(), qPrintable(calibrator->resultSummary()));
    QVERIFY(calibrator->resultSummary().contains(QStringLiteral("PASSED")));
    QVERIFY(!calibrator->resultSummary().contains(QStringLiteral("WARNINGS")));

    // Now flow that barely tracks the body rate. This is what translating instead of rotating in
    // place looks like, and it must not be reported the same way as a clean pass.
    calibrator->cancel();
    startCalibrator(vehicle());
    for (int i = 0; i < kSampleCount; i++) {
        const double bodyRate = 0.2 + (0.01 * (i % 20));
        const double noise = ((i % 2) == 0) ? 0.5 : -0.5;
        sendFlow(vehicle(), bodyRate, bodyRate + noise, 0.0, 0.0, kGoodQuality);
    }
    for (int i = 0; i < kSampleCount; i++) {
        const double bodyRate = 0.2 + (0.01 * (i % 20));
        const double noise = ((i % 2) == 0) ? 0.5 : -0.5;
        sendFlow(vehicle(), 0.0, 0.0, bodyRate, bodyRate + noise, kGoodQuality);
    }
    calibrator->finish();

    QVERIFY2(calibrator->hasWarnings(), qPrintable(calibrator->resultSummary()));
    QVERIFY(calibrator->resultSummary().contains(QStringLiteral("PASSED WITH WARNINGS")));

    calibrator->cancel();
}

void OpticalFlowCalibratorTest::_nearZeroSlopeFails_test()
{
    QVERIFY(vehicle());
    OpticalFlowCalibrator *const calibrator = startCalibrator(vehicle());

    // A sensor reporting exactly zero flow while the vehicle rotates gives a slope of zero.
    // Deriving a scaler divides by that slope, and qRound aborts on the resulting infinity rather
    // than saturating, so this took the whole application down.
    sweepBothAxes(vehicle(), 0.0);
    calibrator->finish();

    QVERIFY2(!calibrator->succeeded(), "a sensor reporting no flow must not pass");
    QVERIFY(!calibrator->hasSuggestions());
    QVERIFY(calibrator->resultSummary().contains(QStringLiteral("FAILED")));

    calibrator->cancel();

    // A slope merely tiny stays finite, so it is bounded to the parameter limit and reported with a
    // warning rather than refused. It must not abort either.
    startCalibrator(vehicle());
    sweepBothAxes(vehicle(), 1e-9);
    calibrator->finish();

    QVERIFY(calibrator->hasWarnings());
    QVERIFY2(calibrator->suggestionSummary().contains(QString::number(OpticalFlowCalibrator::kScalerMax)),
             qPrintable(calibrator->suggestionSummary()));

    calibrator->cancel();
}

UT_REGISTER_TEST(OpticalFlowCalibratorTest, TestLabel::Integration, TestLabel::Vehicle)
