#include "NonGpsFlowHealthTest.h"

#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtTest/QTest>

#include "FactGroup.h"
#include "Vehicle.h"

namespace {

// MockLink has no ArduPilot EK3_MAX_FLOW, so the tests point the object at a PX4 parameter that
// does exist and drive the sample values from whatever limit it reports.
constexpr const char *kLimitParameterName = "MPC_Z_VEL_MAX_UP";

/// Feeds one OPTICAL_FLOW message through the vehicle's fact group. Flow is put entirely on the
/// x axis so the resulting magnitude equals the requested value.
void sendFlowMagnitude(Vehicle *vehicle, double magnitude)
{
    mavlink_optical_flow_t opticalFlow{};
    opticalFlow.quality = 200;
    opticalFlow.flow_comp_m_x = static_cast<float>(magnitude);
    opticalFlow.flow_comp_m_y = 0.0F;
    opticalFlow.ground_distance = 1.0F;

    mavlink_message_t message{};
    (void) mavlink_msg_optical_flow_encode(vehicle->id(), MAV_COMP_ID_AUTOPILOT1, &message, &opticalFlow);

    FactGroup *const factGroup = vehicle->getFactGroup(QStringLiteral("opticalFlow"));
    QVERIFY(factGroup);
    factGroup->handleMessage(vehicle, message);
}

} // namespace

NonGpsFlowHealthTest::NonGpsFlowHealthTest(QObject *parent) : VehicleTest(parent)
{
    setWaitForParameters(true);
}

void NonGpsFlowHealthTest::_noSamplesReportsNoData_test()
{
    QVERIFY(vehicle());

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QGroundControl.FlyView

        NonGpsFlowHealth {}
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    const QScopedPointer<QObject> flowHealth(component.createWithInitialProperties({
        { QStringLiteral("vehicle"), QVariant::fromValue(vehicle()) },
    }));
    QVERIFY(flowHealth);

    QVERIFY(!flowHealth->property("hasSamples").toBool());
    QCOMPARE(flowHealth->property("sampleCount").toInt(), 0);

    // Nothing arriving must not read as "0% rejected", which would look like a healthy sensor
    QVERIFY(qIsNaN(flowHealth->property("rejectedPercent").toDouble()));
    QVERIFY(qIsNaN(flowHealth->property("averageMagnitude").toDouble()));
    QVERIFY(!flowHealth->property("rejectingNow").toBool());
}

void NonGpsFlowHealthTest::_accumulatesSamples_test()
{
    QVERIFY(vehicle());

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QGroundControl.FlyView

        NonGpsFlowHealth {}
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    const QScopedPointer<QObject> flowHealth(component.createWithInitialProperties({
        { QStringLiteral("vehicle"), QVariant::fromValue(vehicle()) },
    }));
    QVERIFY(flowHealth);

    sendFlowMagnitude(vehicle(), 0.5);
    sendFlowMagnitude(vehicle(), 1.5);
    sendFlowMagnitude(vehicle(), 1.0);

    QCOMPARE(flowHealth->property("sampleCount").toInt(), 3);
    QVERIFY(flowHealth->property("hasSamples").toBool());
    QCOMPARE(flowHealth->property("peakMagnitude").toDouble(), 1.5);
    QCOMPARE(flowHealth->property("averageMagnitude").toDouble(), 1.0);
}

void NonGpsFlowHealthTest::_countsRejectedAboveLimit_test()
{
    QVERIFY(vehicle());

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QGroundControl.FlyView

        NonGpsFlowHealth {}
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    const QScopedPointer<QObject> flowHealth(component.createWithInitialProperties({
        { QStringLiteral("vehicle"), QVariant::fromValue(vehicle()) },
        { QStringLiteral("limitParameterNames"), QVariantList{ QString::fromLatin1(kLimitParameterName) } },
    }));
    QVERIFY(flowHealth);

    QVERIFY2(flowHealth->property("limitKnown").toBool(), "limit parameter should resolve on MockLink");
    const double limit = flowHealth->property("flowLimit").toDouble();
    QVERIFY(limit > 0);

    sendFlowMagnitude(vehicle(), limit / 2);
    sendFlowMagnitude(vehicle(), limit * 2);
    sendFlowMagnitude(vehicle(), limit * 3);

    QCOMPARE(flowHealth->property("sampleCount").toInt(), 3);
    QCOMPARE(flowHealth->property("rejectedCount").toInt(), 2);
    QCOMPARE(flowHealth->property("rejectedPercent").toDouble(), (100.0 * 2) / 3);

    // The last reading was above the limit, so the EKF is discarding flow right now
    QVERIFY(flowHealth->property("rejectingNow").toBool());

    sendFlowMagnitude(vehicle(), limit / 4);
    QVERIFY(!flowHealth->property("rejectingNow").toBool());
}

void NonGpsFlowHealthTest::_missingLimitParameterGivesNoVerdict_test()
{
    QVERIFY(vehicle());

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QGroundControl.FlyView

        NonGpsFlowHealth {}
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    const QScopedPointer<QObject> flowHealth(component.createWithInitialProperties({
        { QStringLiteral("vehicle"), QVariant::fromValue(vehicle()) },
        { QStringLiteral("limitParameterNames"), QVariantList{ QStringLiteral("NO_SUCH_PARAMETER") } },
    }));
    QVERIFY(flowHealth);

    sendFlowMagnitude(vehicle(), 1000.0);

    // Without a limit there is nothing to compare against, so no reading may be called rejected
    QVERIFY(!flowHealth->property("limitKnown").toBool());
    QCOMPARE(flowHealth->property("rejectedCount").toInt(), 0);
    QVERIFY(!flowHealth->property("rejectingNow").toBool());
}

void NonGpsFlowHealthTest::_samplesAgeOutOfWindow_test()
{
    QVERIFY(vehicle());

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QGroundControl.FlyView

        NonGpsFlowHealth {}
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    const QScopedPointer<QObject> flowHealth(component.createWithInitialProperties({
        { QStringLiteral("vehicle"), QVariant::fromValue(vehicle()) },
        { QStringLiteral("windowSecs"), 1 },
    }));
    QVERIFY(flowHealth);

    sendFlowMagnitude(vehicle(), 2.0);
    QCOMPARE(flowHealth->property("sampleCount").toInt(), 1);

    // Once flow stops, the window must empty on its own rather than freeze on the last verdict
    QTRY_VERIFY_WITH_TIMEOUT(!flowHealth->property("hasSamples").toBool(), TestTimeout::mediumMs());
    QVERIFY(qIsNaN(flowHealth->property("rejectedPercent").toDouble()));
}

void NonGpsFlowHealthTest::_limitFoundWhenVehicleArrivesLater_test()
{
    QVERIFY(vehicle());

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QGroundControl.FlyView

        NonGpsFlowHealth {}
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    // Created with no vehicle, the way the fly view creates it at startup
    const QScopedPointer<QObject> flowHealth(component.createWithInitialProperties({
        { QStringLiteral("limitParameterNames"), QVariantList{ QString::fromLatin1(kLimitParameterName) } },
    }));
    QVERIFY(flowHealth);
    QVERIFY2(!flowHealth->property("limitKnown").toBool(), "no vehicle means no limit");

    // The vehicle arriving afterwards must still produce a limit. FactPanelController binds its
    // vehicle at construction, so a controller built before this point would be stuck on the
    // offline editing vehicle and never find the parameter.
    flowHealth->setProperty("vehicle", QVariant::fromValue(vehicle()));

    QVERIFY2(flowHealth->property("limitKnown").toBool(),
             "limit parameter should resolve once the vehicle connects");
    QVERIFY(flowHealth->property("flowLimit").toDouble() > 0);
}

UT_REGISTER_TEST(NonGpsFlowHealthTest, TestLabel::Integration, TestLabel::Vehicle)
