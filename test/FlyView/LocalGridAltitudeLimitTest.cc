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

/// Applies a parameter the way the vehicle reports one, without a write down the link
void setParameter(Vehicle *vehicle, const QString &name, const QVariant &value)
{
    vehicle->parameterManager()
        ->getParameter(ParameterManager::defaultComponentId, name)
        ->containerSetRawValue(value);
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

UT_REGISTER_TEST(LocalGridAltitudeLimitTest, TestLabel::Integration, TestLabel::Vehicle)
