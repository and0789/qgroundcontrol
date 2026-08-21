#include "NonGpsPreFlightChecksTest.h"

#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtTest/QTest>

#include <QtPositioning/QGeoCoordinate>

#include "Fact.h"
#include "FactGroup.h"
#include "FirmwarePlugin.h"
#include "MockLink.h"
#include "ParameterManager.h"
#include "Vehicle.h"

namespace {

constexpr const char *kPositionSourceParameter = "EK3_SRC1_POSXY";

// Values of EK3_SRC1_POSXY, from AP_NavEKF_Source::SourceXY in ArduPilot
constexpr int kSourceNone = 0;
constexpr int kSourceGps = 3;

/// Applies a horizontal position source the way the vehicle reports one
void setPositionSource(Vehicle *vehicle, int source)
{
    vehicle->parameterManager()
        ->getParameter(ParameterManager::defaultComponentId, QString::fromLatin1(kPositionSourceParameter))
        ->containerSetRawValue(source);
}

void sendOpticalFlow(Vehicle *vehicle, int quality)
{
    mavlink_optical_flow_t opticalFlow{};
    opticalFlow.quality = static_cast<uint8_t>(quality);
    opticalFlow.ground_distance = 1.5F;

    mavlink_message_t message{};
    (void) mavlink_msg_optical_flow_encode(vehicle->id(), MAV_COMP_ID_AUTOPILOT1, &message, &opticalFlow);
    vehicle->getFactGroup(QStringLiteral("opticalFlow"))->handleMessage(vehicle, message);
}

void sendDownwardRangefinder(Vehicle *vehicle, double metres)
{
    mavlink_distance_sensor_t distanceSensor{};
    distanceSensor.orientation = MAV_SENSOR_ROTATION_PITCH_270;
    distanceSensor.current_distance = static_cast<uint16_t>(metres * 100.0);
    distanceSensor.min_distance = 10;
    distanceSensor.max_distance = 1200;

    mavlink_message_t message{};
    (void) mavlink_msg_distance_sensor_encode(vehicle->id(), MAV_COMP_ID_AUTOPILOT1, &message, &distanceSensor);
    vehicle->getFactGroup(QStringLiteral("distanceSensor"))->handleMessage(vehicle, message);
}

/// Gives the vehicle an estimator origin, the way the operator does from the fly view map.
///
/// Cached-unsupported drives the legacy message, which MockLink records as the vehicle's origin.
bool setEstimatorOrigin(Vehicle *vehicle, MockLink *mockLink, const QGeoCoordinate &origin)
{
    FirmwarePluginInstanceData *const instanceData = vehicle->firmwarePluginInstanceData();
    if (!instanceData) {
        return false;
    }

    instanceData->setCommandSupported(MAV_CMD_DO_SET_GLOBAL_ORIGIN,
                                      FirmwarePluginInstanceData::CommandSupportedResult::UNSUPPORTED);
    vehicle->setEstimatorOrigin(origin);
    if (!QTest::qWaitFor([mockLink]() {
            return mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_SET_GPS_GLOBAL_ORIGIN) >= 1;
        }, TestTimeout::longMs())) {
        return false;
    }

    // Asked again between waits rather than once. The origin comes back as a single
    // GPS_GLOBAL_ORIGIN, and one that goes missing on a link still busy with a full parameter set
    // costs the whole timeout -- which fails the test for its own noise rather than for the thing it
    // is checking. Seen twice while this suite was being written.
    for (int attempt = 0; attempt < 3; attempt++) {
        vehicle->requestEstimatorOrigin();
        if (QTest::qWaitFor([vehicle]() { return vehicle->estimatorOrigin().isValid(); },
                            TestTimeout::mediumMs())) {
            return true;
        }
    }
    return false;
}

/// Flies the vehicle and puts it back down. MockLink decides landed state from altitude above home,
/// so a takeoff to height and a takeoff back to home altitude is the whole flight it can model.
bool flyAndLand(Vehicle *vehicle)
{
    vehicle->sendMavCommand(vehicle->defaultComponentId(), MAV_CMD_NAV_TAKEOFF, false /* showError */,
                            0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 10.0F /* altitude */);
    if (!QTest::qWaitFor([vehicle]() { return vehicle->flying(); }, TestTimeout::longMs())) {
        return false;
    }

    vehicle->sendMavCommand(vehicle->defaultComponentId(), MAV_CMD_NAV_TAKEOFF, false /* showError */,
                            0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F /* altitude */);
    return QTest::qWaitFor([vehicle]() { return !vehicle->flying(); }, TestTimeout::longMs());
}

void sendEkfStatus(Vehicle *vehicle, uint16_t flags)
{
    mavlink_ekf_status_report_t ekfStatus{};
    ekfStatus.flags = flags;

    mavlink_message_t message{};
    (void) mavlink_msg_ekf_status_report_encode(vehicle->id(), MAV_COMP_ID_AUTOPILOT1, &message, &ekfStatus);
    vehicle->getFactGroup(QStringLiteral("estimatorStatus"))->handleMessage(vehicle, message);
}

} // namespace

NonGpsPreFlightChecksTest::NonGpsPreFlightChecksTest(QObject *parent) : VehicleTestAPM(parent)
{
    setWaitForParameters(true);
}

void NonGpsPreFlightChecksTest::init()
{
    VehicleTestAPM::init();

    // Building a real button pulls in font resolution, which the headless runner has to work out
    // the first time. Nothing to do with the checks.
    ignoreLogMessage("qt.qpa.fonts", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Populating font family aliases")));
}

/// Creates one pre-flight check in a context carrying a `globals` stub, which is how the checks
/// reach the active vehicle in the running application.
class PreFlightCheckFixture
{
public:
    explicit PreFlightCheckFixture(Vehicle *vehicle)
    {
        _engine.addImportPath(QStringLiteral("qrc:/qml"));

        QQmlComponent globalsComponent(&_engine);
        globalsComponent.setData(R"(
            import QtQuick
            QtObject { property var activeVehicle }
        )", QUrl());
        _globals.reset(globalsComponent.create());
        if (_globals) {
            _globals->setProperty("activeVehicle", QVariant::fromValue(vehicle));
        }

        _context.reset(new QQmlContext(_engine.rootContext()));
        _context->setContextProperty(QStringLiteral("globals"), _globals.get());
    }

    /// @return the created check, or nullptr with @a error describing why not
    QObject *create(const QString &typeName, QString &error)
    {
        // The host stands in for PreFlightCheckGroup, which gives every check a parent carrying
        // buttonPassedChanged and an ancestor palette. A check created without either still
        // evaluates its own bindings, but logs errors from the base class that say nothing about
        // the logic under test.
        static const QString hostTemplate = QStringLiteral(
            "import QtQuick\n"
            "import QGroundControl\n"
            "import QGroundControl.Controls\n"
            "import QGroundControl.FlyView\n"
            "Item {\n"
            "    property alias check: hostedCheck\n"
            "    function buttonPassedChanged() { }\n"
            "    QGCPalette { id: qgcPal; colorGroupEnabled: true }\n"
            "    %1 { id: hostedCheck }\n"
            "}\n");

        auto *const component = new QQmlComponent(&_engine);
        component->setData(hostTemplate.arg(typeName).toUtf8(), QUrl());
        if (!component->isReady()) {
            error = component->errorString();
            delete component;
            return nullptr;
        }

        QObject *const host = component->create(_context.get());
        if (!host) {
            error = component->errorString();
            delete component;
            return nullptr;
        }

        component->setParent(host);
        _created.append(host);

        QObject *const check = host->property("check").value<QObject *>();
        if (!check) {
            error = QStringLiteral("%1 was not created inside its host").arg(typeName);
        }
        return check;
    }

    ~PreFlightCheckFixture() { qDeleteAll(_created); }

    Q_DISABLE_COPY_MOVE(PreFlightCheckFixture)

private:
    QQmlEngine _engine;
    QScopedPointer<QObject> _globals;
    QScopedPointer<QQmlContext> _context;
    QList<QObject *> _created;
};

#define CREATE_CHECK(fixture, typeName, out)                       \
    QString out##Error;                                            \
    QObject *const out = (fixture).create(typeName, out##Error);   \
    QVERIFY2(out, qPrintable(out##Error))

/// A vehicle navigating by GNSS must not have four extra rows added to its checklist, and they must
/// not hold its checklist back either -- an invisible row that never passes would leave the group
/// permanently incomplete with nothing on screen explaining why.
void NonGpsPreFlightChecksTest::_vehicleWithGNSS_checksAreHiddenAndPassing_test()
{
    QVERIFY(vehicle());
    setPositionSource(vehicle(), kSourceGps);
    QVERIFY(!vehicle()->navigatingWithoutGNSS());

    PreFlightCheckFixture fixture(vehicle());
    const QStringList checkTypes = {
        QStringLiteral("PreFlightEstimatorOriginCheck"),
        QStringLiteral("PreFlightPositionConfirmedCheck"),
        QStringLiteral("PreFlightOpticalFlowCheck"),
        QStringLiteral("PreFlightRangefinderCheck"),
        QStringLiteral("PreFlightEkfNavigationCheck"),
    };

    for (const QString &checkType : checkTypes) {
        CREATE_CHECK(fixture, checkType, check);
        QVERIFY2(!check->property("visible").toBool(), qPrintable(checkType));
        QVERIFY2(!check->property("telemetryFailure").toBool(), qPrintable(checkType));
        QVERIFY2(check->property("passed").toBool(), qPrintable(checkType));
    }
}

/// The same four rows must appear once the estimator is no longer using GNSS, whether or not a GPS
/// is still fitted for ground truth.
void NonGpsPreFlightChecksTest::_vehicleWithoutGNSS_checksAreShown_test()
{
    QVERIFY(vehicle());
    setPositionSource(vehicle(), kSourceNone);
    QVERIFY(vehicle()->navigatingWithoutGNSS());

    PreFlightCheckFixture fixture(vehicle());
    const QStringList checkTypes = {
        QStringLiteral("PreFlightEstimatorOriginCheck"),
        QStringLiteral("PreFlightOpticalFlowCheck"),
        QStringLiteral("PreFlightRangefinderCheck"),
        QStringLiteral("PreFlightEkfNavigationCheck"),
    };

    for (const QString &checkType : checkTypes) {
        CREATE_CHECK(fixture, checkType, check);
        QVERIFY2(check->property("visible").toBool(), qPrintable(checkType));
    }
}

/// Without an origin the mission cannot run at all, so this one must be a hard stop rather than a
/// warning the operator can click past on the flight line.
void NonGpsPreFlightChecksTest::_missingEstimatorOrigin_failsWithoutOverride_test()
{
    QVERIFY(vehicle());
    setPositionSource(vehicle(), kSourceNone);
    QVERIFY(!vehicle()->estimatorOrigin().isValid());

    PreFlightCheckFixture fixture(vehicle());
    CREATE_CHECK(fixture, QStringLiteral("PreFlightEstimatorOriginCheck"), check);

    QVERIFY(check->property("telemetryFailure").toBool());
    QVERIFY(!check->property("allowTelemetryFailureOverride").toBool());

    // The checklist resets every check when the vehicle changes, which is what turns a failing
    // binding into a failed row.
    QVERIFY(QMetaObject::invokeMethod(check, "reset"));
    QVERIFY(check->property("failed").toBool());
    QVERIFY(!check->property("passed").toBool());
}

/// Both sensors reporting sensibly is the case that must pass, or the checks would be noise the
/// operator learns to click through.
void NonGpsPreFlightChecksTest::_flowAndRangefinderReporting_passChecks_test()
{
    QVERIFY(vehicle());
    setPositionSource(vehicle(), kSourceNone);

    PreFlightCheckFixture fixture(vehicle());
    CREATE_CHECK(fixture, QStringLiteral("PreFlightOpticalFlowCheck"), flowCheck);
    CREATE_CHECK(fixture, QStringLiteral("PreFlightRangefinderCheck"), rangefinderCheck);

    // Nothing has been received yet, which must read as a failure rather than as healthy zeroes
    QVERIFY(flowCheck->property("telemetryFailure").toBool());
    QVERIFY(rangefinderCheck->property("telemetryFailure").toBool());

    sendOpticalFlow(vehicle(), 200);
    sendDownwardRangefinder(vehicle(), 1.5);

    QVERIFY(!flowCheck->property("telemetryFailure").toBool());
    QVERIFY(!rangefinderCheck->property("telemetryFailure").toBool());

    // A sensor that reports but reports badly must fail, and stay overridable: texture and lighting
    // are conditions of the site, which only the operator can judge.
    sendOpticalFlow(vehicle(), 10);
    QVERIFY(flowCheck->property("telemetryFailure").toBool());
    QVERIFY(flowCheck->property("allowTelemetryFailureOverride").toBool());
}

/// An estimator that reports itself unready is not the operator's judgement call: nothing holds
/// position until it converges, so a flow-only LOITER simply drifts away.
void NonGpsPreFlightChecksTest::_ekfNotReady_failsWithoutOverride_test()
{
    QVERIFY(vehicle());
    setPositionSource(vehicle(), kSourceNone);

    PreFlightCheckFixture fixture(vehicle());
    CREATE_CHECK(fixture, QStringLiteral("PreFlightEkfNavigationCheck"), check);

    // No EKF telemetry at all says nothing about the aircraft, so that case stays overridable
    QVERIFY(check->property("telemetryFailure").toBool());
    QVERIFY(check->property("allowTelemetryFailureOverride").toBool());

    // Reporting, but fallen back to assuming the vehicle is stationary
    sendEkfStatus(vehicle(), EKF_ATTITUDE | EKF_CONST_POS_MODE);
    QVERIFY(check->property("telemetryFailure").toBool());
    QVERIFY(!check->property("allowTelemetryFailureOverride").toBool());

    sendEkfStatus(vehicle(), EKF_ATTITUDE | EKF_VELOCITY_HORIZ | EKF_POS_HORIZ_REL);
    QVERIFY(!check->property("telemetryFailure").toBool());
}

/// The GPS row used to wait forever for a 3D lock the estimator will never use, leaving a checklist
/// that could not be completed. It must now ask the operator to confirm the intent instead.
void NonGpsPreFlightChecksTest::_gpsCheck_doesNotBlockAGnssDeniedVehicle_test()
{
    QVERIFY(vehicle());
    PreFlightCheckFixture fixture(vehicle());
    CREATE_CHECK(fixture, QStringLiteral("PreFlightGPSCheck"), check);

    setPositionSource(vehicle(), kSourceNone);
    QVERIFY(!check->property("telemetryFailure").toBool());
    QVERIFY(!check->property("manualText").toString().isEmpty());

    // Confirming it is what passes the row, so the aircraft's own configuration still has to be
    // acknowledged rather than assumed.
    QVERIFY(QMetaObject::invokeMethod(check, "reset"));
    QVERIFY(!check->property("passed").toBool());
    QMetaObject::invokeMethod(check, "clicked");
    QVERIFY(check->property("passed").toBool());

    // A GNSS vehicle keeps the check it always had
    setPositionSource(vehicle(), kSourceGps);
    QVERIFY(check->property("manualText").toString().isEmpty());
}

/// The row must stay out of the way until there is an origin. Without one there is nothing to state
/// a position inside, and PreFlightEstimatorOriginCheck is already saying so -- two rows failing over
/// the same missing origin is what teaches an operator to click past both.
///
/// Placing the origin is itself a statement of where the aircraft is standing, so the row appears
/// already satisfied rather than appearing and immediately demanding something that was just done.
void NonGpsPreFlightChecksTest::_positionConfirmedCheck_waitsForAnOrigin_test()
{
    QVERIFY(vehicle());
    QVERIFY(mockLink());
    setPositionSource(vehicle(), kSourceNone);
    QVERIFY(vehicle()->navigatingWithoutGNSS());
    QVERIFY(!vehicle()->estimatorOrigin().isValid());

    PreFlightCheckFixture fixture(vehicle());
    CREATE_CHECK(fixture, QStringLiteral("PreFlightPositionConfirmedCheck"), check);

    QVERIFY2(!check->property("visible").toBool(), "without an origin the origin check speaks alone");
    QVERIFY(!check->property("telemetryFailure").toBool());

    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), QGeoCoordinate(47.3977419, 8.5455938, 488.0)));

    QVERIFY(check->property("visible").toBool());
    QVERIFY2(!check->property("telemetryFailure").toBool(),
             "placing the origin is the operator saying where the aircraft is standing");
}

/// The check the operator meets between two flights. The aircraft has landed, the statement of where
/// it was standing has stopped covering anything, and the row says so with no way to click past it.
///
/// This is the flight that used to go out on the last flight's answer: the estimate crept while the
/// first mission was flown, the frame it drifted into is the one the second mission would be flown
/// in, and nothing on screen said so because the aircraft reports itself exactly where the plan says
/// it should be.
void NonGpsPreFlightChecksTest::_positionNotStatedSinceFlying_failsWithoutOverride_test()
{
    QVERIFY(vehicle());
    QVERIFY(mockLink());
    setPositionSource(vehicle(), kSourceNone);

    // Going flying builds QGCPressure, which warns on a host with no pressure backend
    ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Failed to connect to pressure backend")));
    ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Error Initializing Pressure Sensor")));

    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), QGeoCoordinate(47.3977419, 8.5455938, 488.0)));

    PreFlightCheckFixture fixture(vehicle());
    CREATE_CHECK(fixture, QStringLiteral("PreFlightPositionConfirmedCheck"), check);
    QVERIFY(!check->property("telemetryFailure").toBool());

    QVERIFY(flyAndLand(vehicle()));

    QVERIFY(check->property("visible").toBool());
    QVERIFY2(check->property("telemetryFailure").toBool(),
             "a statement of position covers the flight it was made for and no more");
    QVERIFY2(!check->property("allowTelemetryFailureOverride").toBool(),
             "clicking past this is the same as not having stated the position at all");

    QVERIFY(QMetaObject::invokeMethod(check, "reset"));
    QVERIFY(check->property("failed").toBool());
    QVERIFY(!check->property("passed").toBool());

    // Stating it again is what clears the row, and it is one click on the grid
    QVERIFY(QMetaObject::invokeMethod(vehicle(), "externalPositionEstimateResult",
                                      Q_ARG(bool, true), Q_ARG(QString, QString())));
    QVERIFY(!check->property("telemetryFailure").toBool());
}

UT_REGISTER_TEST(NonGpsPreFlightChecksTest, TestLabel::Integration, TestLabel::Vehicle)
