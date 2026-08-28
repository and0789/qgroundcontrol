#include "NonGpsPreFlightChecksTest.h"

#include <QtCore/QScopeGuard>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtTest/QTest>

#include <QtPositioning/QGeoCoordinate>

#include "Fact.h"
#include "FactGroup.h"
#include "FirmwarePlugin.h"
#include "FlyViewSettings.h"
#include "MockLink.h"
#include "ParameterManager.h"
#include "SettingsManager.h"
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

/// Creates a GuidedActionsController with the collaborators it dereferences unconditionally.
///
/// The controller reads the active vehicle from QGroundControl directly, so only the mission
/// controller and the confirmation dialog have to stand in -- at the shape the paths under test
/// actually reach, which is what keeps the stubs from describing calls that never happen.
class GuidedActionsFixture
{
public:
    GuidedActionsFixture()
    {
        _engine.addImportPath(QStringLiteral("qrc:/qml"));

        // The controller listens to the main window for the arm requests the toolbar raises. Absent
        // it the Connections block cannot resolve its target and says so on every construction.
        QQmlComponent mainWindowComponent(&_engine);
        mainWindowComponent.setData(R"(
            import QtQuick
            QtObject {
                signal armVehicleRequest()
                signal forceArmVehicleRequest()
                signal disarmVehicleRequest()
            }
        )",
                                    QUrl());
        _mainWindow.reset(mainWindowComponent.create());
        _engine.rootContext()->setContextProperty(QStringLiteral("mainWindow"), _mainWindow.get());
    }

    /// @return the created controller, or nullptr with @a error describing why not
    QObject* create(QString& error)
    {
        _component.reset(new QQmlComponent(&_engine));
        _component->setData(R"(
            import QtQuick
            import QGroundControl
            import QGroundControl.Controls
            import QGroundControl.FlyView
            Item {
                property alias controller:    hostedController
                property alias confirmDialog: confirmDialogStub

                QtObject {
                    id: missionControllerStub
                    property bool containsItems: true
                    property var  visualItems: null
                    property int  currentMissionIndex: 0
                    property int  resumeMissionIndex: 0
                    signal resumeMissionUploadFail()
                }

                // Everything confirmAction writes into, which is what makes the dialog's own half of
                // the refusal assertable. No slider stands in beside it: setupSlider matches no
                // branch for this action and never reaches for one.
                QtObject {
                    id: confirmDialogStub
                    property bool   blocked: false
                    property bool   visible: false
                    property bool   hideTrigger: false
                    property string title
                    property string message
                    property string optionText
                    property int    action
                    property var    actionData
                    property var    mapIndicator
                    function confirmCancelled(incomingIndicator) { }
                    function show(immediate) { }
                }

                GuidedActionsController {
                    id:                 hostedController
                    missionController:  missionControllerStub
                    confirmDialog:      confirmDialogStub
                }
            }
        )",
                            QUrl());

        _host.reset(_component->create());
        if (!_host) {
            error = _component->errorString();
            return nullptr;
        }

        QObject* const controller = _host->property("controller").value<QObject*>();
        if (!controller) {
            error = QStringLiteral("no controller on the host");
        }
        // Borrowed: the host owns it and this fixture owns the host
        return controller;
    }

    /// The dialog the controller wrote its refusal into, or nullptr before create()
    QObject* confirmDialog() const { return _host ? _host->property("confirmDialog").value<QObject*>() : nullptr; }

private:
    QQmlEngine _engine;
    QScopedPointer<QObject> _mainWindow;
    QScopedPointer<QQmlComponent> _component;
    QScopedPointer<QObject> _host;
};

/// Starting a mission without an origin must be refused outright, not merely warned about.
///
/// ArduPilot's ModeAuto::takeoff_start finds current_loc uninitialised when there is no EKF origin
/// and raises INTERNAL_ERROR(flow_of_control) -- a state its own comment calls impossible. The error
/// latches: every arm attempt afterwards is refused with "PreArm: Internal errors 0x100000" until
/// the aircraft is power cycled. Reproduced in SITL by restarting the vehicle after a flight and
/// pressing Start Mission. A message the operator can confirm past is not enough for a cost the
/// vehicle pays and only a reboot clears.
void NonGpsPreFlightChecksTest::_startMissionWithoutOrigin_isRefused_test()
{
    QVERIFY(vehicle());
    QVERIFY(mockLink());
    setPositionSource(vehicle(), kSourceNone);
    QVERIFY(vehicle()->navigatingWithoutGNSS());
    QVERIFY(!vehicle()->estimatorOrigin().isValid());

    // The controller raises this dialog by itself the moment a mission becomes startable. Driving it
    // by hand below instead ties what is asserted to the press being modelled rather than to a
    // setting that happens to default on.
    Fact* const automaticPopups = SettingsManager::instance()->flyViewSettings()->enableAutomaticMissionPopups();
    const QVariant savedPopups = automaticPopups->rawValue();
    const auto restorePopups =
        qScopeGuard([automaticPopups, savedPopups] { automaticPopups->setRawValue(savedPopups); });
    automaticPopups->setRawValue(false);

    GuidedActionsFixture fixture;
    QString error;
    QObject* const controller = fixture.create(error);
    QVERIFY2(controller, qPrintable(error));
    QObject* const confirmDialog = fixture.confirmDialog();
    QVERIFY(confirmDialog);

    const auto confirmStartMission = [controller]() {
        // All three arguments: a QML function is exposed with the arity it declares, and a call
        // that leaves the optional ones off does not match it.
        return QMetaObject::invokeMethod(controller, "confirmAction", Qt::DirectConnection,
                                         Q_ARG(QVariant, controller->property("actionStartMission")),
                                         Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()));
    };

    const auto startMission = [controller]() {
        QVariant executed;
        if (!QMetaObject::invokeMethod(controller, "executeAction", Qt::DirectConnection,
                                       Q_RETURN_ARG(QVariant, executed),
                                       Q_ARG(QVariant, controller->property("actionStartMission")),
                                       Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, 0), Q_ARG(QVariant, false))) {
            return false;
        }
        return executed.toBool();
    };

    // Downstream of the assertion below rather than part of it: the mission rewind goes out first,
    // and only then does the start sequence try to change mode on a mock that models no flight modes.
    ignoreLogMessage("FirmwarePlugin.APMFirmwarePlugin", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Unknown flight Mode")));
    ignoreLogMessage("Vehicle.Vehicle", QtWarningMsg, QRegularExpression(QStringLiteral("setFlightMode failed")));

    mockLink()->clearReceivedMavCommandCounts();

    // The dialog's half of the refusal: the action and its explanation are offered, and the button
    // that would run it is shut.
    QVERIFY(confirmStartMission());
    QVERIFY2(confirmDialog->property("blocked").toBool(), "the confirm button must be shut without an origin");

    QVERIFY2(!startMission(), "the action must report that it did not run");

    // Then with an origin, so the refusal above is about the origin rather than about a fixture that
    // could never have started a mission. ArduPilot's start sequence opens by rewinding the mission,
    // which makes DO_SET_MISSION_CURRENT the first thing on the wire either way -- and waiting for
    // this one is what proves the refused attempt sent nothing, without waiting on a silence.
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), QGeoCoordinate(47.3977419, 8.5455938, 488.0)));
    QVERIFY(confirmStartMission());
    QVERIFY2(!confirmDialog->property("blocked").toBool(), "with an origin the button has to open again");

    QVERIFY(startMission());
    QVERIFY_TRUE_WAIT(mockLink()->receivedMavCommandCount(MAV_CMD_DO_SET_MISSION_CURRENT) >= 1, TestTimeout::longMs());
    QCOMPARE(mockLink()->receivedMavCommandCount(MAV_CMD_DO_SET_MISSION_CURRENT), 1);
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
