#include "LocalGridViewTest.h"

#include <cmath>

#include <QtCore/QElapsedTimer>
#include <QtCore/QtNumeric>
#include <QtPositioning/QGeoCoordinate>
#include <QtTest/QSignalSpy>
#include <QtQml/QJSValue>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include "FactGroup.h"
#include "FirmwarePlugin.h"
#include "Vehicle.h"

namespace {

constexpr double kViewWidth = 800.0;
constexpr double kViewHeight = 600.0;

void sendLocalPosition(Vehicle *vehicle, float north, float east, float down)
{
    mavlink_local_position_ned_t localPosition{};
    localPosition.x = north;
    localPosition.y = east;
    localPosition.z = down;

    mavlink_message_t message{};
    (void) mavlink_msg_local_position_ned_encode(vehicle->id(), MAV_COMP_ID_AUTOPILOT1, &message, &localPosition);
    vehicle->getFactGroup(QStringLiteral("localPosition"))->handleMessage(vehicle, message);
}

/// A stand-in for MissionController holding just what the grid touches: the list it draws and the
/// insertion call it makes. Using the real controller here would test QGC's mission machinery rather
/// than whether the grid hands it the right coordinate.
constexpr const char *kMissionControllerStub = R"(
    import QtQuick
    import QtPositioning
    import QGC

    QtObject {
        property var lastCoordinate: null
        property int lastIndex: -99
        property int insertCount: 0

        property var items: []

        // Real objects rather than plain JavaScript ones. The grid writes a moved waypoint straight
        // to item.coordinate, and only a QML property raises the change that makes the grid redraw
        // -- a plain object would take the value silently and the move would never appear.
        property Component itemComponent: Component {
            QtObject {
                property bool specifiesCoordinate: true
                property var  coordinate
                property int  sequenceNumber: 0
                property bool isCurrentItem: false
                property int  command: 16
                // QGC's display name for the command, which is what a row in a list of items shows
                property string commandName: "Waypoint"
                property int  altitudeFrame: 1
                // What the grid uses to tell the plan's own items from the settings item carrying
                // the planned home position, and a takeoff from anything that can be dragged
                property bool homePosition: false
                property bool isTakeoffItem: false
                // A real Fact, not a look-alike: the panel binds it into a FactTextField, which
                // refuses anything else and says so on every rebuild.
                property Fact altitude: Fact { }
            }
        }

        function addItem(coordinate, sequenceNumber) {
            // A copy, not the same array mutated: assigning the same reference back changes nothing
            // as far as QML is concerned, so count would never update
            var list = items.slice()
            list.push(itemComponent.createObject(null, {
                coordinate: coordinate,
                sequenceNumber: sequenceNumber
            }))
            items = list
        }

        // MissionController always keeps the settings item, which carries the planned home position
        // as its coordinate, at the front of the list
        function addHomeItem(coordinate) {
            var list = items.slice()
            list.unshift(itemComponent.createObject(null, {
                coordinate: coordinate,
                sequenceNumber: 0,
                homePosition: true
            }))
            items = list
        }

        // The sequence number the vehicle is flying to. MissionController reads this off the
        // vehicle's mission manager and answers -1 outside the fly view.
        property int currentMissionIndex: -1

        readonly property var visualItems: QtObject {
            readonly property int count: items.length
            function get(index) { return items[index] }
        }

        // Returns the item it made, the way MissionController does, so a caller that adjusts the
        // new item afterwards is exercised rather than silently doing nothing
        property var lastInsertedItem: null

        function insertSimpleMissionItem(coordinate, index, makeCurrentItem) {
            lastCoordinate = coordinate
            lastIndex = index
            insertCount++
            addItem(coordinate, items.length + 1)
            lastInsertedItem = items[items.length - 1]
            return lastInsertedItem
        }

        // MissionController gates its own insert strip on these, and so does the grid
        property bool isInsertTakeoffValid: true
        property bool isInsertLandValid: true

        property int takeoffCount: 0
        property int landCount: 0

        /// Where MissionController would put a takeoff regardless of what it was asked for, when
        /// the plan has a home position. Left null for a plan that has none.
        property var takeoffLandsOn: null

        function insertTakeoffItem(coordinate, index, makeCurrentItem) {
            lastCoordinate = coordinate
            lastIndex = index
            takeoffCount++
            // The real insertTakeoffItem ignores the coordinate it is handed and puts the item on
            // the plan's home position. Modelled here because it is the reason the grid places the
            // takeoff a second time afterwards: home is where the vehicle reported it launched
            // from, which is not necessarily where the estimator is counting from.
            //
            // Appended like any other insertion. Without this the plan looks empty to the grid,
            // which keeps treating the next item as the first one.
            addItem(takeoffLandsOn ? takeoffLandsOn : coordinate, items.length + 1)
            const item = items[items.length - 1]
            item.isTakeoffItem = true
            item.commandName = "Takeoff"
            lastInsertedItem = item
            return item
        }

        function insertLandItem(coordinate, index, makeCurrentItem) {
            lastCoordinate = coordinate
            lastIndex = index
            landCount++
            addItem(coordinate, items.length + 1)
            return items[items.length - 1]
        }

        property int removedIndex: -99
        property int removeCount: 0

        function removeVisualItem(index) {
            removedIndex = index
            removeCount++
            var list = items.slice()
            list.splice(index, 1)
            items = list
        }
    }
)";

/// A stand-in for PlanMasterController. The real one needs a vehicle, three plan managers and a link
/// before it will say anything at all.
///
/// It carries every property the grid's mission actions bind to, not just the transaction flag this
/// exercises: a missing one reads as undefined, which QML refuses to assign to a bool and reports on
/// every rebuild -- and the test runner treats an unexpected warning as a failure.
constexpr const char *kPlanMasterControllerStub = R"(
    import QtQuick

    QtObject {
        property bool syncInProgress: false
        property bool offline: false
        property bool dirtyForUpload: false
    }
)";

QObject *createStub(QQmlComponent &component, const char *source, QString &error)
{
    component.setData(source, QUrl());
    if (!component.isReady()) {
        error = component.errorString();
        return nullptr;
    }

    QObject *const stub = component.create();
    if (!stub) {
        error = component.errorString();
    }
    return stub;
}

QObject *createMissionControllerStub(QQmlComponent &component, QString &error)
{
    return createStub(component, kMissionControllerStub, error);
}

QObject *createPlanMasterControllerStub(QQmlComponent &component, QString &error)
{
    return createStub(component, kPlanMasterControllerStub, error);
}

/// Feeds the vehicle an EKF_STATUS_REPORT, the message ArduPilot reports estimator health in
void sendEkfStatus(Vehicle *vehicle, uint16_t flags, float posHorizVariance, float velVariance)
{
    mavlink_ekf_status_report_t ekfStatus{};
    ekfStatus.flags = flags;
    ekfStatus.pos_horiz_variance = posHorizVariance;
    ekfStatus.velocity_variance = velVariance;

    mavlink_message_t message{};
    (void) mavlink_msg_ekf_status_report_encode(vehicle->id(), MAV_COMP_ID_AUTOPILOT1, &message, &ekfStatus);
    vehicle->getFactGroup(QStringLiteral("estimatorStatus"))->handleMessage(vehicle, message);
}

/// Every flag a healthy solution sets, so a test that means to degrade one thing degrades one thing
constexpr uint16_t kHealthyEkfFlags = EKF_ATTITUDE | EKF_VELOCITY_HORIZ | EKF_VELOCITY_VERT
                                      | EKF_POS_HORIZ_REL | EKF_POS_HORIZ_ABS | EKF_POS_VERT_ABS;

/// Gives the vehicle an estimator origin, the way the operator does from the fly view map
bool setEstimatorOrigin(Vehicle *vehicle, MockLink *mockLink, const QGeoCoordinate &origin)
{
    FirmwarePluginInstanceData *const instanceData = vehicle->firmwarePluginInstanceData();
    if (!instanceData) {
        return false;
    }

    // Cached-unsupported drives the legacy message, which MockLink records as the vehicle's origin
    instanceData->setCommandSupported(MAV_CMD_DO_SET_GLOBAL_ORIGIN,
                                      FirmwarePluginInstanceData::CommandSupportedResult::UNSUPPORTED);
    vehicle->setEstimatorOrigin(origin);
    if (!QTest::qWaitFor([mockLink]() {
            return mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_SET_GPS_GLOBAL_ORIGIN) >= 1;
        }, TestTimeout::longMs())) {
        return false;
    }

    vehicle->requestEstimatorOrigin();
    return QTest::qWaitFor([vehicle]() { return vehicle->estimatorOrigin().isValid(); }, TestTimeout::longMs());
}

/// Creates the view sized like a fly view and bound to the connected vehicle, owned by the caller
QObject *createGridView(QQmlComponent &component, Vehicle *vehicle, QString &error)
{
    component.setData(R"(
        import QtQuick
        import QGroundControl.FlyView

        LocalGridView { }
    )", QUrl());
    if (!component.isReady()) {
        error = component.errorString();
        return nullptr;
    }

    QObject *const gridView = component.createWithInitialProperties({
        { QStringLiteral("vehicle"), QVariant::fromValue(vehicle) },
        { QStringLiteral("width"), kViewWidth },
        { QStringLiteral("height"), kViewHeight },
    });
    if (!gridView) {
        error = component.errorString();
    }
    return gridView;
}

} // namespace

LocalGridViewTest::LocalGridViewTest(QObject *parent) : VehicleTest(parent)
{
}

void LocalGridViewTest::init()
{
    VehicleTest::init();

    // Building a real view lays out labels, which makes the headless runner resolve fonts once
    ignoreLogMessage("qt.qpa.fonts", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Populating font family aliases")));
    // The type selector's drop-down indicator is a coloured SVG, and a bare QML engine has no image
    // provider registered for it. Nothing to do with the grid.
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Invalid image provider")));
}

#define MAKE_GRID_VIEW(name)                                                       \
    QQmlEngine name##Engine;                                                       \
    name##Engine.addImportPath(QStringLiteral("qrc:/qml"));                        \
    QQmlComponent name##Component(&name##Engine);                                  \
    QString name##Error;                                                           \
    const QScopedPointer<QObject> name(                                            \
        createGridView(name##Component, vehicle(), name##Error));                  \
    QVERIFY2(name, qPrintable(name##Error))

/// The fly view exists before anything connects, and the local position facts read zero until they
/// are filled -- which would draw the vehicle exactly on the origin, indistinguishable from an
/// aircraft sitting where it started.
void LocalGridViewTest::_withoutVehicle_reportsNoPosition_test()
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    QString error;
    const QScopedPointer<QObject> gridView(createGridView(component, nullptr, error));
    QVERIFY2(gridView, qPrintable(error));

    QVERIFY2(!gridView->property("positionValid").toBool(),
             "with no vehicle there is no position to draw");
    QVERIFY(qIsNaN(gridView->property("vehicleNorth").toDouble()));
    QVERIFY(qIsNaN(gridView->property("vehicleEast").toDouble()));
}

/// The frame is the estimator's own: x is metres north of the origin and y is metres east, straight
/// off LOCAL_POSITION_NED with no projection in between.
void LocalGridViewTest::_localPosition_isReadInEstimatorFrame_test()
{
    QVERIFY(vehicle());
    MAKE_GRID_VIEW(gridView);

    sendLocalPosition(vehicle(), 12.5F, -4.25F, -3.0F);

    QVERIFY(gridView->property("positionValid").toBool());
    QCOMPARE(gridView->property("vehicleNorth").toDouble(), 12.5);
    QCOMPARE(gridView->property("vehicleEast").toDouble(), -4.25);
}

/// Following is what makes the view usable while flying: the operator should not have to chase the
/// aircraft across the grid by hand.
void LocalGridViewTest::_followingVehicle_keepsItCentred_test()
{
    QVERIFY(vehicle());
    MAKE_GRID_VIEW(gridView);

    QVERIFY(gridView->property("followVehicle").toBool());

    sendLocalPosition(vehicle(), 30.0F, 40.0F, 0.0F);

    QObject *const transform = gridView->property("gridTransform").value<QObject *>();
    QVERIFY(transform);
    QCOMPARE(transform->property("centreNorth").toDouble(), 30.0);
    QCOMPARE(transform->property("centreEast").toDouble(), 40.0);
}

/// Looking at the origin means looking away from the aircraft, so following has to stop -- otherwise
/// the next telemetry frame drags the view straight back and the button appears not to work.
void LocalGridViewTest::_centreOnOrigin_stopsFollowing_test()
{
    QVERIFY(vehicle());
    MAKE_GRID_VIEW(gridView);

    sendLocalPosition(vehicle(), 30.0F, 40.0F, 0.0F);
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "centreOnOrigin"));

    QObject *const transform = gridView->property("gridTransform").value<QObject *>();
    QVERIFY(transform);
    QCOMPARE(transform->property("centreNorth").toDouble(), 0.0);
    QCOMPARE(transform->property("centreEast").toDouble(), 0.0);
    QVERIFY(!gridView->property("followVehicle").toBool());

    // The view must stay put once the aircraft moves again
    sendLocalPosition(vehicle(), 55.0F, 65.0F, 0.0F);
    QCOMPARE(transform->property("centreNorth").toDouble(), 0.0);
    QCOMPARE(transform->property("centreEast").toDouble(), 0.0);

    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "centreOnVehicle"));
    QVERIFY(gridView->property("followVehicle").toBool());
    QCOMPARE(transform->property("centreNorth").toDouble(), 55.0);
}

/// The trail has to be fed from the same telemetry that moves the marker, or the picture shows an
/// aircraft with no history of how it got there.
void LocalGridViewTest::_trailAccumulatesFromTelemetry_test()
{
    QVERIFY(vehicle());
    MAKE_GRID_VIEW(gridView);

    QCOMPARE(gridView->property("trailPointCount").toInt(), 0);

    // A 10 m box, walked once. Each corner is a diagonal away from the last in one axis only, so
    // sampling per axis rather than per position would show here as extra points.
    const QList<QPointF> corners = {
        QPointF(0.0, 0.0), QPointF(10.0, 0.0), QPointF(10.0, 10.0), QPointF(0.0, 10.0), QPointF(0.0, 0.0),
    };
    int expectedPoints = 0;
    for (const QPointF &corner : corners) {
        sendLocalPosition(vehicle(), static_cast<float>(corner.x()), static_cast<float>(corner.y()), 0.0F);
        expectedPoints++;
        // The sample is coalesced onto the next event loop turn, so let it land before the next
        // corner is sent -- otherwise the whole box collapses into a single sample.
        QTRY_COMPARE_WITH_TIMEOUT(gridView->property("trailPointCount").toInt(), expectedPoints,
                                  TestTimeout::shortMs());
    }

    QCOMPARE(gridView->property("trailPointCount").toInt(), 5);
    QVERIFY(qAbs(gridView->property("trailLengthMetres").toDouble() - 40.0) < 1e-6);

    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "clearTrail"));
    QCOMPARE(gridView->property("trailPointCount").toInt(), 0);
    QCOMPARE(gridView->property("trailLengthMetres").toDouble(), 0.0);
}

/// Without an origin the grid is not anchored to anything a mission can be stored against. Placing a
/// waypoint anyway would invent a coordinate: it would upload cleanly and be flown somewhere else.
void LocalGridViewTest::_withoutEstimatorOrigin_refusesWaypoints_test()
{
    QVERIFY(vehicle());
    QVERIFY2(!vehicle()->estimatorOrigin().isValid(), "the mock vehicle starts without an origin");

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QVERIFY(!gridView->property("originKnown").toBool());
    QVERIFY(!gridView->property("canPlaceWaypoints").toBool());

    QVariant placed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, placed),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, -10.0)));
    QVERIFY2(!placed.toBool(), "no waypoint may be placed against an unanchored grid");
    QCOMPARE(stub->property("insertCount").toInt(), 0);
}

/// A vehicle that has said nothing about its estimator must not read as one with a perfect
/// estimator. Both are all-clear on screen, and only one of them has earned it.
void LocalGridViewTest::_estimatorHealthUnknownUntilReported_test()
{
    QVERIFY(vehicle());
    MAKE_GRID_VIEW(gridView);

    QVERIFY2(!gridView->property("estimatorDegraded").toBool(),
             "an unreported estimator raises no warning");
    QCOMPARE(gridView->property("estimatorWarning").toString(), QString());

    sendEkfStatus(vehicle(), kHealthyEkfFlags, 0.2F, 0.2F);
    QVERIFY2(!gridView->property("estimatorDegraded").toBool(), "a healthy solution stays silent");
}

/// EKF_CONST_POS_MODE is not a degraded position, it is the absence of one: with no aiding left the
/// filter holds the last position and the vehicle drifts away from it unobserved. The grid keeps
/// drawing a confident marker the whole time, which is why this has to be said out loud.
void LocalGridViewTest::_estimatorLosingAiding_isReportedAsSevere_test()
{
    QVERIFY(vehicle());
    MAKE_GRID_VIEW(gridView);

    sendEkfStatus(vehicle(), kHealthyEkfFlags, 0.2F, 0.2F);
    QVERIFY(!gridView->property("estimatorDegraded").toBool());

    sendEkfStatus(vehicle(), kHealthyEkfFlags | EKF_CONST_POS_MODE, 0.2F, 0.2F);

    QVERIFY(gridView->property("estimatorDegraded").toBool());
    QVERIFY2(gridView->property("estimatorSevere").toBool(),
             "an estimator that has stopped aiding is not a mild warning");
    QVERIFY(!gridView->property("estimatorWarning").toString().isEmpty());

    // And it clears when the estimator recovers, rather than latching the way telemetryAvailable does
    sendEkfStatus(vehicle(), kHealthyEkfFlags, 0.2F, 0.2F);
    QVERIFY(!gridView->property("estimatorDegraded").toBool());
    QVERIFY(!gridView->property("estimatorSevere").toBool());
}

/// A filter rejecting its own position measurements is the state just before it stops using them.
/// Worth saying while it is still recoverable rather than only once aiding is gone.
void LocalGridViewTest::_estimatorRejectingMeasurements_isReported_test()
{
    QVERIFY(vehicle());
    MAKE_GRID_VIEW(gridView);

    // Under the warn threshold: working, and nothing to say about it
    sendEkfStatus(vehicle(), kHealthyEkfFlags, 0.5F, 0.5F);
    QVERIFY(!gridView->property("estimatorDegraded").toBool());

    // Between warn and bad: struggling
    sendEkfStatus(vehicle(), kHealthyEkfFlags, 0.9F, 0.5F);
    QVERIFY(gridView->property("estimatorDegraded").toBool());
    QVERIFY2(!gridView->property("estimatorSevere").toBool(),
             "struggling is not the same as having given up, and must not read the same");

    // Past the bad threshold: rejecting
    sendEkfStatus(vehicle(), kHealthyEkfFlags, 1.4F, 0.5F);
    QVERIFY(gridView->property("estimatorSevere").toBool());

    // The velocity ratio counts too -- optical flow aiding is what this way of flying rides on
    sendEkfStatus(vehicle(), kHealthyEkfFlags, 0.2F, 1.4F);
    QVERIFY(gridView->property("estimatorSevere").toBool());
}

/// The estimate going quiet is the failure this whole view has to survive.
///
/// Without GNSS the grid is the only picture of where the aircraft is, and FactGroup's
/// telemetryAvailable is a one-way latch -- nothing in the codebase ever sets it back to false. A
/// vehicle that stops reporting therefore leaves its marker frozen exactly where it was last seen,
/// which on a grid is the same image as an aircraft holding station.
void LocalGridViewTest::_positionGoingQuiet_isReportedAsStale_test()
{
    QVERIFY(vehicle());
    MAKE_GRID_VIEW(gridView);

    // Short enough to keep the test quick, long enough that it is silence being measured
    gridView->setProperty("stalePositionTimeoutMs", 250);

    // MockLink streams LOCAL_POSITION_NED at 10 Hz, and every one of those restarts the countdown
    // this test exists to watch fire. Silence the link: going quiet is the condition under test, and
    // leaving it to a gap opening up in the mock's own stream is what made this race.
    QVERIFY(mockLink());
    mockLink()->setCommLost(true);

    sendLocalPosition(vehicle(), 12.0F, 8.0F, -2.0F);
    QVERIFY(gridView->property("positionValid").toBool());
    QVERIFY2(!gridView->property("positionStale").toBool(),
             "a position that has just arrived is not stale");

    QTRY_VERIFY_WITH_TIMEOUT(gridView->property("positionStale").toBool(), TestTimeout::mediumMs());

    // The numbers stay readable -- what is withdrawn is the claim that they are current. Blanking
    // them would throw away the last place the aircraft was seen, which is where a search starts.
    QCOMPARE(gridView->property("vehicleNorth").toDouble(), 12.0);
    QCOMPARE(gridView->property("vehicleEast").toDouble(), 8.0);
    QVERIFY(gridView->property("positionValid").toBool());
    QVERIFY(gridView->property("positionAgeSeconds").toDouble() > 0.0);

    // And one message brings it back
    sendLocalPosition(vehicle(), 12.5F, 8.0F, -2.0F);
    QVERIFY2(!gridView->property("positionStale").toBool(),
             "a message arriving must clear the warning immediately");

    mockLink()->setCommLost(false);
}

/// A vehicle holding station reports the same position over and over.
///
/// Fact::setRawValue only signals when the value differs, so anything timing the estimate off the
/// facts sees nothing arriving and calls a healthy hover stale. That false alarm is its own failure:
/// a warning that cries wolf on the one instrument left is a warning the operator learns to ignore.
void LocalGridViewTest::_repeatedIdenticalPositions_keepTheEstimateFresh_test()
{
    QVERIFY(vehicle());
    MAKE_GRID_VIEW(gridView);

    gridView->setProperty("stalePositionTimeoutMs", 300);

    // Silenced for the same reason as the staleness test above, and here it is what gives the test
    // its meaning: with the mock's own stream running, the estimate would stay fresh whether or not
    // the resends below did anything.
    //
    // Silence stops the replies to whatever the vehicle already has in flight, and holding it for
    // the second below outlasts the origin request's retries. That is a consequence of the silence
    // this test creates, not of the behaviour under test.
    ignoreLogMessage("Vehicle.MavCommandQueue", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Giving up sending command.*GPS_GLOBAL_ORIGIN")));
    QVERIFY(mockLink());
    mockLink()->setCommLost(true);

    sendLocalPosition(vehicle(), 5.0F, -3.0F, -1.0F);
    QVERIFY(!gridView->property("positionStale").toBool());

    // Held still well past the timeout, resent the way a vehicle would. Not one of these changes a
    // fact, so only the message itself can keep the estimate alive. Each wait doubles as the
    // assertion: it returns true only if staleness changed, which is exactly what must not happen.
    QSignalSpy staleSpy(gridView.get(), SIGNAL(positionStaleChanged()));
    QVERIFY(staleSpy.isValid());

    QElapsedTimer heldStill;
    heldStill.start();
    while (heldStill.elapsed() < 900) {
        sendLocalPosition(vehicle(), 5.0F, -3.0F, -1.0F);
        QVERIFY2(!staleSpy.wait(60), "a vehicle reporting the same position is still reporting");
    }

    QVERIFY(!gridView->property("positionStale").toBool());

    mockLink()->setCommLost(false);
}

/// A plan transfer in flight is the one moment nothing may be placed.
///
/// The fly view's mission controller mirrors the vehicle rather than editing it: when a Clear, an
/// upload or a download completes, MissionController rebuilds its visual items from the vehicle's
/// copy. A waypoint drawn while that round trip is open is discarded without a word, and the
/// operator meets it at the flight line -- Clear, build the next pattern, arm, and Auto refuses a
/// mission that was silently emptied under them.
void LocalGridViewTest::_whileThePlanIsTransferring_refusesWaypoints_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QQmlComponent planComponent(&gridViewEngine);
    QString planError;
    const QScopedPointer<QObject> plan(createPlanMasterControllerStub(planComponent, planError));
    QVERIFY2(plan, qPrintable(planError));
    gridView->setProperty("planMasterController", QVariant::fromValue(plan.get()));

    QVERIFY(gridView->property("canPlaceWaypoints").toBool());

    // Clear has just been pressed: the removal is on its way to the vehicle, and the reply to it
    // will rebuild the list this would be placed into
    plan->setProperty("syncInProgress", true);
    QVERIFY(gridView->property("planSyncInProgress").toBool());
    QVERIFY(!gridView->property("canPlaceWaypoints").toBool());

    QVariant placed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, placed),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, -10.0)));
    QVERIFY2(!placed.toBool(), "nothing may be placed into a list the vehicle's reply is about to overwrite");
    QCOMPARE(stub->property("insertCount").toInt(), 0);
    QCOMPARE(stub->property("takeoffCount").toInt(), 0);

    // The takeoff goes through the same gate, so the other way into the plan is shut too
    QVariant tookOff;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "insertTakeoffAtOrigin", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, tookOff)));
    QVERIFY(!tookOff.toBool());
    QCOMPARE(stub->property("takeoffCount").toInt(), 0);

    // And it all comes back the moment the transfer lands, rather than staying shut until something
    // else nudges the binding
    plan->setProperty("syncInProgress", false);
    QVERIFY(gridView->property("canPlaceWaypoints").toBool());
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, placed),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, -10.0)));
    QVERIFY2(placed.toBool(), "placing must work again once the plan is no longer in transit");
}

/// The point of the whole view: a waypoint placed as metres on the grid must reach the plan as the
/// coordinate those metres describe.
void LocalGridViewTest::_waypointPlacedInMetres_reachesThePlanAsACoordinate_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QVERIFY(gridView->property("originKnown").toBool());
    QVERIFY(gridView->property("canPlaceWaypoints").toBool());

    QVariant placed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, placed),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, -10.0)));
    QVERIFY(placed.toBool());
    // An empty plan gains a takeoff on the origin first, so the waypoint that was asked for is the
    // second insertion and the coordinate asserted below is the one it carried
    QCOMPARE(stub->property("takeoffCount").toInt(), 1);
    QCOMPARE(stub->property("insertCount").toInt(), 1);
    // -1 appends, which is what clicking past the end of a route means
    QCOMPARE(stub->property("lastIndex").toInt(), -1);

    const QGeoCoordinate inserted = stub->property("lastCoordinate").value<QGeoCoordinate>();
    QVERIFY(inserted.isValid());
    QVERIFY2(qAbs(origin.distanceTo(inserted) - std::hypot(20.0, -10.0)) < 0.05,
             "the waypoint must be the stated distance from the origin");
    // 20 north and 10 west is a bearing of about 333 degrees
    const double bearing = origin.azimuthTo(inserted);
    QVERIFY2(qAbs(bearing - 333.435) < 0.5, qPrintable(QStringLiteral("bearing came out %1").arg(bearing)));
}

/// The plan is drawn in the frame the vehicle flies in, so a waypoint sitting 20 m north of the
/// origin has to appear 20 m north of the origin on the grid and not somewhere geodesically close.
void LocalGridViewTest::_planIsDrawnInGridMetres_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));

    // A 20 m leg due north, then 20 m due east: the project's own box pattern, half walked
    QVERIFY(QMetaObject::invokeMethod(stub.get(), "addItem", Qt::DirectConnection,
                                      Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0, 0.0))),
                                      Q_ARG(QVariant, 1)));
    QVERIFY(QMetaObject::invokeMethod(stub.get(), "addItem", Qt::DirectConnection,
                                      Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0, 90.0))),
                                      Q_ARG(QVariant, 2)));

    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QVERIFY(gridView->property("originKnown").toBool());

    // Guard the stand-in itself, so a failure below points at the grid rather than at the fake
    const QJSValue stubItems = stub->property("items").value<QJSValue>();
    QCOMPARE(stubItems.property(QStringLiteral("length")).toInt(), 2);
    QObject *const visualItems = stub->property("visualItems").value<QObject *>();
    QVERIFY2(visualItems, "the stand-in must expose a visualItems list");
    QCOMPARE(visualItems->property("count").toInt(), 2);

    // The property holds a JavaScript array, which only reads back as one through QJSValue
    const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QVERIFY2(points.isArray(), "missionPoints must be an array of grid offsets");
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 2);

    const QJSValue first = points.property(0);
    QVERIFY(qAbs(first.property(QStringLiteral("north")).toNumber() - 20.0) < 0.05);
    QVERIFY(qAbs(first.property(QStringLiteral("east")).toNumber()) < 0.05);
    QCOMPARE(first.property(QStringLiteral("sequence")).toInt(), 1);

    const QJSValue second = points.property(1);
    QVERIFY(qAbs(second.property(QStringLiteral("north")).toNumber()) < 0.05);
    QVERIFY(qAbs(second.property(QStringLiteral("east")).toNumber() - 20.0) < 0.05);
}

/// Removal goes by visual item index, which is not the sequence number on the marker's face. Using
/// the wrong one deletes a different waypoint than the operator pointed at, and the plan still looks
/// perfectly reasonable afterwards.
void LocalGridViewTest::_deleteRemovesTheSelectedWaypoint_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));

    // Sequence numbers deliberately do not start at the index: a real plan carries a settings item
    // ahead of the waypoints, so the two run offset from each other.
    for (int i = 0; i < 3; i++) {
        QVERIFY(QMetaObject::invokeMethod(
            stub.get(), "addItem", Qt::DirectConnection,
            Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0 * (i + 1), 0.0))),
            Q_ARG(QVariant, i + 1)));
    }
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection, Q_ARG(QVariant, 1)));
    QCOMPARE(gridView->property("selectedWaypointIndex").toInt(), 1);

    QVariant removed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "removeSelectedWaypoint", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, removed)));
    QVERIFY(removed.toBool());
    QCOMPARE(stub->property("removeCount").toInt(), 1);
    QVERIFY2(stub->property("removedIndex").toInt() == 1, "removal must use the visual item index");

    // Cleared, because removal renumbers everything after it: a held selection would name a
    // different waypoint than the one that was on screen.
    QCOMPARE(gridView->property("selectedWaypointIndex").toInt(), -1);
}

/// Nothing selected, or a selection left over from a plan that has since shrunk, must not delete
/// whatever happens to sit at that index now.
void LocalGridViewTest::_deleteWithoutASelection_doesNothing_test()
{
    QVERIFY(vehicle());
    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QCOMPARE(gridView->property("selectedWaypointIndex").toInt(), -1);

    QVariant removed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "removeSelectedWaypoint", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, removed)));
    QVERIFY(!removed.toBool());
    QCOMPARE(stub->property("removeCount").toInt(), 0);

    // An index past the end of a plan that shrank underneath the selection
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection, Q_ARG(QVariant, 5)));
    QVERIFY2(gridView->property("selectedWaypointIndex").toInt() == -1,
             "an index naming no waypoint must not become a selection");
}

/// Dragging a marker has to land the waypoint where it was dropped, in the frame it is flown in.
void LocalGridViewTest::_dragMovesTheWaypointToTheDroppedOffsets_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    QVERIFY(QMetaObject::invokeMethod(
        stub.get(), "addItem", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0, 0.0))),
        Q_ARG(QVariant, 1)));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QVariant moved;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "moveWaypointTo", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved),
                                      Q_ARG(QVariant, 0), Q_ARG(QVariant, -15.0), Q_ARG(QVariant, 35.0)));
    QVERIFY(moved.toBool());

    // Read back through the same conversion the grid draws with
    const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 1);
    QVERIFY(qAbs(points.property(0).property(QStringLiteral("north")).toNumber() - (-15.0)) < 0.05);
    QVERIFY(qAbs(points.property(0).property(QStringLiteral("east")).toNumber() - 35.0) < 0.05);

    // Without an origin there is no frame to move it in, so the waypoint must be left alone
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "moveWaypointTo", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved),
                                      Q_ARG(QVariant, 0), Q_ARG(QVariant, qQNaN()), Q_ARG(QVariant, 5.0)));
    QVERIFY2(!moved.toBool(), "a waypoint must not be moved to a position that is not a number");
}

/// Typing a bearing and a range is how a leg is briefed, so the grid accepts one. It has to land the
/// waypoint in the same place the equivalent offsets would.
void LocalGridViewTest::_bearingAndRangeAgreeWithOffsets_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    QVERIFY(QMetaObject::invokeMethod(
        stub.get(), "addItem", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0, 0.0))), Q_ARG(QVariant, 1)));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    // Due east at 30 m is 0 north, 30 east
    QVariant moved;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "moveWaypointToPolar", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved),
                                      Q_ARG(QVariant, 0), Q_ARG(QVariant, 90.0), Q_ARG(QVariant, 30.0)));
    QVERIFY(moved.toBool());

    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QVERIFY(qAbs(points.property(0).property(QStringLiteral("north")).toNumber()) < 0.05);
    QVERIFY(qAbs(points.property(0).property(QStringLiteral("east")).toNumber() - 30.0) < 0.05);

    // South west at 45 degrees past south: equal negative offsets
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "moveWaypointToPolar", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved),
                                      Q_ARG(QVariant, 0), Q_ARG(QVariant, 225.0), Q_ARG(QVariant, 28.2843)));
    QVERIFY(moved.toBool());

    points = gridView->property("missionPoints").value<QJSValue>();
    QVERIFY(qAbs(points.property(0).property(QStringLiteral("north")).toNumber() - (-20.0)) < 0.05);
    QVERIFY(qAbs(points.property(0).property(QStringLiteral("east")).toNumber() - (-20.0)) < 0.05);

    // A negative range would put the waypoint on the reciprocal bearing, which is not what anyone
    // typing one means
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "moveWaypointToPolar", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved),
                                      Q_ARG(QVariant, 0), Q_ARG(QVariant, 90.0), Q_ARG(QVariant, -10.0)));
    QVERIFY(!moved.toBool());
}

/// A route without a map is built one leg at a time -- "from there, ninety degrees for twenty
/// metres" -- so a leg is measured from the waypoint before, and from the origin for the first.
/// Measuring it from the origin throughout would silently turn every leg into a radial.
void LocalGridViewTest::_legIsMeasuredFromThePreviousWaypoint_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));

    // One waypoint 20 m due north, then a second somewhere that will be moved
    QVERIFY(QMetaObject::invokeMethod(
        stub.get(), "addItem", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0, 0.0))), Q_ARG(QVariant, 1)));
    QVERIFY(QMetaObject::invokeMethod(
        stub.get(), "addItem", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(5.0, 90.0))), Q_ARG(QVariant, 2)));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    // The leg to the first waypoint starts at the origin, since that is where the vehicle starts
    QVariant legStart;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "legStartFor", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, legStart), Q_ARG(QVariant, 0)));
    QVERIFY(qAbs(legStart.toMap().value(QStringLiteral("north")).toDouble()) < 1e-9);
    QVERIFY(qAbs(legStart.toMap().value(QStringLiteral("east")).toDouble()) < 1e-9);

    // The leg to the second starts at the first, not at the origin
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "legStartFor", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, legStart), Q_ARG(QVariant, 1)));
    QVERIFY(qAbs(legStart.toMap().value(QStringLiteral("north")).toDouble() - 20.0) < 0.05);

    // Ninety degrees for twenty metres, from a waypoint already 20 m north, lands at (20, 20).
    // Measured from the origin it would land at (0, 20) instead.
    QVariant moved;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "moveWaypointToLeg", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved),
                                      Q_ARG(QVariant, 1), Q_ARG(QVariant, 90.0), Q_ARG(QVariant, 20.0)));
    QVERIFY(moved.toBool());

    const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QVERIFY(qAbs(points.property(1).property(QStringLiteral("north")).toNumber() - 20.0) < 0.05);
    QVERIFY(qAbs(points.property(1).property(QStringLiteral("east")).toNumber() - 20.0) < 0.05);
}

/// A plan needs more than waypoints to fly itself. Each kind goes through the call the Plan view
/// makes for it, so an item added here is the same item as one added there -- a takeoff built by
/// insertSimpleMissionItem would be a waypoint wearing the wrong name.
void LocalGridViewTest::_takeoffAndLandingUseTheirOwnInsertions_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QVariant added;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addMissionItemAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, QStringLiteral("takeoff")),
                                      Q_ARG(QVariant, 0.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());
    QCOMPARE(stub->property("takeoffCount").toInt(), 1);
    QCOMPARE(stub->property("insertCount").toInt(), 0);

    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addMissionItemAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, QStringLiteral("land")),
                                      Q_ARG(QVariant, 5.0), Q_ARG(QVariant, 5.0)));
    QVERIFY(added.toBool());
    QCOMPARE(stub->property("landCount").toInt(), 1);

    // Once the plan holds something, a waypoint stays a waypoint
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addMissionItemAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, QStringLiteral("waypoint")),
                                      Q_ARG(QVariant, 10.0), Q_ARG(QVariant, 10.0)));
    QVERIFY(added.toBool());
    QCOMPARE(stub->property("insertCount").toInt(), 1);
    QVERIFY2(stub->property("takeoffCount").toInt() == 1, "only the first item may be turned into a takeoff");

    // Landing where the vehicle stands is a plain item whose command is changed afterwards, since
    // MissionController offers no insertion for it. 21 is MAV_CMD_NAV_LAND; the number is pinned
    // here because MAVLinkEnums exposes no values to QML and the source has to write it out.
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addMissionItemAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, QStringLiteral("landHere")),
                                      Q_ARG(QVariant, 40.0), Q_ARG(QVariant, -12.0)));
    QVERIFY(added.toBool());
    QCOMPARE(stub->property("insertCount").toInt(), 2);
    QObject *const landItem = stub->property("lastInsertedItem").value<QObject *>();
    QVERIFY(landItem);
    QVERIFY2(landItem->property("command").toInt() == static_cast<int>(MAV_CMD_NAV_LAND),
             "a land-here item must carry the landing command, not the waypoint one");

    // The same origin guard covers every kind: without one none of them may be placed
    gridView->setProperty("vehicle", QVariant::fromValue<Vehicle *>(nullptr));
    QVERIFY(!gridView->property("originKnown").toBool());
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addMissionItemAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, QStringLiteral("takeoff")),
                                      Q_ARG(QVariant, 0.0), Q_ARG(QVariant, 0.0)));
    QVERIFY2(!added.toBool(), "no item may be placed against an unanchored grid");
    QCOMPARE(stub->property("takeoffCount").toInt(), 1);
}

/// A mission whose first item is a plain waypoint does not climb -- the aircraft sits there. Starting
/// every plan with a takeoff removes a step nobody remembers until the one flight they forget it.
///
/// The takeoff goes on the origin and the point that was clicked still becomes the item that was
/// asked for. Consuming the click instead cost the operator the waypoint they had just placed, and
/// left the takeoff sitting wherever they happened to have aimed.
void LocalGridViewTest::_firstItemOfAnEmptyPlanBecomesATakeoff_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QVariant added;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 25.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());
    QCOMPARE(stub->property("takeoffCount").toInt(), 1);
    QCOMPARE(stub->property("insertCount").toInt(), 1);

    // The takeoff on the origin, the waypoint where it was asked for
    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 2);
    QVERIFY(qAbs(points.property(0).property(QStringLiteral("north")).toNumber()) < 0.05);
    QVERIFY(qAbs(points.property(0).property(QStringLiteral("east")).toNumber()) < 0.05);
    QVERIFY(points.property(0).property(QStringLiteral("isPinned")).toBool());
    QVERIFY(qAbs(points.property(1).property(QStringLiteral("north")).toNumber() - 25.0) < 0.05);

    // The waypoint is what is selected, so its altitude can be set without hunting for it -- not the
    // takeoff that was inserted underneath it
    QCOMPARE(gridView->property("selectedWaypointIndex").toInt(), 1);

    // Every point after it is a plain waypoint. Only an empty plan gains a takeoff.
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());
    QCOMPARE(stub->property("takeoffCount").toInt(), 1);
    QCOMPARE(stub->property("insertCount").toInt(), 2);

    // A plan that already carries a takeoff must not gain a second one from an empty-looking list
    stub->setProperty("isInsertTakeoffValid", false);
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 40.0), Q_ARG(QVariant, 0.0)));
    QCOMPARE(stub->property("takeoffCount").toInt(), 1);
}

/// The plan's first visual item is the mission settings, which carries the planned home position as
/// its coordinate. Drawn as a waypoint it lands on top of the origin marker, and -- the failure this
/// exists for -- makes a plan that has just been cleared look like it already holds a route, so the
/// next plan never gains its takeoff and the vehicle refuses the mission it is given.
void LocalGridViewTest::_homeItemIsNotDrawnAsAWaypoint_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));

    // What a cleared plan holds: the settings item and nothing else
    QVERIFY(QMetaObject::invokeMethod(stub.get(), "addHomeItem", Qt::DirectConnection,
                                      Q_ARG(QVariant, QVariant::fromValue(origin))));
    stub->setProperty("takeoffLandsOn", QVariant::fromValue(origin));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QObject *const visualItems = stub->property("visualItems").value<QObject *>();
    QVERIFY(visualItems);
    QCOMPARE(visualItems->property("count").toInt(), 1);

    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QVERIFY2(points.property(QStringLiteral("length")).toInt() == 0,
             "the home position is not a waypoint of the plan");

    // And so the next point placed still starts the plan with a takeoff
    QVariant added;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());
    QCOMPARE(stub->property("takeoffCount").toInt(), 1);

    // The home item stays at the front of the list, so the indices the grid hands back have to be
    // the ones into visualItems rather than into the points it drew
    points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 2);
    QCOMPARE(points.property(0).property(QStringLiteral("index")).toInt(), 1);
    QCOMPARE(points.property(1).property(QStringLiteral("index")).toInt(), 2);
}

/// A multirotor climbs in place whatever coordinate is uploaded with NAV_TAKEOFF, so a takeoff drawn
/// anywhere but where the aircraft is standing is a picture of a departure it will not fly. It goes
/// on the origin wherever the operator clicked, and it stays there.
void LocalGridViewTest::_takeoffIsHeldOnTheOrigin_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));

    // A home position 40 m away from the estimator origin, which is where MissionController would
    // put the takeoff if the grid left it there
    stub->setProperty("takeoffLandsOn",
                      QVariant::fromValue(origin.atDistanceAndAzimuth(40.0, 90.0)));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QVariant added;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addMissionItemAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, QStringLiteral("takeoff")),
                                      Q_ARG(QVariant, 60.0), Q_ARG(QVariant, -30.0)));
    QVERIFY(added.toBool());

    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 1);
    QVERIFY2(qAbs(points.property(0).property(QStringLiteral("north")).toNumber()) < 0.05,
             "the takeoff belongs on the origin, not on the point that was clicked");
    QVERIFY(qAbs(points.property(0).property(QStringLiteral("east")).toNumber()) < 0.05);

    // Dragged or typed at, it stays where it is
    QVariant moved;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "moveWaypointTo", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved),
                                      Q_ARG(QVariant, 0), Q_ARG(QVariant, 12.0), Q_ARG(QVariant, 8.0)));
    QVERIFY2(!moved.toBool(), "a takeoff may not be moved off the origin");

    QVariant pinned;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "waypointIsPinned", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, pinned), Q_ARG(QVariant, 0)));
    QVERIFY(pinned.toBool());

    // And the marker itself refuses the drag, so it is never reported in the first place. This also
    // pins down that markers are built at all: they are the only part of the plan that can be
    // pointed at, and how the Repeater is modelled decides whether they exist.
    QVariant marker;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "waypointMarkerAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, marker), Q_ARG(QVariant, 0)));
    QObject *const takeoffMarker = marker.value<QObject *>();
    QVERIFY2(takeoffMarker, "a placed waypoint must have a marker that can be picked up");
    QCOMPARE(takeoffMarker->property("sequenceNumber").toInt(), 1);
    QVERIFY2(!takeoffMarker->property("draggable").toBool(), "the takeoff marker must not be draggable");

    points = gridView->property("missionPoints").value<QJSValue>();
    QVERIFY(qAbs(points.property(0).property(QStringLiteral("north")).toNumber()) < 0.05);
    QVERIFY(qAbs(points.property(0).property(QStringLiteral("east")).toNumber()) < 0.05);
}

/// Turning the last waypoint of a pattern into a landing is the common edit. Doing it in place keeps
/// the offsets that were the point of positioning it; deleting and re-adding throws them away.
void LocalGridViewTest::_itemTypeChangesInPlace_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    QVERIFY(QMetaObject::invokeMethod(
        stub.get(), "addItem", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0, 90.0))), Q_ARG(QVariant, 1)));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    // The numbers this view exists to show must survive the change
    const QJSValue before = gridView->property("missionPoints").value<QJSValue>();
    const double eastBefore = before.property(0).property(QStringLiteral("east")).toNumber();
    QVERIFY(qAbs(eastBefore - 20.0) < 0.05);

    QVariant changed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "setWaypointCommand", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, changed),
                                      Q_ARG(QVariant, 0),
                                      Q_ARG(QVariant, static_cast<int>(MAV_CMD_NAV_LAND))));
    QVERIFY(changed.toBool());

    QVariant command;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "waypointCommand", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, command), Q_ARG(QVariant, 0)));
    QCOMPARE(command.toInt(), static_cast<int>(MAV_CMD_NAV_LAND));

    const QJSValue after = gridView->property("missionPoints").value<QJSValue>();
    QVERIFY2(qAbs(after.property(0).property(QStringLiteral("east")).toNumber() - eastBefore) < 1e-9,
             "changing the type must not move the item");

    // An index naming nothing must not write a command into whatever sits there now
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "setWaypointCommand", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, changed),
                                      Q_ARG(QVariant, 7), Q_ARG(QVariant, static_cast<int>(MAV_CMD_NAV_TAKEOFF))));
    QVERIFY(!changed.toBool());
}

/// A pattern is flown at one height, and comparing drift at two heights means retyping every
/// waypoint otherwise. Items that carry no altitude of their own are stepped over rather than
/// stopping the sweep at the first one.
void LocalGridViewTest::_altitudeCanBeAppliedToEveryItem_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));

    for (int i = 0; i < 3; i++) {
        QVERIFY(QMetaObject::invokeMethod(
            stub.get(), "addItem", Qt::DirectConnection,
            Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0 * (i + 1), 0.0))),
            Q_ARG(QVariant, i + 1)));
    }
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QVariant changed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "setAllWaypointAltitudes", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, changed), Q_ARG(QVariant, 8.0)));
    QCOMPARE(changed.toInt(), 3);

    for (int i = 0; i < 3; i++) {
        QVariant fact;
        QVERIFY(QMetaObject::invokeMethod(gridView.get(), "waypointAltitudeFact", Qt::DirectConnection,
                                          Q_RETURN_ARG(QVariant, fact), Q_ARG(QVariant, i)));
        QObject *const altitude = fact.value<QObject *>();
        QVERIFY(altitude);
        QCOMPARE(altitude->property("rawValue").toDouble(), 8.0);
    }

    // An altitude that is not a number must leave every item alone rather than blanking the plan
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "setAllWaypointAltitudes", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, changed), Q_ARG(QVariant, qQNaN())));
    QCOMPARE(changed.toInt(), 0);
}

/// The regression this exists for. A warning shown only for the selected item is not a warning: the
/// item that ran a flight away was the takeoff, while the item on screen was the landing. The scan
/// has to cover the whole plan and name what it found.
void LocalGridViewTest::_everyItemAboveTheCeilingIsFound_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    for (int i = 0; i < 3; i++) {
        QVERIFY(QMetaObject::invokeMethod(
            stub.get(), "addItem", Qt::DirectConnection,
            Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0 * (i + 1), 0.0))),
            Q_ARG(QVariant, i + 1)));
    }
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    // The mock vehicle's estimator takes its height from the barometer and nothing from optical
    // flow, so no ceiling applies and nothing may be reported however high the plan goes
    QVERIFY(!gridView->property("altitudeLimitKnown").toBool());
    QVariant applied;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "setAllWaypointAltitudes", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, applied), Q_ARG(QVariant, 500.0)));
    QVERIFY2(gridView->property("itemsAboveAltitudeLimit").value<QJSValue>()
                 .property(QStringLiteral("length")).toInt() == 0,
             "a vehicle that does not depend on the rangefinder has no ceiling to breach");
}

/// Builds one list row on its own.
///
/// The editor is loaded on demand rather than built for every item and hidden. A row that built it
/// anyway would look identical on screen and cost a set of live bindings -- on the transform, the
/// altitude limit and the item's altitude fact -- for every leg of the pattern, all recomputing on
/// each pan of the grid.
void LocalGridViewTest::_missionItemRowOpensOnlyWhenCurrent_test()
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QGroundControl.FlyView

        LocalGridMissionItemRow { }
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    const QScopedPointer<QObject> row(component.create());
    QVERIFY2(row, qPrintable(component.errorString()));

    // A control that exists only inside the editor, so finding it is the editor being present
    const auto editorLoaded = [&row]() {
        return row->findChild<QObject *>(QStringLiteral("localGrid_applyAltitudeToAllButton")) != nullptr;
    };

    QVERIFY2(!editorLoaded(), "a row that is not the current item carries no editor");
    QVERIFY2(!row->findChild<QObject *>(QStringLiteral("localGrid_rowDeleteButton"))->property("visible").toBool(),
             "delete belongs to the row being edited, not to every line of the list");

    row->setProperty("isCurrentItem", true);
    QVERIFY2(editorLoaded(), "the current item opens into its fields");
    QVERIFY(row->findChild<QObject *>(QStringLiteral("localGrid_rowDeleteButton"))->property("visible").toBool());

    // And it is given up again, rather than every row that has ever been opened staying loaded.
    // Waited on rather than read straight away: the Loader hands the old editor to the event loop to
    // delete, so it is still findable for the rest of this turn.
    row->setProperty("isCurrentItem", false);
    QTRY_VERIFY_WITH_TIMEOUT(!editorLoaded(), TestTimeout::shortMs());
}

/// The collapsed row is the whole list at a glance -- takeoff, waypoint, waypoint, land. It takes
/// QGC's own name for the command rather than mapping the three types the grid can create, so an
/// item planned elsewhere is not renamed into one of them.
void LocalGridViewTest::_missionItemRowNamesTheItemItHolds_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    // A takeoff and a waypoint, so the name has to follow the item rather than being the same word
    // for every row
    QVariant inserted;
    QVERIFY(QMetaObject::invokeMethod(stub.get(), "insertTakeoffItem", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, inserted),
                                      Q_ARG(QVariant, QVariant::fromValue(origin)),
                                      Q_ARG(QVariant, 1), Q_ARG(QVariant, true)));
    QVERIFY(QMetaObject::invokeMethod(
        stub.get(), "addItem", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0, 0.0))),
        Q_ARG(QVariant, 2)));

    const auto nameAt = [&gridView](int index) {
        QVariant name;
        return QMetaObject::invokeMethod(gridView.get(), "waypointCommandName", Qt::DirectConnection,
                                         Q_RETURN_ARG(QVariant, name), Q_ARG(QVariant, index))
                   ? name.toString()
                   : QStringLiteral("<not invoked>");
    };

    QCOMPARE(nameAt(0), QStringLiteral("Takeoff"));
    QCOMPARE(nameAt(1), QStringLiteral("Waypoint"));

    // Out of range answers with nothing rather than inventing a name for an item that is not there
    QCOMPARE(nameAt(99), QString());
}

/// Counts the items named @a objectName anywhere under @a root.
///
/// Walks childItems() rather than using findChildren(). A ListView gives its delegates a parent
/// *item* but no QObject parent, so the whole list of rows is invisible to a QObject-tree search --
/// which reads as a list that built nothing rather than as the wrong kind of search.
static QList<QQuickItem *> collectItemsNamed(QQuickItem *root, const QString &objectName)
{
    QList<QQuickItem *> found;
    if (!root) {
        return found;
    }

    if (root->objectName() == objectName) {
        found.append(root);
    }
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *const child : children) {
        found.append(collectItemsNamed(child, objectName));
    }
    return found;
}

static int countItemsNamed(QQuickItem *root, const QString &objectName)
{
    return static_cast<int>(collectItemsNamed(root, objectName).count());
}

/// Shows @a list in a window and waits for it to lay out.
///
/// QtQuick.Layouts size their children during the polish pass, which only a window drives. Without
/// one the list holds a model count and builds no rows at all, and a test reading only that count
/// would pass against a list that renders nothing.
///     @return false, having already failed the test, if the window never came up
bool LocalGridViewTest::_showInWindow(QQuickWindow &window, QObject *list)
{
    auto *const item = qobject_cast<QQuickItem *>(list);
    if (!item) {
        return false;
    }

    item->setParentItem(window.contentItem());
    window.resize(400, 700);
    window.show();
    return QTest::qWaitForWindowExposed(&window);
}

/// Builds the list against a real grid, which is the only way to check the two agree. The rows come
/// from the same missionPoints the markers do, so what this really pins is that the list inherits
/// the grid's rules rather than filtering the plan a second way of its own -- in particular that the
/// home position, which is a visual item but not a waypoint, is not listed as one.
void LocalGridViewTest::_missionListShowsOneRowPerDrawnItem_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));

    QVERIFY(QMetaObject::invokeMethod(stub.get(), "addHomeItem", Qt::DirectConnection,
                                      Q_ARG(QVariant, QVariant::fromValue(origin))));
    for (int i = 0; i < 3; i++) {
        QVERIFY(QMetaObject::invokeMethod(
            stub.get(), "addItem", Qt::DirectConnection,
            Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(15.0 * (i + 1), 30.0))),
            Q_ARG(QVariant, i + 1)));
    }
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QQmlComponent listComponent(&gridViewEngine);
    listComponent.setData(R"(
        import QtQuick
        import QGroundControl.FlyView

        LocalGridMissionList { width: 300; height: 600 }
    )", QUrl());
    QVERIFY2(listComponent.isReady(), qPrintable(listComponent.errorString()));

    const QScopedPointer<QObject> list(listComponent.create());
    QVERIFY2(list, qPrintable(listComponent.errorString()));
    list->setProperty("gridView", QVariant::fromValue(gridView.get()));

    QQuickWindow window;
    QVERIFY(_showInWindow(window, list.get()));

    // Rows actually built, not just a model count: the count would read 3 against a list that
    // rendered nothing at all
    const auto builtRowCount = [&list]() {
        return countItemsNamed(qobject_cast<QQuickItem *>(list.get()),
                               QStringLiteral("localGrid_rowDeleteButton"));
    };

    QObject *const visualItems = stub->property("visualItems").value<QObject *>();
    QVERIFY(visualItems);
    QCOMPARE(visualItems->property("count").toInt(), 4);
    QTRY_COMPARE_WITH_TIMEOUT(builtRowCount(), 3, TestTimeout::mediumMs());
    QCOMPARE(list->property("rowCount").toInt(), 3);

    // And it follows the plan rather than being read once at build time
    QVERIFY(QMetaObject::invokeMethod(
        stub.get(), "addItem", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(80.0, 30.0))),
        Q_ARG(QVariant, 4)));
    QTRY_COMPARE_WITH_TIMEOUT(builtRowCount(), 4, TestTimeout::mediumMs());
    QCOMPARE(list->property("rowCount").toInt(), 4);
}

/// One row open at a time, and it is the grid's selected waypoint. A list keeping a selection of its
/// own would let the open row and the highlighted marker name different items, which on a grid flown
/// without a map is the operator editing one waypoint while looking at another.
void LocalGridViewTest::_missionListOpensTheRowTheGridHasSelected_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    for (int i = 0; i < 2; i++) {
        QVERIFY(QMetaObject::invokeMethod(
            stub.get(), "addItem", Qt::DirectConnection,
            Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(15.0 * (i + 1), 30.0))),
            Q_ARG(QVariant, i + 1)));
    }
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QQmlComponent listComponent(&gridViewEngine);
    listComponent.setData(R"(
        import QtQuick
        import QGroundControl.FlyView

        LocalGridMissionList { width: 300; height: 600 }
    )", QUrl());
    QVERIFY2(listComponent.isReady(), qPrintable(listComponent.errorString()));

    const QScopedPointer<QObject> list(listComponent.create());
    QVERIFY2(list, qPrintable(listComponent.errorString()));
    list->setProperty("gridView", QVariant::fromValue(gridView.get()));

    QQuickWindow window;
    QVERIFY(_showInWindow(window, list.get()));

    // Only the open row carries an editor, so counting them counts the open rows
    const auto openRowCount = [&list]() {
        return countItemsNamed(qobject_cast<QQuickItem *>(list.get()),
                               QStringLiteral("localGrid_applyAltitudeToAllButton"));
    };
    const auto builtRowCount = [&list]() {
        return countItemsNamed(qobject_cast<QQuickItem *>(list.get()),
                               QStringLiteral("localGrid_rowDeleteButton"));
    };

    QTRY_COMPARE_WITH_TIMEOUT(builtRowCount(), 2, TestTimeout::mediumMs());

    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "clearWaypointSelection", Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(openRowCount(), 0, TestTimeout::mediumMs());

    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, 1)));
    QTRY_COMPARE_WITH_TIMEOUT(openRowCount(), 1, TestTimeout::mediumMs());

    // Moving the selection moves the open row rather than opening a second one
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, 0)));
    QTRY_COMPARE_WITH_TIMEOUT(openRowCount(), 1, TestTimeout::mediumMs());
}

/// The grid shows the plan as a list, and exactly one of them: a second would be two editors for the
/// same waypoint, each able to disagree with the other about which one is open.
void LocalGridViewTest::_gridShowsThePlanAsAList_test()
{
    QVERIFY(vehicle());
    MAKE_GRID_VIEW(gridView);

    auto *const gridItem = qobject_cast<QQuickItem *>(gridView.get());
    QVERIFY(gridItem);

    QCOMPARE(countItemsNamed(gridItem, QStringLiteral("localGrid_missionList")), 1);
}

/// Which waypoint the vehicle is flying to is not which waypoint the operator is editing, and the
/// grid draws them differently -- green fill for the vehicle's target, an outline for the selection.
///
/// Read off the controller's currentMissionIndex, which comes from the vehicle's own mission
/// manager, rather than off the items' isCurrentItem. That flag is written twice over in the fly
/// view: the vehicle advancing sets it, but so does inserting an item, so a freshly placed waypoint
/// marked itself as the one being flown to while the aircraft was still standing on the origin.
void LocalGridViewTest::_listMarksTheWaypointTheVehicleIsFlyingTo_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    for (int i = 0; i < 3; i++) {
        QVERIFY(QMetaObject::invokeMethod(
            stub.get(), "addItem", Qt::DirectConnection,
            Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(15.0 * (i + 1), 30.0))),
            Q_ARG(QVariant, i + 1)));
    }
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QQuickWindow window;
    QVERIFY(_showInWindow(window, gridView.get()));

    auto *const gridItem = qobject_cast<QQuickItem *>(gridView.get());
    QVERIFY(gridItem);

    const auto targetSequences = [gridItem]() {
        QList<int> sequences;
        const QList<QQuickItem *> rows = collectItemsNamed(gridItem, QStringLiteral("localGrid_missionItemRow"));
        for (QQuickItem *const row : rows) {
            if (row->property("isVehicleTarget").toBool()) {
                sequences.append(row->property("sequenceNumber").toInt());
            }
        }
        return sequences;
    };

    QTRY_COMPARE_WITH_TIMEOUT(countItemsNamed(gridItem, QStringLiteral("localGrid_missionItemRow")), 3,
                              TestTimeout::mediumMs());
    QVERIFY2(targetSequences().isEmpty(), "a plan nobody is flying marks no target");

    // The vehicle reports it is flying to the second item
    stub->setProperty("currentMissionIndex", 2);
    QTRY_COMPARE_WITH_TIMEOUT(targetSequences(), QList<int>({ 2 }), TestTimeout::mediumMs());

    // Selecting a different row for editing leaves the target where it is
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, 0)));
    QCOMPARE(targetSequences(), QList<int>({ 2 }));

    // And an item being inserted does not steal the mark. MissionController makes a new item the
    // current one, which is what made a waypoint placed on the ground look like the one being
    // flown to.
    QObject *const visualItems = stub->property("visualItems").value<QObject *>();
    QVERIFY(visualItems);
    QVariant itemVar;
    QVERIFY(QMetaObject::invokeMethod(visualItems, "get", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, itemVar), Q_ARG(QVariant, 2)));
    QObject *const thirdItem = itemVar.value<QObject *>();
    QVERIFY(thirdItem);
    thirdItem->setProperty("isCurrentItem", true);

    QCOMPARE(targetSequences(), QList<int>({ 2 }));
}

/// An empty plan has nothing to list, and a panel standing open to say so is covering the one
/// picture the operator has. It opens itself when the plan gets its first item and folds away again
/// when the last one goes.
void LocalGridViewTest::_listStaysFoldedUntilThePlanHasSomething_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QQuickWindow window;
    QVERIFY(_showInWindow(window, gridView.get()));

    auto *const gridItem = qobject_cast<QQuickItem *>(gridView.get());
    QVERIFY(gridItem);
    const QList<QQuickItem *> panels = collectItemsNamed(gridItem, QStringLiteral("localGrid_missionList"));
    QCOMPARE(panels.count(), 1);
    QQuickItem *const panel = panels.first();

    QVERIFY2(panel->property("collapsed").toBool(), "an empty plan leaves the panel folded");
    const qreal foldedHeight = panel->property("height").toReal();
    QVERIFY(foldedHeight > 0);

    QVERIFY(QMetaObject::invokeMethod(
        stub.get(), "addItem", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0, 30.0))),
        Q_ARG(QVariant, 1)));

    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("collapsed").toBool(), TestTimeout::mediumMs());
    // Waited on: unfolding and the layout settling to the new height are separate passes
    QTRY_VERIFY2_WITH_TIMEOUT(panel->property("height").toReal() > foldedHeight,
                              "an opened panel is taller than its own header", TestTimeout::mediumMs());

    // Clearing the plan puts it back, rather than leaving an open panel listing nothing
    QVERIFY(QMetaObject::invokeMethod(stub.get(), "removeVisualItem", Qt::DirectConnection,
                                      Q_ARG(QVariant, 0)));
    QTRY_VERIFY_WITH_TIMEOUT(panel->property("collapsed").toBool(), TestTimeout::mediumMs());
    QTRY_COMPARE_WITH_TIMEOUT(panel->property("height").toReal(), foldedHeight, TestTimeout::mediumMs());
}

/// The panel is as tall as the rows it holds, and only clamps once the plan outgrows the room left
/// below it. Stretched to fill instead, the layout had nothing that wanted the extra height and
/// spread it between the header and the empty-plan line, stranding both in the middle of a tall box.
void LocalGridViewTest::_listIsAsTallAsItsRowsUntilItRunsOutOfRoom_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QQuickWindow window;
    QVERIFY(_showInWindow(window, gridView.get()));

    auto *const gridItem = qobject_cast<QQuickItem *>(gridView.get());
    QVERIFY(gridItem);
    QQuickItem *const panel = collectItemsNamed(gridItem, QStringLiteral("localGrid_missionList")).value(0);
    QVERIFY(panel);

    const auto addItems = [&stub, &origin](int count, int firstSequence) {
        for (int i = 0; i < count; i++) {
            const bool added = QMetaObject::invokeMethod(
                stub.get(), "addItem", Qt::DirectConnection,
                Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(10.0 * (i + 1), 30.0))),
                Q_ARG(QVariant, firstSequence + i));
            if (!added) {
                return false;
            }
        }
        return true;
    };

    QVERIFY(addItems(2, 1));
    QTRY_COMPARE_WITH_TIMEOUT(panel->property("rowCount").toInt(), 2, TestTimeout::mediumMs());

    const qreal twoRowHeight = panel->property("height").toReal();
    const qreal maximumHeight = panel->property("maximumHeight").toReal();
    QVERIFY(maximumHeight > 0);
    QVERIFY2(twoRowHeight < maximumHeight,
             "two rows must not fill the whole view: the panel follows its contents");

    // Enough rows to outgrow the room below the readout
    QVERIFY(addItems(40, 3));
    QTRY_COMPARE_WITH_TIMEOUT(panel->property("rowCount").toInt(), 42, TestTimeout::mediumMs());

    QTRY_VERIFY_WITH_TIMEOUT(panel->property("height").toReal() > twoRowHeight, TestTimeout::mediumMs());
    QVERIFY2(panel->property("height").toReal() <= maximumHeight,
             "the panel stops at the bottom of the view rather than running off it");

    // Clamped means the rows have to scroll, which is the whole point of stopping there
    QQuickItem *const listView = collectItemsNamed(panel, QStringLiteral("localGrid_missionListView")).value(0);
    QVERIFY(listView);
    QVERIFY2(listView->property("contentHeight").toReal() > listView->property("height").toReal(),
             "a clamped list must be scrollable, or the rows past the fold cannot be reached");
}

/// What an item *is* belongs on the row's header, beside its number, not down among its position
/// fields -- and it is the Plan view's arrangement, which is the whole point of listing items this
/// way. A closed row names its type; the open one turns that name into the control that changes it.
void LocalGridViewTest::_typeIsChangedFromTheRowHeader_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    for (int i = 0; i < 2; i++) {
        QVERIFY(QMetaObject::invokeMethod(
            stub.get(), "addItem", Qt::DirectConnection,
            Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(15.0 * (i + 1), 30.0))),
            Q_ARG(QVariant, i + 1)));
    }
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QQuickWindow window;
    QVERIFY(_showInWindow(window, gridView.get()));

    auto *const gridItem = qobject_cast<QQuickItem *>(gridView.get());
    QVERIFY(gridItem);

    const auto visibleCount = [gridItem](const QString &objectName) {
        int count = 0;
        const QList<QQuickItem *> items = collectItemsNamed(gridItem, objectName);
        for (QQuickItem *const item : items) {
            if (item->property("visible").toBool()) {
                count++;
            }
        }
        return count;
    };

    QTRY_COMPARE_WITH_TIMEOUT(countItemsNamed(gridItem, QStringLiteral("localGrid_missionItemRow")), 2,
                              TestTimeout::mediumMs());
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "clearWaypointSelection", Qt::DirectConnection));

    QTRY_COMPARE_WITH_TIMEOUT(visibleCount(QStringLiteral("localGrid_rowTypeLabel")), 2, TestTimeout::mediumMs());
    QCOMPARE(visibleCount(QStringLiteral("localGrid_rowTypeCombo")), 0);

    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, 1)));
    QTRY_COMPARE_WITH_TIMEOUT(visibleCount(QStringLiteral("localGrid_rowTypeCombo")), 1, TestTimeout::mediumMs());
    QVERIFY2(visibleCount(QStringLiteral("localGrid_rowTypeLabel")) == 1,
             "the closed row still names its type while the open one is being changed");

    // The editor below no longer carries a second copy of the same control
    QCOMPARE(countItemsNamed(gridItem, QStringLiteral("localGrid_rowTypeCombo")), 2);
}

/// A waypoint added to a plan longer than the panel lands below the fold, and the operator has to go
/// looking for the fields they just asked for. The open row is brought into view instead.
void LocalGridViewTest::_selectedRowIsBroughtIntoView_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    for (int i = 0; i < 40; i++) {
        QVERIFY(QMetaObject::invokeMethod(
            stub.get(), "addItem", Qt::DirectConnection,
            Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(5.0 * (i + 1), 30.0))),
            Q_ARG(QVariant, i + 1)));
    }
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QQuickWindow window;
    QVERIFY(_showInWindow(window, gridView.get()));

    auto *const gridItem = qobject_cast<QQuickItem *>(gridView.get());
    QVERIFY(gridItem);
    QQuickItem *const flickable = collectItemsNamed(gridItem, QStringLiteral("localGrid_missionListView")).value(0);
    QVERIFY(flickable);

    QTRY_VERIFY_WITH_TIMEOUT(flickable->property("contentHeight").toReal()
                                 > flickable->property("height").toReal(),
                             TestTimeout::mediumMs());
    QCOMPARE(flickable->property("contentY").toReal(), 0.0);

    // The last item is well past the bottom of the panel
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, 39)));
    QTRY_VERIFY2_WITH_TIMEOUT(flickable->property("contentY").toReal() > 0.0,
                              "the open row must be scrolled into view, not left below the fold",
                              TestTimeout::mediumMs());

    // And back up again for one above the top of the view
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, 0)));
    QTRY_COMPARE_WITH_TIMEOUT(flickable->property("contentY").toReal(), 0.0, TestTimeout::mediumMs());
}

/// Builds the drift watcher on its own, bound to @a vehicle.
/// @return the created object, or nullptr with @a error describing why not
static QObject *createOriginDrift(QQmlComponent &component, Vehicle *vehicle, QString &error)
{
    component.setData(R"(
        import QtQuick
        import QGroundControl.FlyView

        LocalGridOriginDrift { }
    )", QUrl());
    if (!component.isReady()) {
        error = component.errorString();
        return nullptr;
    }

    QObject *const drift = component.create();
    if (!drift) {
        error = component.errorString();
        return nullptr;
    }

    drift->setProperty("vehicle", QVariant::fromValue(vehicle));
    return drift;
}

/// A correction is sent as a coordinate, and the grid is the only thing that knows which coordinate
/// the operator pointed at. Swapping north for east here, or losing a sign, puts the aircraft's
/// believed position on the wrong side of the field -- and the vehicle takes it, because what arrives
/// is a perfectly ordinary coordinate. Nothing downstream can catch this, which is why it is asserted
/// on the same geodesic terms the plan itself is.
void LocalGridViewTest::_gridOffsetsBecomeTheCoordinateACorrectionSends_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QVERIFY(gridView->property("originKnown").toBool());

    // Standing on the origin: the correction the operator reaches for after carrying a drifted
    // aircraft back to where it took off from, and the one the origin marker offers in one click
    QVariant atOrigin;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "coordinateAtOffsets", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, atOrigin),
                                      Q_ARG(QVariant, 0.0), Q_ARG(QVariant, 0.0)));
    const QGeoCoordinate originPoint = atOrigin.value<QGeoCoordinate>();
    QVERIFY(originPoint.isValid());
    QVERIFY2(origin.distanceTo(originPoint) < 0.05, "the origin has to correct to the origin itself");

    // And an arbitrary point on the grid, checked as a distance and a bearing rather than as a
    // latitude, so a transposed pair cannot pass by landing somewhere plausible
    QVariant atPoint;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "coordinateAtOffsets", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, atPoint),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, -10.0)));
    const QGeoCoordinate point = atPoint.value<QGeoCoordinate>();
    QVERIFY(point.isValid());
    QVERIFY2(qAbs(origin.distanceTo(point) - std::hypot(20.0, -10.0)) < 0.05,
             "the point must be the stated distance from the origin");
    // 20 north and 10 west is a bearing of about 333 degrees
    const double bearing = origin.azimuthTo(point);
    QVERIFY2(qAbs(bearing - 333.435) < 0.5, qPrintable(QStringLiteral("bearing came out %1").arg(bearing)));
}

/// The remedy for a drifted frame on firmware that will not take a correction: move the whole plan by
/// the offset the frame has slid, keeping its shape, so the pattern is flown over the ground it was
/// drawn on. It changes the plan rather than the aircraft, which is why it works where no position
/// reset exists.
///
/// The takeoff stays where it is. It is pinned to the origin because a multirotor climbs in place
/// whatever coordinate is uploaded with it, and moving one drags the planned home position with it.
void LocalGridViewTest::_planIsMovedByOneOffsetKeepingItsShape_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QQmlComponent planComponent(&gridViewEngine);
    QString planError;
    const QScopedPointer<QObject> plan(createPlanMasterControllerStub(planComponent, planError));
    QVERIFY2(plan, qPrintable(planError));
    gridView->setProperty("planMasterController", QVariant::fromValue(plan.get()));

    // Two legs of the project's own box. The first placement also gives the plan its takeoff, pinned
    // to the origin, so the list under test carries one of each kind.
    const auto place = [&gridView](double north, double east) {
        QVariant added;
        const bool called = QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                                      Q_RETURN_ARG(QVariant, added),
                                                      Q_ARG(QVariant, north), Q_ARG(QVariant, east));
        return called && added.toBool();
    };
    QVERIFY(place(20.0, 0.0));
    QVERIFY(place(20.0, 20.0));

    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 3);
    QVERIFY2(points.property(0).property(QStringLiteral("isPinned")).toBool(),
             "the first item must be the takeoff, or this is testing something else");

    const auto pointIsAbout = [&points](int index, double north, double east) {
        const QJSValue point = points.property(index);
        return (qAbs(point.property(QStringLiteral("north")).toNumber() - north) < 0.05)
               && (qAbs(point.property(QStringLiteral("east")).toNumber() - east) < 0.05);
    };

    // Deliberately not a round number and negative on one axis: a sign dropped here flies the pattern
    // out by twice the drift rather than putting it back
    QVariant moved;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "offsetMission", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved),
                                      Q_ARG(QVariant, 3.25), Q_ARG(QVariant, -1.5)));
    QCOMPARE(moved.toInt(), 2);

    points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 3);
    QVERIFY2(pointIsAbout(0, 0.0, 0.0), "the takeoff is anchored to the origin and may not be moved");
    QVERIFY2(pointIsAbout(1, 23.25, -1.5), "every waypoint moves by the offset it was given");
    QVERIFY2(pointIsAbout(2, 23.25, 18.5), "and by the same offset, or the pattern changes shape");

    // Mid-transfer the fly view's list is about to be overwritten by the vehicle's copy, so a plan
    // moved now is a plan moved into the bin -- and the operator would have no way of knowing
    plan->setProperty("syncInProgress", true);
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "offsetMission", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved),
                                      Q_ARG(QVariant, 5.0), Q_ARG(QVariant, 5.0)));
    QCOMPARE(moved.toInt(), 0);

    points = gridView->property("missionPoints").value<QJSValue>();
    QVERIFY2(pointIsAbout(1, 23.25, -1.5), "nothing may move while the plan is in transit");
    QVERIFY(pointIsAbout(2, 23.25, 18.5));
}

/// Without an origin there is no frame to correct a position inside of, so the offer is withdrawn
/// rather than left to produce a coordinate invented from nothing.
void LocalGridViewTest::_positionCorrectionIsOfferedOnlyAgainstAnOrigin_test()
{
    QVERIFY(vehicle());
    QVERIFY2(!vehicle()->estimatorOrigin().isValid(), "the mock vehicle starts without an origin");

    MAKE_GRID_VIEW(gridView);
    QVERIFY(!gridView->property("originKnown").toBool());

    QObject *const marker = gridView->findChild<QObject *>(QStringLiteral("localGrid_originMarker"));
    QVERIFY2(marker, "the origin has to be an item that can be pointed at, not a painting of one");
    QVERIFY2(!marker->property("originKnown").toBool(),
             "a marker that invites a click with no origin behind it invites a coordinate out of nowhere");

    // Nothing to send, and nothing sent: the guard is in the view rather than only in the dialog, so
    // there is no path that opens a dialog holding an invalid coordinate
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "showPositionCorrectionDialog", Qt::DirectConnection,
                                      Q_ARG(QVariant, 0.0), Q_ARG(QVariant, 0.0), Q_ARG(QVariant, QString())));

    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));
    QTRY_VERIFY_WITH_TIMEOUT(gridView->property("originKnown").toBool(), TestTimeout::mediumMs());
    QTRY_VERIFY_WITH_TIMEOUT(marker->property("originKnown").toBool(), TestTimeout::mediumMs());
}

#define MAKE_ORIGIN_DRIFT(name)                                                     \
    QQmlEngine name##Engine;                                                        \
    name##Engine.addImportPath(QStringLiteral("qrc:/qml"));                         \
    QQmlComponent name##Component(&name##Engine);                                   \
    QString name##Error;                                                            \
    const QScopedPointer<QObject> name(                                             \
        createOriginDrift(name##Component, vehicle(), name##Error));                \
    QVERIFY2(name, qPrintable(name##Error))

/// Drift is the reported position moving while nothing is flying the aircraft. Measured that way
/// rather than as distance from the origin, which is as large for a vehicle parked somewhere else as
/// for one whose frame has slid.
void LocalGridViewTest::_driftIsTheReportedPositionMovingWhileParked_test()
{
    QVERIFY(vehicle());
    MAKE_ORIGIN_DRIFT(drift);

    // MockLink streams its own sweeping LOCAL_POSITION_NED at 10 Hz, which is movement this would
    // dutifully report as drift. The positions under test are the injected ones.
    QVERIFY(mockLink());
    mockLink()->setCommLost(true);

    // Positions travel as 32 bit floats, so the distances they work out to are near rather than
    // exact
    const auto driftIsAbout = [&drift](double metres) {
        return qAbs(drift->property("driftMetres").toReal() - metres) < 0.001;
    };

    sendLocalPosition(vehicle(), 0.0F, 0.0F, -1.0F);
    QTRY_VERIFY_WITH_TIMEOUT(drift->property("observing").toBool(), TestTimeout::mediumMs());
    QVERIFY(driftIsAbout(0.0));
    QVERIFY2(!drift->property("drifting").toBool(), "a position that has not moved is not drift");
    QCOMPARE(drift->property("warning").toString(), QString());

    // Under the threshold: the ordinary noise of a flow sensor watching a static scene
    sendLocalPosition(vehicle(), 0.3F, 0.4F, -1.0F);
    QVERIFY(driftIsAbout(0.5));
    QVERIFY2(!drift->property("drifting").toBool(), "sub-metre noise must not raise a warning");

    // Past it: the frame really has slid
    sendLocalPosition(vehicle(), 3.0F, 4.0F, -1.0F);
    QVERIFY(driftIsAbout(5.0));
    QVERIFY(drift->property("drifting").toBool());

    const QString warning = drift->property("warning").toString();
    QVERIFY2(warning.contains(QStringLiteral("5.0")),
             qPrintable(QStringLiteral("the measurement itself must be quoted, got: %1").arg(warning)));
    QVERIFY2(warning.contains(QStringLiteral("has not been moved")),
             qPrintable(QStringLiteral("being carried has to be offered as the other reading, got: %1").arg(warning)));

    // And starting again from where it stands clears it, which is what to do after carrying it
    QVERIFY(QMetaObject::invokeMethod(drift.get(), "reset", Qt::DirectConnection));
    sendLocalPosition(vehicle(), 3.0F, 4.0F, -1.0F);
    QVERIFY(driftIsAbout(0.0));
    QVERIFY(!drift->property("drifting").toBool());

    mockLink()->setCommLost(false);
}

/// A vehicle parked twenty metres from the origin is not drifting, it is parked. A warning built on
/// the range figure would fire on every flight that does not start on the origin, and a warning that
/// fires every flight is one the operator learns to ignore -- which is the failure this is for.
void LocalGridViewTest::_parkedAwayFromTheOriginIsNotDrift_test()
{
    QVERIFY(vehicle());
    MAKE_ORIGIN_DRIFT(drift);

    QVERIFY(mockLink());
    mockLink()->setCommLost(true);

    for (int i = 0; i < 5; i++) {
        sendLocalPosition(vehicle(), 20.0F, -15.0F, -1.0F);
    }

    QTRY_VERIFY_WITH_TIMEOUT(drift->property("observing").toBool(), TestTimeout::mediumMs());
    QVERIFY(qAbs(drift->property("driftMetres").toReal()) < 0.001);
    QVERIFY2(!drift->property("drifting").toBool(),
             "standing 25 m from the origin without moving is not drift");
    QCOMPARE(drift->property("warning").toString(), QString());

    mockLink()->setCommLost(false);
}

/// Armed, the aircraft is moving on purpose and every metre of it would be counted as drift.
void LocalGridViewTest::_armedVehicleIsNotWatchedForDrift_test()
{
    QVERIFY(vehicle());
    MAKE_ORIGIN_DRIFT(drift);

    sendLocalPosition(vehicle(), 0.0F, 0.0F, -1.0F);
    QTRY_VERIFY_WITH_TIMEOUT(drift->property("observing").toBool(), TestTimeout::mediumMs());

    vehicle()->setArmedShowError(true);
    QTRY_VERIFY_WITH_TIMEOUT(vehicle()->armed(), TestTimeout::longMs());

    QVERIFY2(!drift->property("observing").toBool(), "nothing is measured while the aircraft is flying");
    QVERIFY(!drift->property("drifting").toBool());
    QCOMPARE(drift->property("warning").toString(), QString());

    vehicle()->setArmedShowError(false);
    QTRY_VERIFY_WITH_TIMEOUT(!vehicle()->armed(), TestTimeout::longMs());

    // And the next stretch on the ground is measured from where that stretch began, not from before
    // the flight
    QTRY_VERIFY_WITH_TIMEOUT(drift->property("observing").toBool(), TestTimeout::mediumMs());
    QVERIFY2(!drift->property("drifting").toBool(),
             "a landing somewhere else must not be reported as drift accumulated on the ground");
}

UT_REGISTER_TEST(LocalGridViewTest, TestLabel::Integration, TestLabel::Vehicle)
