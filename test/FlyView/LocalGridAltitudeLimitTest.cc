#include "LocalGridAltitudeLimitTest.h"

#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtTest/QTest>

#include "Fact.h"
#include "ParameterManager.h"
#include "Vehicle.h"

namespace {

// Values of EK3_SRC1_POSZ, from AP_NavEKF_Source::SourceZ in ArduPilot
constexpr int kSourceBaro = 1;
constexpr int kSourceRangefinder = 2;

// Values of EK3_SRC1_VELXY, from AP_NavEKF_Source::SourceXY
constexpr int kVelocityNone = 0;
constexpr int kVelocityOpticalFlow = 5;

constexpr double kRangefinderMaxMetres = 12.0;

// MockLink's ArduPilot parameter set carries no rangefinder at all, so the tests point the object at
// a numeric parameter the mock vehicle does have and drive the logic from its value. What is under
// test is the rule -- a ceiling that applies only while the rangefinder is the height source -- not
// the spelling of a parameter name.
constexpr const char *kStandInMaxParameter = "EK3_ALT_M_NSE";
constexpr const char *kStandInVelocityParameter = "EK3_SRC1_VELXY";

// Same trick for the return altitude: the mock vehicle carries no RTL_ALT at all. What is under test
// is the comparison -- a return that climbs past the height reference -- not which parameter the
// number was read from.
constexpr const char *kStandInReturnAltParameter = "EK3_VELD_M_NSE";

/// Applies a parameter the way the vehicle reports one, without a write down the link
void setParameter(Vehicle *vehicle, const QString &name, const QVariant &value)
{
    vehicle->parameterManager()
        ->getParameter(ParameterManager::defaultComponentId, name)
        ->containerSetRawValue(value);
}

/// Reports a height the way a downward-facing rangefinder does. PITCH_270 is the orientation that
/// means "looking at the ground", and it is the only one whose reading is a height at all.
void sendDownwardRangefinder(Vehicle *vehicle, double metres)
{
    mavlink_distance_sensor_t distanceSensor{};
    distanceSensor.orientation = MAV_SENSOR_ROTATION_PITCH_270;
    distanceSensor.current_distance = static_cast<uint16_t>(metres * 100.0);   // metres to cm
    distanceSensor.min_distance = 20;
    distanceSensor.max_distance = static_cast<uint16_t>(kRangefinderMaxMetres * 100.0);

    mavlink_message_t message{};
    (void) mavlink_msg_distance_sensor_encode(vehicle->id(), MAV_COMP_ID_AUTOPILOT1, &message, &distanceSensor);
    vehicle->getFactGroup(QStringLiteral("distanceSensor"))->handleMessage(vehicle, message);
}

/// Reports a height the way OPTICAL_FLOW carries one. ArduPilot fills ground_distance from the same
/// downward rangefinder, and sends it negative when it has no distance to report.
void sendFlowHeight(Vehicle *vehicle, double metres)
{
    mavlink_optical_flow_t opticalFlow{};
    opticalFlow.quality = 200;
    opticalFlow.ground_distance = static_cast<float>(metres);

    mavlink_message_t message{};
    (void) mavlink_msg_optical_flow_encode(vehicle->id(), MAV_COMP_ID_AUTOPILOT1, &message, &opticalFlow);
    vehicle->getFactGroup(QStringLiteral("opticalFlow"))->handleMessage(vehicle, message);
}

QObject *createLimit(QQmlComponent &component, Vehicle *vehicle, QString &error)
{
    component.setData(R"(
        import QGroundControl.FlyView

        LocalGridAltitudeLimit { }
    )", QUrl());
    if (!component.isReady()) {
        error = component.errorString();
        return nullptr;
    }

    QObject *const limit = component.createWithInitialProperties({
        { QStringLiteral("vehicle"), QVariant::fromValue(vehicle) },
        { QStringLiteral("rangefinderMaxParameterName"), QString::fromLatin1(kStandInMaxParameter) },
        { QStringLiteral("velocitySourceParameterName"), QString::fromLatin1(kStandInVelocityParameter) },
        { QStringLiteral("returnAltitudeParameterName"), QString::fromLatin1(kStandInReturnAltParameter) },
    });
    if (!limit) {
        error = component.errorString();
    }
    return limit;
}

bool exceeds(QObject *limit, double metres)
{
    QVariant result;
    const bool invoked = QMetaObject::invokeMethod(limit, "exceeds", Qt::DirectConnection,
                                                   Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, metres));
    return invoked && result.toBool();
}

} // namespace

LocalGridAltitudeLimitTest::LocalGridAltitudeLimitTest(QObject *parent) : VehicleTestAPM(parent)
{
    setWaitForParameters(true);
}

#define MAKE_LIMIT(name)                                                \
    QQmlEngine name##Engine;                                            \
    name##Engine.addImportPath(QStringLiteral("qrc:/qml"));             \
    QQmlComponent name##Component(&name##Engine);                       \
    QString name##Error;                                                \
    const QScopedPointer<QObject> name(                                 \
        createLimit(name##Component, vehicle(), name##Error));          \
    QVERIFY2(name, qPrintable(name##Error))

/// The case this exists for: the estimator's height comes from the rangefinder, so the rangefinder's
/// range is how high the plan may go.
void LocalGridAltitudeLimitTest::_rangefinderSource_reportsTheRangeAsTheCeiling_test()
{
    QVERIFY(vehicle());
    setParameter(vehicle(), QStringLiteral("EK3_SRC1_POSZ"), kSourceRangefinder);
    setParameter(vehicle(), QString::fromLatin1(kStandInVelocityParameter), kVelocityNone);
    setParameter(vehicle(), QString::fromLatin1(kStandInMaxParameter), kRangefinderMaxMetres);

    MAKE_LIMIT(limit);

    QVERIFY(limit->property("rangefinderIsAltitudeSource").toBool());
    QVERIFY(limit->property("limitKnown").toBool());
    QCOMPARE(limit->property("limitMetres").toDouble(), kRangefinderMaxMetres);

    QVERIFY2(!exceeds(limit.get(), 5.0), "well inside the range");
    QVERIFY2(!exceeds(limit.get(), kRangefinderMaxMetres), "exactly at the range is still in range");
    QVERIFY2(exceeds(limit.get(), kRangefinderMaxMetres + 0.5), "past the range must be reported");

    // An altitude that is not a number is not a breach of anything
    QVERIFY(!exceeds(limit.get(), qQNaN()));
}

/// The barometer holding the altitude removes one reason for a ceiling, but not the other: optical
/// flow still needs a height to be scaled into a velocity, and above the rangefinder's range there
/// is none. This is the case the corrected SITL configuration produces, and the one that would have
/// been missed by checking the altitude source alone.
void LocalGridAltitudeLimitTest::_flowVelocitySource_keepsTheCeiling_test()
{
    QVERIFY(vehicle());
    setParameter(vehicle(), QStringLiteral("EK3_SRC1_POSZ"), kSourceBaro);
    setParameter(vehicle(), QString::fromLatin1(kStandInVelocityParameter), kVelocityOpticalFlow);
    setParameter(vehicle(), QString::fromLatin1(kStandInMaxParameter), kRangefinderMaxMetres);

    MAKE_LIMIT(limit);

    QVERIFY2(!limit->property("rangefinderIsAltitudeSource").toBool(), "the barometer holds the height");
    QVERIFY(limit->property("opticalFlowIsVelocitySource").toBool());
    QVERIFY2(limit->property("limitKnown").toBool(), "flow still needs a height to be scaled with");
    QVERIFY(exceeds(limit.get(), kRangefinderMaxMetres + 1.0));

    // The reason names what is lost, since the failure reads as a compass or position problem
    QVERIFY(limit->property("limitReason").toString().contains(QStringLiteral("flow")));
}

/// Where neither the height nor the horizontal velocity depends on the rangefinder, its range says
/// nothing about how high the vehicle may fly. Warning anyway would be noise, and noise is what an
/// operator learns to click past.
void LocalGridAltitudeLimitTest::_neitherSource_warnsAboutNothing_test()
{
    QVERIFY(vehicle());
    setParameter(vehicle(), QStringLiteral("EK3_SRC1_POSZ"), kSourceBaro);
    setParameter(vehicle(), QString::fromLatin1(kStandInVelocityParameter), kVelocityNone);
    setParameter(vehicle(), QString::fromLatin1(kStandInMaxParameter), kRangefinderMaxMetres);

    MAKE_LIMIT(limit);

    QVERIFY(!limit->property("limitApplies").toBool());
    QVERIFY(!limit->property("limitKnown").toBool());
    QVERIFY2(!exceeds(limit.get(), 500.0), "no ceiling applies when nothing depends on the rangefinder");
}

/// The altitude a plan falls back to when the one it was given is out of reach. It has to be under
/// the ceiling with room to spare: a waypoint sitting exactly on the rangefinder's range is one gust
/// away from being over it, and the last metre of a rangefinder is where its readings are least
/// trustworthy. On a sensor too short for that margin, half the range is still a flight -- zero is
/// not.
void LocalGridAltitudeLimitTest::_safeDefault_staysUnderTheCeiling_test()
{
    QVERIFY(vehicle());
    setParameter(vehicle(), QStringLiteral("EK3_SRC1_POSZ"), kSourceRangefinder);
    setParameter(vehicle(), QString::fromLatin1(kStandInVelocityParameter), kVelocityNone);
    setParameter(vehicle(), QString::fromLatin1(kStandInMaxParameter), kRangefinderMaxMetres);

    MAKE_LIMIT(limit);

    QCOMPARE(limit->property("safeDefaultMetres").toDouble(), kRangefinderMaxMetres - 1.0);
    QVERIFY(!exceeds(limit.get(), limit->property("safeDefaultMetres").toDouble()));

    // A rangefinder short enough that a metre of margin would take the answer to nothing
    setParameter(vehicle(), QString::fromLatin1(kStandInMaxParameter), 1.5);
    QCOMPARE(limit->property("safeDefaultMetres").toDouble(), 0.75);
    QVERIFY(!exceeds(limit.get(), limit->property("safeDefaultMetres").toDouble()));

    // Nothing to fall back to where no ceiling applies, so the caller keeps the altitude it has
    setParameter(vehicle(), QStringLiteral("EK3_SRC1_POSZ"), kSourceBaro);
    QVERIFY(!limit->property("limitKnown").toBool());
    QVERIFY(qIsNaN(limit->property("safeDefaultMetres").toDouble()));
}

/// The same ceiling, checked against where the vehicle actually is rather than where the plan put
/// it.
///
/// A plan flown exactly as drawn still ends up here: the operator climbs by hand, the ground falls
/// away under a pattern flown level, or the vehicle overshoots its target altitude. None of those
/// are visible to a check that only reads the plan, and the failure is identical -- above the
/// rangefinder's range there is no height source and nothing on screen says so.
void LocalGridAltitudeLimitTest::_liveHeightIsCheckedAgainstTheCeiling_test()
{
    QVERIFY(vehicle());
    setParameter(vehicle(), QStringLiteral("EK3_SRC1_POSZ"), kSourceRangefinder);
    setParameter(vehicle(), QString::fromLatin1(kStandInVelocityParameter), kVelocityNone);
    setParameter(vehicle(), QString::fromLatin1(kStandInMaxParameter), kRangefinderMaxMetres);

    MAKE_LIMIT(limit);

    // Nothing reported yet. An unread rangefinder must not read as a vehicle safely on the ground:
    // both are silent, and only one of them is fine.
    QVERIFY2(!limit->property("currentHeightKnown").toBool(),
             "a rangefinder that has reported nothing knows no height");
    QVERIFY(!limit->property("nearCeiling").toBool());
    QVERIFY(!limit->property("aboveCeiling").toBool());

    // Well under the ceiling
    sendDownwardRangefinder(vehicle(), 4.0);
    QVERIFY(limit->property("currentHeightKnown").toBool());
    QCOMPARE(limit->property("currentHeightMetres").toDouble(), 4.0);
    QVERIFY(!limit->property("nearCeiling").toBool());

    // Past the safe default but still within range: the margin is there to be warned inside
    sendDownwardRangefinder(vehicle(), kRangefinderMaxMetres - 0.5);
    QVERIFY(limit->property("nearCeiling").toBool());
    QVERIFY2(!limit->property("aboveCeiling").toBool(),
             "inside the range is a warning, not a loss of reference");

    // Over the range itself
    sendDownwardRangefinder(vehicle(), kRangefinderMaxMetres + 2.0);
    QVERIFY(limit->property("aboveCeiling").toBool());

    // And it clears on the way back down
    sendDownwardRangefinder(vehicle(), 3.0);
    QVERIFY(!limit->property("nearCeiling").toBool());
    QVERIFY(!limit->property("aboveCeiling").toBool());
}

/// Where the rangefinder is not holding the vehicle up, its range says nothing about how high the
/// vehicle may fly. Warning about it there would be noise, and noise is what teaches an operator to
/// ignore the warning that matters.
void LocalGridAltitudeLimitTest::_liveHeightIsSilentWhereNoCeilingApplies_test()
{
    QVERIFY(vehicle());
    setParameter(vehicle(), QStringLiteral("EK3_SRC1_POSZ"), kSourceBaro);
    setParameter(vehicle(), QString::fromLatin1(kStandInVelocityParameter), kVelocityNone);
    setParameter(vehicle(), QString::fromLatin1(kStandInMaxParameter), kRangefinderMaxMetres);

    MAKE_LIMIT(limit);

    sendDownwardRangefinder(vehicle(), kRangefinderMaxMetres + 5.0);

    QVERIFY(!limit->property("limitApplies").toBool());
    QVERIFY2(!limit->property("currentHeightKnown").toBool(),
             "a height means nothing against a ceiling that does not apply");
    QVERIFY(!limit->property("nearCeiling").toBool());
    QVERIFY(!limit->property("aboveCeiling").toBool());
}

/// A vehicle that streams OPTICAL_FLOW but not DISTANCE_SENSOR still has to be warned.
///
/// Whether the dedicated distance message is on the link at all depends on how its rates are set
/// up, and the flow message carries the same rangefinder's reading. Read from the distance fact
/// alone the ceiling stayed silent on exactly the aircraft it was written for -- and worse than
/// silent, because that fact starts at zero rather than NaN, so "nothing arrived" and "sitting on
/// the ground" are the same reading.
void LocalGridAltitudeLimitTest::_flowHeightStandsInForASilentRangefinder_test()
{
    QVERIFY(vehicle());
    setParameter(vehicle(), QStringLiteral("EK3_SRC1_POSZ"), kSourceRangefinder);
    setParameter(vehicle(), QString::fromLatin1(kStandInVelocityParameter), kVelocityNone);
    setParameter(vehicle(), QString::fromLatin1(kStandInMaxParameter), kRangefinderMaxMetres);

    MAKE_LIMIT(limit);

    // The flow message's own way of saying it has no distance. It must not read as a height, and
    // must not read as zero either.
    sendFlowHeight(vehicle(), -1.0);
    QVERIFY2(!limit->property("currentHeightKnown").toBool(),
             "a flow message reporting no distance knows no height");

    // No DISTANCE_SENSOR has been sent in this test at all, so everything below comes from the flow
    sendFlowHeight(vehicle(), 4.0);
    QVERIFY(limit->property("currentHeightKnown").toBool());
    QCOMPARE(limit->property("currentHeightMetres").toDouble(), 4.0);
    QVERIFY(!limit->property("nearCeiling").toBool());

    sendFlowHeight(vehicle(), kRangefinderMaxMetres - 0.5);
    QVERIFY(limit->property("nearCeiling").toBool());
    QVERIFY(!limit->property("aboveCeiling").toBool());

    sendFlowHeight(vehicle(), kRangefinderMaxMetres + 2.0);
    QVERIFY(limit->property("aboveCeiling").toBool());
}

/// Where both messages arrive, the dedicated one wins.
///
/// DISTANCE_SENSOR states the orientation its reading was taken in; OPTICAL_FLOW only promises a
/// distance to the ground. The fallback exists to cover silence, not to compete with a sensor that
/// is reporting.
void LocalGridAltitudeLimitTest::_rangefinderIsPreferredOverTheFlowHeight_test()
{
    QVERIFY(vehicle());
    setParameter(vehicle(), QStringLiteral("EK3_SRC1_POSZ"), kSourceRangefinder);
    setParameter(vehicle(), QString::fromLatin1(kStandInVelocityParameter), kVelocityNone);
    setParameter(vehicle(), QString::fromLatin1(kStandInMaxParameter), kRangefinderMaxMetres);

    MAKE_LIMIT(limit);

    sendFlowHeight(vehicle(), kRangefinderMaxMetres + 2.0);
    QVERIFY(limit->property("aboveCeiling").toBool());

    sendDownwardRangefinder(vehicle(), 3.0);
    QCOMPARE(limit->property("currentHeightMetres").toDouble(), 3.0);
    QVERIFY2(!limit->property("aboveCeiling").toBool(),
             "the sensor that is reporting decides, not the stand-in for its silence");
}

/// A return to launch climbs to RTL_ALT before it starts home, and that altitude lives in a
/// parameter rather than in the plan -- so the ceiling this object holds every waypoint under has no
/// way to clamp it. On an aircraft navigating on a rangefinder it is the one item an operator can
/// add to a plan that leaves the height reference behind with nothing in QGC able to stop it.
void LocalGridAltitudeLimitTest::_returnAltitudeAboveTheCeilingIsFlagged_test()
{
    QVERIFY(vehicle());
    setParameter(vehicle(), QStringLiteral("EK3_SRC1_POSZ"), kSourceRangefinder);
    setParameter(vehicle(), QString::fromLatin1(kStandInVelocityParameter), kVelocityNone);
    setParameter(vehicle(), QString::fromLatin1(kStandInMaxParameter), kRangefinderMaxMetres);
    setParameter(vehicle(), QString::fromLatin1(kStandInReturnAltParameter), kRangefinderMaxMetres + 8.0);

    MAKE_LIMIT(limit);

    QVERIFY(limit->property("returnAltitudeKnown").toBool());
    QCOMPARE(limit->property("returnAltitudeMetres").toDouble(), kRangefinderMaxMetres + 8.0);
    QVERIFY2(limit->property("returnAltitudeAboveCeiling").toBool(),
             "a return that climbs past the rangefinder's range has to be reported");

    // Brought under the range, it is an ordinary item again
    setParameter(vehicle(), QString::fromLatin1(kStandInReturnAltParameter), kRangefinderMaxMetres - 4.0);
    QVERIFY(!limit->property("returnAltitudeAboveCeiling").toBool());

    // Exactly at the range is still in range, the same answer a waypoint at that height gets
    setParameter(vehicle(), QString::fromLatin1(kStandInReturnAltParameter), kRangefinderMaxMetres);
    QVERIFY(!limit->property("returnAltitudeAboveCeiling").toBool());
}

/// Where no ceiling applies the return altitude says nothing. A vehicle holding its position on GNSS
/// climbs to RTL_ALT with a source that does not care how high it is, and flagging that would be
/// noise the operator learns to click past -- which is how a real warning gets missed.
void LocalGridAltitudeLimitTest::_returnAltitudeIsSilentWhereNoCeilingApplies_test()
{
    QVERIFY(vehicle());
    setParameter(vehicle(), QStringLiteral("EK3_SRC1_POSZ"), kSourceBaro);
    setParameter(vehicle(), QString::fromLatin1(kStandInVelocityParameter), kVelocityNone);
    setParameter(vehicle(), QString::fromLatin1(kStandInMaxParameter), kRangefinderMaxMetres);
    setParameter(vehicle(), QString::fromLatin1(kStandInReturnAltParameter), kRangefinderMaxMetres + 8.0);

    MAKE_LIMIT(limit);

    QVERIFY(!limit->property("limitApplies").toBool());
    QVERIFY2(!limit->property("returnAltitudeAboveCeiling").toBool(),
             "no height reference to lose means no reason to refuse a return");
}

UT_REGISTER_TEST(LocalGridAltitudeLimitTest, TestLabel::Integration, TestLabel::Vehicle)
