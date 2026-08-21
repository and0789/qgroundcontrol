#include "NavigatingWithoutGNSSTest.h"

#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "Fact.h"
#include "ParameterManager.h"
#include "Vehicle.h"

namespace {

constexpr const char *kPositionSourceParameter = "EK3_SRC1_POSXY";

// Values of EK3_SRC1_POSXY, from AP_NavEKF_Source::SourceXY in ArduPilot. Written out rather than
// taken from the APM plugin header so this file still builds in a QGC configured without it.
constexpr int kSourceNone = 0;
constexpr int kSourceGps = 3;
constexpr int kSourceOpticalFlow = 5;

Fact *positionSourceFact(Vehicle *vehicle)
{
    return vehicle->parameterManager()->getParameter(ParameterManager::defaultComponentId,
                                                     QString::fromLatin1(kPositionSourceParameter));
}

/// Applies a new source value the way the vehicle reports one, rather than writing it back down the
/// link. The operator edits this parameter on the aircraft; what QGC sees is the PARAM_VALUE that
/// comes back, and that is the path under test here.
void setPositionSource(Vehicle *vehicle, int source)
{
    positionSourceFact(vehicle)->containerSetRawValue(source);
}

} // namespace

NavigatingWithoutGNSSTest::NavigatingWithoutGNSSTest(QObject *parent) : VehicleTestAPM(parent)
{
    setWaitForParameters(true);
}

/// The stock ArduPilot configuration takes horizontal position from GPS, which is the case that
/// must keep the estimator origin controls hidden.
void NavigatingWithoutGNSSTest::_gpsPositionSource_reportsNavigatingWithGNSS_test()
{
    QVERIFY(vehicle());
    QCOMPARE(positionSourceFact(vehicle())->rawValue().toInt(), kSourceGps);

    QVERIFY(!vehicle()->navigatingWithoutGNSS());
}

/// The regression this exists for. An optical flow aircraft sets EK3_SRC1_POSXY to NONE while still
/// carrying a GPS to log ground truth, so the vehicle reports the GPS sensor bit as present. Keying
/// off that bit hid "Set Estimator Origin" on precisely the vehicles that cannot take off without
/// it, and suppressed the warning that a mission would stall on its first item.
void NavigatingWithoutGNSSTest::_noPositionSource_reportsNavigatingWithoutGNSS_test()
{
    QVERIFY(vehicle());

    // Guard the premise: MockLink reports a GPS as present throughout, exactly as a ground truth
    // receiver does.
    QVERIFY_TRUE_WAIT(vehicle()->requiresGpsFix(), TestTimeout::longMs());

    setPositionSource(vehicle(), kSourceNone);

    QVERIFY(vehicle()->navigatingWithoutGNSS());
    QVERIFY2(vehicle()->requiresGpsFix(), "a fitted GPS must not change the estimator's answer");
}

/// The sources are edited during bring-up, so an answer that only settles at connection time would
/// leave the origin controls in whatever state the vehicle booted with.
void NavigatingWithoutGNSSTest::_sourceChanged_emitsChangedSignal_test()
{
    QVERIFY(vehicle());
    QSignalSpy changedSpy(vehicle(), &Vehicle::navigatingWithoutGNSSChanged);
    QVERIFY(changedSpy.isValid());

    setPositionSource(vehicle(), kSourceOpticalFlow);

    QVERIFY(changedSpy.count() > 0);
    QVERIFY(vehicle()->navigatingWithoutGNSS());

    setPositionSource(vehicle(), kSourceGps);
    QVERIFY(!vehicle()->navigatingWithoutGNSS());
}

NavigatingWithoutGNSSFallbackTest::NavigatingWithoutGNSSFallbackTest(QObject *parent) : VehicleTest(parent)
{
    // Without this the parameter absence checked below would be true simply because nothing has
    // loaded yet, and the test would pass against any implementation.
    setWaitForParameters(true);
}

/// PX4 has no EK3_SRC parameters, so the answer comes from whether a GPS is fitted. That is a weak
/// signal, but reporting a GNSS-denied vehicle where none was detected would be worse.
void NavigatingWithoutGNSSFallbackTest::_noEstimatorSourceParameters_fallsBackToGpsPresence_test()
{
    QVERIFY(vehicle());
    QVERIFY(!vehicle()->parameterManager()->parameterExists(ParameterManager::defaultComponentId,
                                                            QString::fromLatin1(kPositionSourceParameter)));

    QVERIFY_TRUE_WAIT(vehicle()->requiresGpsFix(), TestTimeout::longMs());
    QVERIFY(!vehicle()->navigatingWithoutGNSS());
}

UT_REGISTER_TEST(NavigatingWithoutGNSSTest, TestLabel::Integration, TestLabel::Vehicle)
UT_REGISTER_TEST(NavigatingWithoutGNSSFallbackTest, TestLabel::Integration, TestLabel::Vehicle)
