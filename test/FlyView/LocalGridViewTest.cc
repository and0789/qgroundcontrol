#include "LocalGridViewTest.h"

#include <cmath>

#include <QtCore/QtNumeric>
#include <QtPositioning/QGeoCoordinate>
#include <QtQml/QJSValue>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
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

        readonly property var visualItems: QtObject {
            readonly property int count: items.length
            function get(index) { return items[index] }
        }

        function insertSimpleMissionItem(coordinate, index, makeCurrentItem) {
            lastCoordinate = coordinate
            lastIndex = index
            insertCount++
            return null
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

QObject *createMissionControllerStub(QQmlComponent &component, QString &error)
{
    component.setData(kMissionControllerStub, QUrl());
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

UT_REGISTER_TEST(LocalGridViewTest, TestLabel::Integration, TestLabel::Vehicle)
