#include "LocalGridViewTest.h"

#include <cmath>
#include <iterator>

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

#include "Fact.h"
#include "FactGroup.h"
#include "FactMetaData.h"
#include "FirmwarePlugin.h"
#include "FlyViewSettings.h"
#include "SettingsManager.h"
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
                id: missionItem
                property bool specifiesCoordinate: true
                // False for every plain waypoint and landing: the aircraft is routed through them,
                // so a leg is measured through them and the polyline is drawn through them. True for
                // an ROI, which carries a real coordinate without ever being flown to.
                property bool isStandaloneCoordinate: false
                property var  coordinate
                property int  sequenceNumber: 0
                // The last mission sequence number this one item occupies. A waypoint carrying a
                // speed is uploaded as a NAV_WAYPOINT followed by a DO_CHANGE_SPEED, so one visual
                // item can span two numbers -- which is what puts gaps in the plan's numbering.
                property int  lastSequenceNumber: sequenceNumber
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

                // Where ArduPilot's per-waypoint speed lives. QGC carries it as a section on the
                // item rather than as a visual item of its own, and only a plain waypoint has one --
                // which is the check the grid makes before touching it.
                property QtObject speedSection: QtObject {
                    property bool available: true
                    property bool specifyFlightSpeed: false
                    property Fact flightSpeed: Fact { }
                }

                // Where the command tree publishes an item's editable parameters, in the two lists
                // it sorts them into: the plain ones here, the ones it marks advanced beside them.
                // Only the shape the grid reads off a QmlObjectListModel -- a count and a get().
                //
                // Left empty for tests to fill through publishItemFact rather than filled per
                // command here. Which parameters a command publishes, which list each falls into,
                // and what each is named are the command tree's answers; a stand-in that made them
                // up would be asserting its own guess instead of the search the grid does.
                property QtObject textFieldFacts: QtObject {
                    property var facts: []
                    readonly property int count: facts.length
                    function get(index) { return facts[index] }
                }

                property QtObject textFieldFactsAdvanced: QtObject {
                    property var facts: []
                    readonly property int count: facts.length
                    function get(index) { return facts[index] }
                }

                // What the real item does when its command is written: the command tree decides all
                // over again what the item is, and a CONDITION_YAW carries no position at all. It
                // matters here because the grid inserts one as a plain item and gives it its command
                // afterwards -- an item left claiming a coordinate would be drawn on the grid and
                // measured into the leg beside it.
                onCommandChanged: {
                    if (command === 115) {
                        specifiesCoordinate = false
                        commandName = "Wait for Yaw"
                    }
                }
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
            // Left pointing at the item just added, the same place a freshly built real plan leaves
            // it (MissionController::_initAllVisualItems ends by calling setCurrentPlanViewSeqNum
            // against its own last item). Tests that build a fixture plan through a run of addItem
            // calls before ever touching the grid's own insert functions get the same starting point
            // a real plan would hand them, rather than the -1 this stub starts at before anything
            // has been added.
            currentPlanViewVIIndex = list.length - 1
            currentPlanViewSeqNum = sequenceNumber
        }

        // Splices a ready-made item into the list at a visual item index, or appends it when the
        // index names no position (-1, or past the end) -- the same two cases
        // MissionController::_insertSimpleMissionItemWorker handles. Used only by the insert
        // functions below; addItem above stays a plain append for the many tests that build a plan
        // by hand and choose their own sequence numbers.
        function _insertAt(item, index) {
            var list = items.slice()
            if ((index < 0) || (index > list.length)) {
                list.push(item)
            } else {
                list.splice(index, 0, item)
            }
            items = list
        }

        // Sequence numbers handed to items this stub inserts itself, kept well clear of the ones
        // tests assign by hand through addItem/addHomeItem so a lookup by number can never confuse
        // the two.
        property int _nextAutoSequenceNumber: 1000

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
            // Same reasoning as addItem above: a plan holding only its home item leaves current
            // pointed at that one item, which is what makes the next insert land after it (index 1)
            // rather than in front of it.
            currentPlanViewVIIndex = 0
            currentPlanViewSeqNum = 0
        }

        // The sequence number the vehicle is flying to. MissionController reads this off the
        // vehicle's mission manager and answers -1 outside the fly view.
        property int currentMissionIndex: -1

        /// What MissionController::sendToVehiclePreCheck would answer, by the order of its own enum:
        /// 0 Ok, 1 NoActiveVehicle, 2 FirwmareVehicleMismatch, 3 ActiveMission.
        property int preCheckState: 0
        function sendToVehiclePreCheck() { return preCheckState }

        /// How far through a transfer the mission panel's progress bar is. Present because the panel
        /// binds it into a real: left off, it reads as undefined and QML says so on every rebuild --
        /// which the runner counts as a failure.
        property real progressPct: 0

        /// What MissionController's flight-status calculation produces. Real properties on the real
        /// controller and not gated behind the plan view, so the grid's totals panel reads them in
        /// flight -- which means the stand-in has to carry them or every binding on them reads
        /// undefined and QML says so on each rebuild.
        ///
        /// Set by tests rather than derived from the items here: reproducing QGC's own distance and
        /// speed integration would be testing a copy of it instead of what the grid does with it.
        property real missionTotalDistance: 0
        property real missionTime: 0

        readonly property var visualItems: QtObject {
            readonly property int count: items.length
            function get(index) { return items[index] }
        }

        // Where an insert lands, and the flags every insert button on the grid and on the tool
        // strip's Plan panel reads. Mirrors MissionController::setCurrentPlanViewSeqNum in shape
        // (search by exact sequenceNumber match, -1 when the plan is empty) without its rules for
        // *why* an insert would be refused -- those are covered by the plain bools below instead,
        // which tests set directly rather than deriving from plan shape the way the real one does.
        property int currentPlanViewSeqNum: -1
        property int currentPlanViewVIIndex: -1
        signal planViewStateChanged()
        signal visualItemsReset()

        function setCurrentPlanViewSeqNum(sequenceNumber, force) {
            if (!force && (sequenceNumber === currentPlanViewSeqNum)) {
                return
            }
            currentPlanViewSeqNum = sequenceNumber
            var viIndex = -1
            for (var i = 0; i < items.length; i++) {
                if (items[i].sequenceNumber === sequenceNumber) {
                    viIndex = i
                    break
                }
            }
            currentPlanViewVIIndex = viIndex
            planViewStateChanged()
        }

        // Returns the item it made, the way MissionController does, so a caller that adjusts the
        // new item afterwards is exercised rather than silently doing nothing
        property var lastInsertedItem: null

        function insertSimpleMissionItem(coordinate, index, makeCurrentItem) {
            lastCoordinate = coordinate
            lastIndex = index
            insertCount++
            const item = itemComponent.createObject(null, {
                coordinate: coordinate,
                sequenceNumber: _nextAutoSequenceNumber
            })
            _nextAutoSequenceNumber++
            _insertAt(item, index)
            lastInsertedItem = item
            if (makeCurrentItem) {
                setCurrentPlanViewSeqNum(item.sequenceNumber, true)
            }
            return item
        }

        // MissionController gates its own insert strip on these, and so does the grid
        property bool isInsertTakeoffValid: true
        property bool isInsertLandValid: true
        property bool isInsertROIValid: true
        property bool isROIActive: false
        property bool flyThroughCommandsAllowed: true

        property int takeoffCount: 0
        property int landCount: 0

        /// Whether a takeoff item carries a coordinate of its own, which is a firmware difference
        /// rather than a preference. PX4 gives its takeoff a position; ArduPilot's is altitude-only
        /// and QGC's command tree says so, which leaves the grid a plan item it cannot draw.
        /// Defaults to the PX4 shape so tests naming the other one have to say it.
        property bool takeoffSpecifiesCoordinate: true

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
            const item = itemComponent.createObject(null, {
                coordinate: takeoffLandsOn ? takeoffLandsOn : coordinate,
                sequenceNumber: _nextAutoSequenceNumber,
                // MAV_CMD_NAV_TAKEOFF. Carried like the landing's own command below, because a
                // takeoff's param1 is not a waypoint's: anything that decides what to offer per item
                // reads the command, and an item left claiming to be a waypoint would be offered a
                // waypoint's fields.
                command: 22,
                isTakeoffItem: true,
                commandName: "Takeoff",
                specifiesCoordinate: takeoffSpecifiesCoordinate
            })
            _nextAutoSequenceNumber++
            _insertAt(item, index)
            lastInsertedItem = item
            if (makeCurrentItem) {
                setCurrentPlanViewSeqNum(item.sequenceNumber, true)
            }
            return item
        }

        function insertLandItem(coordinate, index, makeCurrentItem) {
            lastCoordinate = coordinate
            lastIndex = index
            landCount++
            const item = itemComponent.createObject(null, {
                coordinate: coordinate,
                sequenceNumber: _nextAutoSequenceNumber,
                command: 21,
                commandName: "Land"
            })
            _nextAutoSequenceNumber++
            _insertAt(item, index)
            lastInsertedItem = item
            if (makeCurrentItem) {
                setCurrentPlanViewSeqNum(item.sequenceNumber, true)
            }
            return item
        }

        property int roiCount: 0
        property int cancelRoiCount: 0

        function insertROIMissionItem(coordinate, index, makeCurrentItem) {
            lastCoordinate = coordinate
            lastIndex = index
            roiCount++
            const item = itemComponent.createObject(null, {
                coordinate: coordinate,
                sequenceNumber: _nextAutoSequenceNumber,
                isStandaloneCoordinate: true,
                commandName: "ROI"
            })
            _nextAutoSequenceNumber++
            _insertAt(item, index)
            lastInsertedItem = item
            if (makeCurrentItem) {
                setCurrentPlanViewSeqNum(item.sequenceNumber, true)
            }
            return item
        }

        function insertCancelROIMissionItem(index, makeCurrentItem) {
            lastIndex = index
            cancelRoiCount++
            const item = itemComponent.createObject(null, {
                coordinate: QtPositioning.coordinate(),
                sequenceNumber: _nextAutoSequenceNumber,
                specifiesCoordinate: false,
                isStandaloneCoordinate: true,
                commandName: "Cancel ROI"
            })
            _nextAutoSequenceNumber++
            _insertAt(item, index)
            lastInsertedItem = item
            if (makeCurrentItem) {
                setCurrentPlanViewSeqNum(item.sequenceNumber, true)
            }
            return item
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

        /// What the mission panel reaches through this one for its upload pre-check. Null by
        /// default, which is the shape every test that never presses Upload sees.
        property var missionController: null

        /// VisualMissionItem.ReadyForSave is 0. Anything else means an item is still waiting on
        /// data and has no coordinate to send.
        property int saveState: 0
        function readyForSaveState() { return saveState }

        property int sendCount: 0
        function sendToVehicle() { sendCount++ }

        property int showPlanCount: 0
        function showPlanFromManagerVehicle() { showPlanCount++ }
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

/// Gives a QML-declared Fact the type the real one has.
///
/// A Fact built in QML carries no metadata, and FactMetaData defaults to int32 -- so a stub fact
/// takes 1.5 m/s and hands back 2. The real SpeedSection fact is a double, and a stub that rounds
/// would let a speed the operator can actually type pass here and fail in the air.
void makeFactDouble(QObject *factObject)
{
    Fact *const fact = qobject_cast<Fact *>(factObject);
    if (!fact) {
        return;
    }
    fact->setMetaData(new FactMetaData(FactMetaData::valueTypeDouble, fact));
}

/// The speed section of a stub item, with its fact typed the way the real one is
QObject *stubSpeedSection(QObject *stub, int index)
{
    const QVariantList items = stub->property("items").toList();
    if ((index < 0) || (index >= items.count())) {
        return nullptr;
    }
    QObject *const item = items.at(index).value<QObject *>();
    if (!item) {
        return nullptr;
    }
    QObject *const section = item->property("speedSection").value<QObject *>();
    if (section) {
        makeFactDouble(section->property("flightSpeed").value<QObject *>());
    }
    return section;
}

/// Publishes a parameter on a stub item under the name the command tree gives it, and hands the
/// fact back so a test can set what the operator would type into it.
///
/// Named from here because a Fact publishes its name read-only: the real one takes it from the
/// metadata the command tree built it with, which is exactly the path a stub item does not have. The
/// name is the whole point of the exercise -- the grid finds this parameter by asking for it by
/// name, because the position it sits at in the list depends on which other parameters this firmware
/// and vehicle publish.
///     @param listName "textFieldFacts" or "textFieldFactsAdvanced", which is the tree's own choice
///            per parameter: NAV_WAYPOINT's Hold is marked advanced, CONDITION_YAW's Heading is not
Fact *publishItemFact(QObject *item, const char *listName, const QString &factName)
{
    if (!item) {
        return nullptr;
    }

    QObject *const list = item->property(listName).value<QObject *>();
    if (!list) {
        return nullptr;
    }

    // Parented to the item, so the fact lives exactly as long as the item publishing it and no
    // engine ever takes ownership of it
    auto *const fact = new Fact(0, factName, FactMetaData::valueTypeDouble, item);
    QVariantList facts = list->property("facts").toList();
    facts.append(QVariant::fromValue(static_cast<QObject *>(fact)));
    list->setProperty("facts", facts);
    return fact;
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
    // 1: right after the takeoff this same call just placed at index 0. Bagian 3 made every insert
    // land after whichever item the controller calls current, rather than always past the end.
    QCOMPARE(stub->property("lastIndex").toInt(), 1);

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

/// A landing's altitude is never flown. ArduPilot's do_land() zeroes the one it is given and refills
/// it from the vehicle's current altitude, so the aircraft arrives over the landing point at
/// whatever height the leg before it was flown at. Left carrying a number of its own the item is a
/// field that changes nothing -- and the plan's profile is drawn from that number, so the last leg
/// showed a climb or a dive the aircraft would not fly.
void LocalGridViewTest::_landingTakesTheAltitudeOfTheItemBeforeIt_test()
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
            Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0 * (i + 1), 0.0))),
            Q_ARG(QVariant, i + 1)));
    }
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    const auto altitudeFact = [&gridView](int index) -> QObject * {
        QVariant fact;
        if (!QMetaObject::invokeMethod(gridView.get(), "waypointAltitudeFact", Qt::DirectConnection,
                                       Q_RETURN_ARG(QVariant, fact), Q_ARG(QVariant, index))) {
            return nullptr;
        }
        return fact.value<QObject *>();
    };

    QObject *const firstAltitude = altitudeFact(0);
    QObject *const secondAltitude = altitudeFact(1);
    QVERIFY(firstAltitude);
    QVERIFY(secondAltitude);
    firstAltitude->setProperty("rawValue", 7.0);
    secondAltitude->setProperty("rawValue", 3.0);

    const auto isLanding = [&gridView](int index) {
        QVariant landing;
        return QMetaObject::invokeMethod(gridView.get(), "waypointIsLanding", Qt::DirectConnection,
                                         Q_RETURN_ARG(QVariant, landing), Q_ARG(QVariant, index))
                && landing.toBool();
    };
    QVERIFY(!isLanding(1));

    // Turning the last waypoint of a pattern into a landing is the common edit, and it is where the
    // altitude stops being the operator's to set
    QVariant changed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "setWaypointCommand", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, changed), Q_ARG(QVariant, 1),
                                      Q_ARG(QVariant, static_cast<int>(MAV_CMD_NAV_LAND))));
    QVERIFY(changed.toBool());
    QVERIFY(isLanding(1));
    QVERIFY2(qFuzzyCompare(secondAltitude->property("rawValue").toDouble(), 7.0),
             "a landing must be flown at the height of the leg that reaches it");

    // And it keeps following that leg when the waypoint before it is retyped
    firstAltitude->setProperty("rawValue", 4.0);
    QVariant moved;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "syncLandingAltitudes", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved)));
    QCOMPARE(moved.toInt(), 1);
    QCOMPARE(secondAltitude->property("rawValue").toDouble(), 4.0);

    // Already in step, so nothing to do. Reported rather than assumed: the panel calls this on every
    // altitude edit, and a walk that always claims a change would fight the field being typed into.
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "syncLandingAltitudes", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved)));
    QCOMPARE(moved.toInt(), 0);

    // A landing with nothing before it has no leg to take a height from, and is left alone rather
    // than zeroed: the aircraft is on the ground there either way, and rewriting it would be a
    // change the operator did not ask for and cannot see the reason for.
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "setWaypointCommand", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, changed), Q_ARG(QVariant, 0),
                                      Q_ARG(QVariant, static_cast<int>(MAV_CMD_NAV_LAND))));
    QVERIFY(changed.toBool());
    QCOMPARE(firstAltitude->property("rawValue").toDouble(), 4.0);
}

/// A grey "Add takeoff at origin" is not an explanation. The operator who cannot tell a refusal from
/// a stuck click is the one who closes QGC to get the option back -- which is exactly what a plan
/// still mirroring the flight before it produced. The grid answers which of the two states it is in
/// so the panel can say so.
void LocalGridViewTest::_gridSaysWhetherThePlanAlreadyHasATakeoff_test()
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

    QVERIFY(gridView->property("planIsEmpty").toBool());
    QVERIFY(!gridView->property("planHasTakeoff").toBool());

    // A waypoint on an empty plan gains a takeoff underneath it, so both answers move together
    QVariant added;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 25.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());
    QVERIFY(!gridView->property("planIsEmpty").toBool());
    QVERIFY2(gridView->property("planHasTakeoff").toBool(),
             "a plan that gained a takeoff must say so, or the refusal cannot be explained");

    // A plan holding only waypoints is the other refusal: a takeoff can only go first, and this one
    // has items already. Told apart because the way out of the two is not the same.
    MAKE_GRID_VIEW(plainGridView);
    QQmlComponent plainStubComponent(&gridViewEngine);
    QString plainStubError;
    const QScopedPointer<QObject> plainStub(createMissionControllerStub(plainStubComponent, plainStubError));
    QVERIFY2(plainStub, qPrintable(plainStubError));
    QVERIFY(QMetaObject::invokeMethod(
        plainStub.get(), "addItem", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0, 0.0))), Q_ARG(QVariant, 1)));
    plainGridView->setProperty("missionController", QVariant::fromValue(plainStub.get()));

    QVERIFY(!plainGridView->property("planIsEmpty").toBool());
    QVERIFY(!plainGridView->property("planHasTakeoff").toBool());
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
                sequences.append(row->property("itemNumber").toInt());
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

/// The heading left the grid as a rose of its own and came back as a number, because the fly view's
/// instrument panel already draws a compass and two pictures of one heading cost the top of the
/// screen without adding a fact.
///
/// What matters in the move is that an unknown heading stays unknown. The instrument panel's compass
/// reads its heading as zero when the vehicle has not sent one, which draws a needle pointing
/// confidently at north -- and on a view whose entire subject is an estimate that can be quietly
/// wrong, a confident wrong answer is the one failure worth engineering against.
void LocalGridViewTest::_headingIsExposedAsANumberAndUnknownStaysUnknown_test()
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    QString error;
    const QScopedPointer<QObject> headless(createGridView(component, nullptr, error));
    QVERIFY2(headless, qPrintable(error));

    QVERIFY2(qIsNaN(headless->property("vehicleHeadingDegrees").toDouble()),
             "with no vehicle there is no heading, and zero is a heading");

    QVERIFY(vehicle());
    MAKE_GRID_VIEW(gridView);

    // Whatever the vehicle is reporting, the grid has to be reporting the same thing. Compared
    // against the fact rather than against a number written here: the point is the wiring, and a
    // literal would only be testing what MockLink happens to sweep through.
    Fact *const headingFact = vehicle()->heading();
    QVERIFY(headingFact);
    QTRY_VERIFY_WITH_TIMEOUT(!qIsNaN(gridView->property("vehicleHeadingDegrees").toDouble()),
                             TestTimeout::mediumMs());
    QVERIFY2(qAbs(gridView->property("vehicleHeadingDegrees").toDouble()
                  - headingFact->rawValue().toDouble()) < 0.001,
             "the grid must report the vehicle's own heading, not one of its own");
}

/// The readout folds itself away while there is nothing to read, and opens when telemetry starts.
///
/// Folded is the right default: before the first position the panel is at its largest and says the
/// least -- six dashes and a line explaining that it has nothing -- standing over the one picture the
/// operator has. Once positions arrive, that panel is the instrument of the whole flight.
///
/// What must never fold is a warning. A panel that hides "the position you are looking at stopped
/// being current" because the operator tidied it away is worse than a panel that was never tidy.
void LocalGridViewTest::_readoutFoldsUntilThereIsTelemetryAndNeverFoldsAWarning_test()
{
    QVERIFY(vehicle());
    QVERIFY(mockLink());

    // The link is cut further down to drive the estimate stale, and anything still in flight when it
    // goes will run out of retries and say so. That is the consequence of cutting it, not a fault.
    ignoreLogMessage("Vehicle.MavCommandQueue", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Giving up sending command")));

    // Started without a vehicle, which is the only way to see the state the operator meets on a cold
    // fly view. telemetryAvailable is a one-way latch -- FactGroup never puts it back -- so a vehicle
    // that has ever reported a position keeps reporting one no matter what the link does.
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    QString error;
    const QScopedPointer<QObject> gridView(createGridView(component, nullptr, error));
    QVERIFY2(gridView, qPrintable(error));

    QQuickWindow window;
    QVERIFY(_showInWindow(window, gridView.get()));

    auto *const gridItem = qobject_cast<QQuickItem *>(gridView.get());
    QVERIFY(gridItem);
    QObject *const readout = gridItem->findChild<QObject *>(QStringLiteral("localGrid_readout"));
    QVERIFY2(readout, "the readout has to be findable, or nothing below is testing it");

    QVERIFY2(readout->property("collapsed").toBool(),
             "a panel with nothing to say must not stand over the grid saying it");

    // The summary is what makes folding worth doing -- folded and silent, the operator opens it again
    // every time -- so it has to be there even before there is a number to put in it
    QObject *const summary = gridItem->findChild<QObject *>(QStringLiteral("localGrid_readoutSummary"));
    QVERIFY(summary);
    QVERIFY(summary->property("visible").toBool());

    // Short enough to keep the test quick, long enough that it is silence being measured -- the same
    // bargain the stale-position test strikes
    gridView->setProperty("stalePositionTimeoutMs", 250);

    // MockLink streams LOCAL_POSITION_NED at 10 Hz, and every one of those restarts the countdown to
    // a stale estimate. Silenced here so the only position on this grid is the one put there below.
    mockLink()->setCommLost(true);

    // A vehicle arrives and reports: the instrument opens itself rather than waiting to be asked. The
    // position is injected into the fact group rather than flown in, which is the only way to have
    // one with the link cut -- and it lands after the grid has the vehicle, because attaching one
    // stops the countdown on the grounds that a new aircraft has not gone silent, it has simply not
    // spoken yet.
    gridView->setProperty("vehicle", QVariant::fromValue(vehicle()));
    sendLocalPosition(vehicle(), 12.0F, -5.0F, -2.0F);

    QTRY_VERIFY_WITH_TIMEOUT(gridView->property("positionValid").toBool(), TestTimeout::mediumMs());
    QTRY_VERIFY_WITH_TIMEOUT(!readout->property("collapsed").toBool(), TestTimeout::mediumMs());
    QVERIFY2(!summary->property("visible").toBool(),
             "open, the same pair is spelled out below and repeating it reads as a second measurement");

    // Open, the panel has to be big enough for what is in it. A Layout does not shrink its children
    // to fit -- it lets them overflow -- so a width chosen without checking the contents put the
    // second column of numbers and the whole button row outside the panel, running off across the
    // grid. Nothing else here would have caught it: every value was correct and every binding fired.
    auto *const readoutItem = qobject_cast<QQuickItem *>(readout);
    QVERIFY(readoutItem);
    for (const QString &name : { QStringLiteral("localGrid_readoutNumbers"),
                                 QStringLiteral("localGrid_readoutViewButtons") }) {
        auto *const content = gridItem->findChild<QQuickItem *>(name);
        QVERIFY2(content, qPrintable(QStringLiteral("%1 is missing").arg(name)));

        // Waited on rather than read once. These blocks are hidden while the panel is folded, a
        // Layout leaves anything hidden out of its implicit size, and Qt Quick lays out on a later
        // pass -- so the frame in which the panel opens is one where the panel has not been told yet
        // how wide its contents became.
        (void) QTest::qWaitFor([content, readoutItem]() {
            return content->width() <= readoutItem->width();
        }, TestTimeout::shortMs());

        QVERIFY2(content->width() <= readoutItem->width(),
                 qPrintable(QStringLiteral("%1 is %2 wide inside a panel of %3 — it overflows")
                                .arg(name).arg(content->width()).arg(readoutItem->width())));
    }

    // Folded again by hand while the position is still live, which is the operator's call to make
    readout->setProperty("collapsed", true);
    QVERIFY(readout->property("collapsed").toBool());

    // And the warning shows through the fold. This is the whole safety case for it: what is hidden is
    // the numbers, never the reason they cannot be trusted.
    QTRY_VERIFY_WITH_TIMEOUT(gridView->property("positionStale").toBool(), TestTimeout::mediumMs());
    QObject *const staleWarning = gridItem->findChild<QObject *>(QStringLiteral("localGrid_staleWarning"));
    QVERIFY(staleWarning);
    QVERIFY2(staleWarning->property("visible").toBool(),
             "a folded panel must still say that the position it is holding stopped being current");
    QVERIFY2(readout->property("collapsed").toBool(),
             "a warning must not quietly unfold the panel either -- that is the operator's choice");

    mockLink()->setCommLost(false);
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

/// A waypoint placed on the grid says how fast it will be flown, rather than inheriting whatever
/// WP_SPD happens to be.
///
/// It matters more here than on a GPS aircraft: above EK3_RNG_USE_SPD the estimator stops taking
/// its height from the rangefinder, and optical flow is scaled by height -- so the speed a leg is
/// flown at changes how far the aircraft believes it has travelled. A plan that does not carry its
/// speed is a plan whose accuracy depends on a parameter nobody looked at.
void LocalGridViewTest::_aPlacedWaypointCarriesTheGridsOwnSpeed_test()
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

    QVariant placed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, placed),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, -10.0)));
    QVERIFY(placed.toBool());

    QVariant sectionValue;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "waypointSpeedSection", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, sectionValue),
                                      Q_ARG(QVariant, stub->property("items").toList().count() - 1)));
    QObject *const section = sectionValue.value<QObject *>();
    QVERIFY2(section, "a placed waypoint reported no speed section");

    const QVariant expected = gridView->property("defaultWaypointSpeedMetersPerSecond");
    QObject *const speed = section->property("flightSpeed").value<QObject *>();
    QVERIFY(speed);
    QCOMPARE(speed->property("rawValue").toDouble(), expected.toDouble());

    // The value alone is not enough. Left unspecified it uploads as nothing and the leg is flown at
    // the vehicle's own speed, which is a plan that disagrees with the panel that drew it.
    QVERIFY2(section->property("specifyFlightSpeed").toBool(),
             "the speed was filled in but the waypoint would not carry it");
}

/// One pattern flown at two speeds is the comparison this grid exists to make, and retyping every
/// waypoint between runs is how a run ends up half at one speed and half at the other.
void LocalGridViewTest::_oneSpeedCanBeSetOnEveryWaypoint_test()
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

    // 1.5 m/s is the speed the comparison flights are actually flown at, and a stub fact left at
    // its default int32 type would quietly hand back 2
    for (int i = 0; i < 3; i++) {
        QVERIFY(stubSpeedSection(stub.get(), i));
    }

    QVariant changed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "setAllWaypointSpeeds", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, changed), Q_ARG(QVariant, 1.5)));
    QCOMPARE(changed.toInt(), 3);

    for (int i = 0; i < 3; i++) {
        QVariant sectionValue;
        QVERIFY(QMetaObject::invokeMethod(gridView.get(), "waypointSpeedSection", Qt::DirectConnection,
                                          Q_RETURN_ARG(QVariant, sectionValue), Q_ARG(QVariant, i)));
        QObject *const section = sectionValue.value<QObject *>();
        QVERIFY(section);
        QObject *const speed = section->property("flightSpeed").value<QObject *>();
        QVERIFY(speed);
        QCOMPARE(speed->property("rawValue").toDouble(), 1.5);
        // Repairs a plan arrived from a file as well as setting one built here
        QVERIFY(section->property("specifyFlightSpeed").toBool());
    }

    // A speed that is not a number, or one that cannot be flown, must leave the plan alone rather
    // than writing a standstill into every leg
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "setAllWaypointSpeeds", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, changed),
                                      Q_ARG(QVariant, std::numeric_limits<double>::quiet_NaN())));
    QCOMPARE(changed.toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "setAllWaypointSpeeds", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, changed), Q_ARG(QVariant, 0.0)));
    QCOMPARE(changed.toInt(), 0);
}

/// A plan reads as the sequence it will be flown: one, two, three, with nothing missing between
/// them.
///
/// The numbers on screen used to be the mission sequence numbers, which count things nobody placed.
/// The home position takes zero, ArduPilot's takeoff takes one and is drawn nowhere because it
/// carries no coordinate, and a waypoint's speed is uploaded as a DO_CHANGE_SPEED that takes another
/// -- so a plan of takeoff, waypoint and landing listed exactly two rows, numbered 2 and 4, with no
/// way for the operator to tell whether items 1 and 3 were missing or merely unlabelled.
void LocalGridViewTest::_planIsNumberedInTheOrderItIsFlown_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));

    // The plan the screenshot came from, in the shape ArduPilot gives it: home at 0, a takeoff at 1
    // with no coordinate of its own, a waypoint at 2 whose speed occupies 3, and a landing at 4.
    stub->setProperty("takeoffSpecifiesCoordinate", false);
    QVERIFY(QMetaObject::invokeMethod(stub.get(), "addHomeItem", Qt::DirectConnection,
                                      Q_ARG(QVariant, QVariant::fromValue(origin))));
    QVERIFY(QMetaObject::invokeMethod(stub.get(), "insertTakeoffItem", Qt::DirectConnection,
                                      Q_ARG(QVariant, QVariant::fromValue(origin)),
                                      Q_ARG(QVariant, -1), Q_ARG(QVariant, true)));
    QVERIFY(QMetaObject::invokeMethod(
        stub.get(), "addItem", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0, 0.0))),
        Q_ARG(QVariant, 2)));
    QVERIFY(QMetaObject::invokeMethod(
        stub.get(), "addItem", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0, 90.0))),
        Q_ARG(QVariant, 4)));

    // The waypoint's DO_CHANGE_SPEED, which is why the landing after it is numbered 4 on the wire
    const QVariantList items = stub->property("items").toList();
    QCOMPARE(items.count(), 4);
    QObject *const waypoint = items.at(2).value<QObject *>();
    QVERIFY(waypoint);
    waypoint->setProperty("lastSequenceNumber", 3);

    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 3);

    const auto numberAt = [&points](int index) {
        return points.property(index).property(QStringLiteral("number")).toInt();
    };
    QCOMPARE(numberAt(0), 1);
    QCOMPARE(numberAt(1), 2);
    QCOMPARE(numberAt(2), 3);

    // The takeoff is listed even though there is nowhere to draw it. It is the item whose altitude
    // is flown first, and a plan that hides it is a plan the operator cannot check.
    QVERIFY2(!points.property(0).property(QStringLiteral("onGrid")).toBool(),
             "ArduPilot's takeoff carries no coordinate, so it cannot be drawn");
    QVERIFY(points.property(0).property(QStringLiteral("isPinned")).toBool());
    QVERIFY(points.property(1).property(QStringLiteral("onGrid")).toBool());
    QVERIFY(points.property(2).property(QStringLiteral("onGrid")).toBool());

    // The rows say the same numbers, since the whole complaint was what is on screen
    QQuickWindow window;
    QVERIFY(_showInWindow(window, gridView.get()));
    auto *const gridItem = qobject_cast<QQuickItem *>(gridView.get());
    QVERIFY(gridItem);

    const auto rowNumbers = [gridItem]() {
        QList<int> numbers;
        const QList<QQuickItem *> rows = collectItemsNamed(gridItem, QStringLiteral("localGrid_missionItemRow"));
        for (QQuickItem *const row : rows) {
            numbers.append(row->property("itemNumber").toInt());
        }
        std::sort(numbers.begin(), numbers.end());
        return numbers;
    };
    QTRY_COMPARE_WITH_TIMEOUT(rowNumbers(), QList<int>({ 1, 2, 3 }), TestTimeout::mediumMs());

    // And the marker for an item with no position is not drawn standing at the origin, which is
    // where a NaN offset would otherwise put it
    QVariant marker;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "waypointMarkerAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, marker), Q_ARG(QVariant, 0)));
    QObject *const takeoffMarker = marker.value<QObject *>();
    QVERIFY(takeoffMarker);
    QVERIFY2(!takeoffMarker->property("visible").toBool(),
             "an item with no coordinate must not be drawn anywhere on the grid");
}

/// The vehicle's mission index can land on the second half of an item. A waypoint carrying a speed
/// is flown as a NAV_WAYPOINT followed by a DO_CHANGE_SPEED, and matching only the first of those
/// left the plan with nothing marked while the aircraft was working through the rest of it.
void LocalGridViewTest::_rowIsMarkedWhereverTheVehicleIsInsideIt_test()
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
    QVERIFY(QMetaObject::invokeMethod(
        stub.get(), "addItem", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0, 90.0))), Q_ARG(QVariant, 3)));

    // The first waypoint's speed occupies sequence 2, which is why the second one starts at 3
    const QVariantList items = stub->property("items").toList();
    QCOMPARE(items.count(), 2);
    QObject *const firstWaypoint = items.at(0).value<QObject *>();
    QVERIFY(firstWaypoint);
    firstWaypoint->setProperty("lastSequenceNumber", 2);

    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    const auto markedNumbers = [&gridView]() {
        QList<int> numbers;
        const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
        const int count = points.property(QStringLiteral("length")).toInt();
        for (int i = 0; i < count; i++) {
            if (points.property(i).property(QStringLiteral("isVehicleTarget")).toBool()) {
                numbers.append(points.property(i).property(QStringLiteral("number")).toInt());
            }
        }
        return numbers;
    };

    QVERIFY2(markedNumbers().isEmpty(), "a plan nobody is flying marks nothing");

    stub->setProperty("currentMissionIndex", 1);
    QCOMPARE(markedNumbers(), QList<int>({ 1 }));

    // Still the first waypoint: sequence 2 is its speed, not the item after it
    stub->setProperty("currentMissionIndex", 2);
    QCOMPARE(markedNumbers(), QList<int>({ 1 }));

    stub->setProperty("currentMissionIndex", 3);
    QCOMPARE(markedNumbers(), QList<int>({ 2 }));
}

/// The takeoff is the item whose altitude the aircraft climbs to first, and on ArduPilot it is the
/// one the grid cannot draw. Leaving it out of the sweeps meant "set this altitude on all" missed
/// it, and the ceiling warning -- which exists because the item that runs a flight away is usually
/// the takeoff -- could not see the very item it was written for.
void LocalGridViewTest::_takeoffWithNoCoordinateIsStillPartOfThePlan_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    stub->setProperty("takeoffSpecifiesCoordinate", false);
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    // Placing the first waypoint gives the plan its takeoff, the way it does on the flight line
    QVariant added;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

    const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 2);
    QVERIFY(!points.property(0).property(QStringLiteral("onGrid")).toBool());

    // Every item, including the one with nowhere to be drawn
    QVariant changed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "setAllWaypointAltitudes", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, changed), Q_ARG(QVariant, 3.0)));
    QCOMPARE(changed.toInt(), 2);

    for (int index = 0; index < 2; index++) {
        QVariant factValue;
        QVERIFY(QMetaObject::invokeMethod(gridView.get(), "waypointAltitudeFact", Qt::DirectConnection,
                                          Q_RETURN_ARG(QVariant, factValue), Q_ARG(QVariant, index)));
        auto *const fact = factValue.value<Fact *>();
        QVERIFY(fact);
        QCOMPARE(fact->rawValue().toDouble(), 3.0);
    }

    // The ceiling scan walks this same list, so it now reaches the takeoff too. What the ceiling
    // itself is worth on a given aircraft is LocalGridAltitudeLimitTest's business; this mock takes
    // its height from the barometer, so there is no ceiling here to breach.
    QVERIFY(!gridView->property("altitudeLimitKnown").toBool());
}

/// A second flight of the same pattern is the whole point of drawing one, and until now it needed
/// the aircraft rebooted. The plan is held as coordinates worked out from the origin the aircraft
/// was standing on; after a flight it is standing somewhere else, so flying the plan again sends it
/// back over the ground it has already covered from a start point part way along the route.
/// ArduPilot will not take a second origin, so moving the frame meant a reboot. Moving the plan
/// needs nothing from the firmware.
void LocalGridViewTest::_planCanBeMovedToStartFromTheAircraft_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    stub->setProperty("takeoffSpecifiesCoordinate", false);
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QQmlComponent planComponent(&gridViewEngine);
    QString planError;
    const QScopedPointer<QObject> plan(createPlanMasterControllerStub(planComponent, planError));
    QVERIFY2(plan, qPrintable(planError));
    gridView->setProperty("planMasterController", QVariant::fromValue(plan.get()));
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "resetPlanAnchor", Qt::DirectConnection));

    const auto place = [&gridView](double north, double east) {
        QVariant added;
        const bool called = QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                                      Q_RETURN_ARG(QVariant, added),
                                                      Q_ARG(QVariant, north), Q_ARG(QVariant, east));
        return called && added.toBool();
    };
    // An empty plan has no pattern to move, so the offer is withdrawn rather than left to move
    // nothing and report that it did
    QCOMPARE(gridView->property("canReanchorPlan").toBool(), false);

    QVERIFY(place(20.0, 0.0));
    QVERIFY(place(20.0, 20.0));

    // Where the aircraft came to rest at the end of the pattern it just flew
    sendLocalPosition(vehicle(), 20.0F, 20.0F, 0.0F);
    QTRY_VERIFY_WITH_TIMEOUT(gridView->property("canReanchorPlan").toBool(), TestTimeout::mediumMs());
    QCOMPARE(gridView->property("reanchorNorthMetres").toDouble(), 20.0);
    QCOMPARE(gridView->property("reanchorEastMetres").toDouble(), 20.0);

    QVariant moved;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "reanchorPlanToVehicle", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved)));
    QCOMPARE(moved.toInt(), 2);

    const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 3);

    const auto pointIsAbout = [&points](int index, double north, double east) {
        const QJSValue point = points.property(index);
        return (qAbs(point.property(QStringLiteral("north")).toNumber() - north) < 0.05)
               && (qAbs(point.property(QStringLiteral("east")).toNumber() - east) < 0.05);
    };

    // The pattern keeps its shape and now runs from where the aircraft is standing
    QVERIFY2(pointIsAbout(1, 40.0, 20.0), "the first leg must start from the aircraft, not the origin");
    QVERIFY2(pointIsAbout(2, 40.0, 40.0), "and the rest of the pattern must move with it");

    // The takeoff has no coordinate to move and is pinned besides, so it is not counted as moved
    QVERIFY2(!points.property(0).property(QStringLiteral("onGrid")).toBool(),
             "an item with no position must not be handed a NaN one by the move");

    // Not while it is flying. Moving the plan under an aircraft already following it changes where
    // it is going mid-flight, which is not what anyone reaching for this between flights means.
    // Walked away from the pattern first, so the refusal being tested is the arming one rather than
    // the plan already sitting where the button would put it.
    sendLocalPosition(vehicle(), 40.0F, 40.0F, 0.0F);
    QTRY_VERIFY_WITH_TIMEOUT(gridView->property("canReanchorPlan").toBool(), TestTimeout::mediumMs());

    vehicle()->setArmedShowError(true);
    QTRY_VERIFY_WITH_TIMEOUT(vehicle()->armed(), TestTimeout::longMs());
    QTRY_COMPARE_WITH_TIMEOUT(gridView->property("canReanchorPlan").toBool(), false, TestTimeout::mediumMs());
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "reanchorPlanToVehicle", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved)));
    QCOMPARE(moved.toInt(), 0);
}

/// Pressing it a second time must not move the pattern a second time.
///
/// The offer used to be the aircraft's whole distance from the origin, worked out fresh on every
/// press. That is right once and wrong afterwards: the plan moved by the first press already starts
/// where the aircraft is, and a second press moved it by the full distance again. The failure is
/// worse than it sounds, because the first press does exactly what the operator wanted -- so the
/// pattern that ends up twice as far out is the one they have already learned to trust.
void LocalGridViewTest::_movingThePlanToTheAircraftTwiceMovesItOnce_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    stub->setProperty("takeoffSpecifiesCoordinate", false);
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QQmlComponent planComponent(&gridViewEngine);
    QString planError;
    const QScopedPointer<QObject> plan(createPlanMasterControllerStub(planComponent, planError));
    QVERIFY2(plan, qPrintable(planError));
    gridView->setProperty("planMasterController", QVariant::fromValue(plan.get()));

    // The anchor lives in settings now, and settings facts keep their values across test functions
    // within a process. Started from a known one rather than from whatever ran before this.
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "resetPlanAnchor", Qt::DirectConnection));

    const auto place = [&gridView](double north, double east) {
        QVariant added;
        const bool called = QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                                      Q_RETURN_ARG(QVariant, added),
                                                      Q_ARG(QVariant, north), Q_ARG(QVariant, east));
        return called && added.toBool();
    };
    QVERIFY(place(20.0, 0.0));
    QVERIFY(place(20.0, 20.0));

    const auto pointIsAbout = [&gridView](int index, double north, double east) {
        const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
        const QJSValue point = points.property(index);
        return (qAbs(point.property(QStringLiteral("north")).toNumber() - north) < 0.05)
               && (qAbs(point.property(QStringLiteral("east")).toNumber() - east) < 0.05);
    };
    const auto reanchor = [&gridView]() {
        QVariant moved;
        const bool called = QMetaObject::invokeMethod(gridView.get(), "reanchorPlanToVehicle",
                                                      Qt::DirectConnection, Q_RETURN_ARG(QVariant, moved));
        return called ? moved.toInt() : -1;
    };

    // Where the aircraft came to rest at the end of the pattern it just flew
    sendLocalPosition(vehicle(), 20.0F, 20.0F, 0.0F);
    QTRY_VERIFY_WITH_TIMEOUT(gridView->property("canReanchorPlan").toBool(), TestTimeout::mediumMs());
    QCOMPARE(reanchor(), 2);
    QVERIFY(pointIsAbout(1, 40.0, 20.0));
    QVERIFY(pointIsAbout(2, 40.0, 40.0));

    // The pattern now starts where the aircraft is standing, so there is nothing left to offer and
    // the button goes quiet rather than staying live with a move of zero behind it
    QVERIFY2(gridView->property("planStartsAtVehicle").toBool(),
             "the plan must be reported as already starting at the aircraft");
    QCOMPARE(gridView->property("canReanchorPlan").toBool(), false);
    QCOMPARE(gridView->property("reanchorNorthMetres").toDouble(), 0.0);
    QCOMPARE(gridView->property("reanchorEastMetres").toDouble(), 0.0);

    // Pressed again anyway -- from a stale binding, a double tap, or an operator making sure
    QCOMPARE(reanchor(), 0);
    QVERIFY2(pointIsAbout(1, 40.0, 20.0), "a second press must not move the pattern a second time");
    QVERIFY(pointIsAbout(2, 40.0, 40.0));

    // And after the next flight, only the distance covered since is offered
    sendLocalPosition(vehicle(), 40.0F, 40.0F, 0.0F);
    QTRY_VERIFY_WITH_TIMEOUT(gridView->property("canReanchorPlan").toBool(), TestTimeout::mediumMs());
    QCOMPARE(gridView->property("reanchorNorthMetres").toDouble(), 20.0);
    QCOMPARE(gridView->property("reanchorEastMetres").toDouble(), 20.0);

    QCOMPARE(reanchor(), 2);
    QVERIFY2(pointIsAbout(1, 60.0, 40.0), "the third flight moves the pattern by one flight's worth");
    QVERIFY(pointIsAbout(2, 60.0, 60.0));
}

/// A pattern drawn after one has been moved is laid out from the origin like every other one, so
/// where the last plan was moved to must not be carried over to it -- that would take the first move
/// of the new pattern short by however far the old one had travelled.
void LocalGridViewTest::_aFreshPatternIsNotOffsetByTheLastPlansMove_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    stub->setProperty("takeoffSpecifiesCoordinate", false);
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QQmlComponent planComponent(&gridViewEngine);
    QString planError;
    const QScopedPointer<QObject> plan(createPlanMasterControllerStub(planComponent, planError));
    QVERIFY2(plan, qPrintable(planError));
    gridView->setProperty("planMasterController", QVariant::fromValue(plan.get()));
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "resetPlanAnchor", Qt::DirectConnection));

    const auto place = [&gridView](double north, double east) {
        QVariant added;
        const bool called = QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                                      Q_RETURN_ARG(QVariant, added),
                                                      Q_ARG(QVariant, north), Q_ARG(QVariant, east));
        return called && added.toBool();
    };
    QVERIFY(place(20.0, 0.0));

    sendLocalPosition(vehicle(), 15.0F, -5.0F, 0.0F);
    QTRY_VERIFY_WITH_TIMEOUT(gridView->property("canReanchorPlan").toBool(), TestTimeout::mediumMs());

    QVariant moved;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "reanchorPlanToVehicle", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved)));
    QCOMPARE(moved.toInt(), 1);
    QCOMPARE(gridView->property("planAnchorNorth").toDouble(), 15.0);

    // The operator wipes the plan and draws a new one, the way a second sortie starts
    const int itemCount = stub->property("items").toList().count();
    for (int i = itemCount - 1; i >= 0; i--) {
        QVERIFY(QMetaObject::invokeMethod(stub.get(), "removeVisualItem", Qt::DirectConnection,
                                          Q_ARG(QVariant, i)));
    }
    QCOMPARE(gridView->property("missionPoints").value<QJSValue>()
                 .property(QStringLiteral("length")).toInt(), 0);

    QVERIFY(place(20.0, 0.0));
    QCOMPARE(gridView->property("planAnchorNorth").toDouble(), 0.0);
    QCOMPARE(gridView->property("planAnchorEast").toDouble(), 0.0);

    // So the whole distance from the origin is offered again, not what is left of it
    QCOMPARE(gridView->property("reanchorNorthMetres").toDouble(), 15.0);
    QCOMPARE(gridView->property("reanchorEastMetres").toDouble(), -5.0);
}

/// A plan already on the vehicle is picked up where the last flight stopped, not at its head.
///
/// ArduPilot resumes rather than restarts -- MIS_RESTART defaults to Resume -- so entering Auto after
/// a flight that was cut short carries on from the item it stopped on. The aircraft takes off and
/// then flies to the middle of the route, which is what a second flight "not working" looks like
/// from the ground. Nothing on screen said so, because the vehicle's own index counts things nobody
/// placed: the home position, and the DO_CHANGE_SPEED a waypoint's speed is uploaded as.
void LocalGridViewTest::_planSaysWhereTheVehicleWouldPickItUp_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));

    // ArduPilot's shape: home at 0, a takeoff at 1 carrying no coordinate, a waypoint at 2 whose
    // speed occupies 3, and a landing at 4.
    stub->setProperty("takeoffSpecifiesCoordinate", false);
    QVERIFY(QMetaObject::invokeMethod(stub.get(), "addHomeItem", Qt::DirectConnection,
                                      Q_ARG(QVariant, QVariant::fromValue(origin))));
    QVERIFY(QMetaObject::invokeMethod(stub.get(), "insertTakeoffItem", Qt::DirectConnection,
                                      Q_ARG(QVariant, QVariant::fromValue(origin)),
                                      Q_ARG(QVariant, -1), Q_ARG(QVariant, true)));
    QVERIFY(QMetaObject::invokeMethod(
        stub.get(), "addItem", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0, 0.0))),
        Q_ARG(QVariant, 2)));
    QVERIFY(QMetaObject::invokeMethod(
        stub.get(), "addItem", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant::fromValue(origin.atDistanceAndAzimuth(20.0, 90.0))),
        Q_ARG(QVariant, 4)));

    const QVariantList items = stub->property("items").toList();
    QCOMPARE(items.count(), 4);
    // The stub numbers items as it appends them, which puts the takeoff at 2. On the wire it is 1:
    // home takes 0, and this test is about the mapping between the two numberings.
    QObject *const takeoff = items.at(1).value<QObject *>();
    QVERIFY(takeoff);
    takeoff->setProperty("sequenceNumber", 1);
    QObject *const waypoint = items.at(2).value<QObject *>();
    QVERIFY(waypoint);
    waypoint->setProperty("lastSequenceNumber", 3);

    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    const auto resumeNumber = [&gridView]() {
        return gridView->property("vehicleResumeItemNumber").toInt();
    };

    // Nothing reported yet, so nothing to warn about
    QCOMPARE(resumeNumber(), 0);

    // Sitting on home is the head of the plan, which is where it should start
    stub->setProperty("currentMissionIndex", 0);
    QCOMPARE(resumeNumber(), 0);

    // The takeoff. Still the head of the plan.
    stub->setProperty("currentMissionIndex", 1);
    QCOMPARE(resumeNumber(), 1);

    // The waypoint, and then its own speed item. Both are the same item as far as the operator is
    // concerned, and naming the speed as a separate one would point at a waypoint nobody placed.
    stub->setProperty("currentMissionIndex", 2);
    QCOMPARE(resumeNumber(), 2);
    stub->setProperty("currentMissionIndex", 3);
    QCOMPARE(resumeNumber(), 2);

    // The landing -- the third item on screen, not the fourth number on the wire
    stub->setProperty("currentMissionIndex", 4);
    QCOMPARE(resumeNumber(), 3);

    // And the plan can be sent back to its head. The plan's own first sequence number rather than a
    // literal one, because what the firmware counts differs.
    QVariant restarted;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "restartPlanOnVehicle", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, restarted)));
    QVERIFY2(restarted.toBool(), "a disarmed vehicle holding a plan must accept being sent to its head");

    // Not while it is armed: the index is what the aircraft is flying to, and moving it mid-flight
    // sends it somewhere nobody asked for.
    vehicle()->setArmedShowError(true);
    QTRY_VERIFY_WITH_TIMEOUT(vehicle()->armed(), TestTimeout::longMs());
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "restartPlanOnVehicle", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, restarted)));
    QCOMPARE(restarted.toBool(), false);
}

/// Upload from the grid used to go straight to sendToVehicle, skipping the pre-check the Plan view
/// runs before its own. Two of the states that check catches are the ones this view meets most:
/// a mission already running on the vehicle, and a plan file built for another firmware or vehicle
/// class. Both upload cleanly and fly as something else.
void LocalGridViewTest::_uploadIsRefusedWhenThePreCheckSaysSo_test()
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
    // The panel reaches its mission controller through the plan controller, the way the real one does
    plan->setProperty("missionController", QVariant::fromValue(stub.get()));
    gridView->setProperty("planMasterController", QVariant::fromValue(plan.get()));

    auto *const gridItem = qobject_cast<QQuickItem *>(gridView.get());
    QVERIFY(gridItem);
    const QList<QQuickItem *> panels = collectItemsNamed(gridItem, QStringLiteral("localGrid_missionActions"));
    QCOMPARE(panels.count(), 1);
    QQuickItem *const panel = panels.first();

    const auto upload = [panel]() {
        return QMetaObject::invokeMethod(panel, "_upload", Qt::DirectConnection);
    };
    const auto sendCount = [&plan]() { return plan->property("sendCount").toInt(); };

    // MissionController::SendToVehiclePreCheckState, by the order of its own enum
    constexpr int kPreCheckOk = 0;
    constexpr int kPreCheckNoActiveVehicle = 1;
    constexpr int kPreCheckFirmwareMismatch = 2;
    constexpr int kPreCheckActiveMission = 3;

    stub->setProperty("preCheckState", kPreCheckOk);
    QVERIFY(upload());
    QCOMPARE(sendCount(), 1);

    // A mission running on the vehicle. ArduPilot rewrites its mission store in place, so a write
    // landing now leaves the aircraft part way through a route that no longer exists.
    stub->setProperty("preCheckState", kPreCheckActiveMission);
    QVERIFY(upload());
    QCOMPARE(sendCount(), 1);

    stub->setProperty("preCheckState", kPreCheckNoActiveVehicle);
    QVERIFY(upload());
    QCOMPARE(sendCount(), 1);

    // A mismatched plan is asked about rather than refused -- the operator may know better -- so
    // nothing is sent until the confirmation is answered
    stub->setProperty("preCheckState", kPreCheckFirmwareMismatch);
    QVERIFY(upload());
    QCOMPARE(sendCount(), 1);

    // An item still waiting on data has no coordinate to send, whatever the pre-check says.
    // VisualMissionItem::NotReadyForSaveData is 2.
    stub->setProperty("preCheckState", kPreCheckOk);
    plan->setProperty("saveState", 2);
    QVERIFY(upload());
    QCOMPARE(sendCount(), 1);

    plan->setProperty("saveState", 0);
    QVERIFY(upload());
    QCOMPARE(sendCount(), 2);
}

/// The plan outlives the view that moved it, so the anchor has to as well.
///
/// A pattern moved to the aircraft and uploaded is on the vehicle. Close QGC, open it again, and the
/// fly view shows that same moved plan back from the vehicle -- but an anchor held only in the view
/// went back to the origin with it, so the button offered to move the pattern by the whole distance
/// a second time. The operator gets a plan twice as far out as they asked for, on the press that
/// worked correctly the day before.
///
/// Stored against the vehicle it was measured under, because a distance travelled by one aircraft
/// says nothing about where another one's plan is drawn.
void LocalGridViewTest::_planAnchorOutlivesTheView_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    stub->setProperty("takeoffSpecifiesCoordinate", false);
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QQmlComponent planComponent(&gridViewEngine);
    QString planError;
    const QScopedPointer<QObject> plan(createPlanMasterControllerStub(planComponent, planError));
    QVERIFY2(plan, qPrintable(planError));
    gridView->setProperty("planMasterController", QVariant::fromValue(plan.get()));
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "resetPlanAnchor", Qt::DirectConnection));

    QVariant added;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

    sendLocalPosition(vehicle(), 20.0F, 10.0F, 0.0F);
    QTRY_VERIFY_WITH_TIMEOUT(gridView->property("canReanchorPlan").toBool(), TestTimeout::mediumMs());

    QVariant moved;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "reanchorPlanToVehicle", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved)));
    QCOMPARE(moved.toInt(), 1);
    QCOMPARE(gridView->property("planAnchorNorth").toDouble(), 20.0);
    QCOMPARE(gridView->property("planAnchorEast").toDouble(), 10.0);

    // A second view of the same aircraft, which is what the next run of QGC amounts to
    QQmlComponent restartedComponent(&gridViewEngine);
    QString restartedError;
    const QScopedPointer<QObject> restarted(createGridView(restartedComponent, vehicle(), restartedError));
    QVERIFY2(restarted, qPrintable(restartedError));

    QCOMPARE(restarted->property("planAnchorNorth").toDouble(), 20.0);
    QCOMPARE(restarted->property("planAnchorEast").toDouble(), 10.0);
    QVERIFY2(restarted->property("planStartsAtVehicle").toBool(),
             "a plan already moved to this aircraft must not be offered the move again after a restart");

    // A view with no vehicle, and one whose vehicle is not the one the anchor was recorded under,
    // both read the plan as undrawn rather than inheriting a distance that was never theirs
    QQmlComponent noVehicleComponent(&gridViewEngine);
    QString noVehicleError;
    const QScopedPointer<QObject> noVehicle(createGridView(noVehicleComponent, nullptr, noVehicleError));
    QVERIFY2(noVehicle, qPrintable(noVehicleError));
    QCOMPARE(noVehicle->property("planAnchorNorth").toDouble(), 0.0);
    QCOMPARE(noVehicle->property("planAnchorEast").toDouble(), 0.0);

    FlyViewSettings *const flyViewSettings = SettingsManager::instance()->flyViewSettings();
    QVERIFY(flyViewSettings);
    flyViewSettings->localGridPlanAnchorVehicleId()->setRawValue(vehicle()->id() + 1);
    QCOMPARE(restarted->property("planAnchorNorth").toDouble(), 0.0);
    QCOMPARE(restarted->property("planAnchorEast").toDouble(), 0.0);

    // And clearing it puts every view back to a pattern drawn from the origin
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "resetPlanAnchor", Qt::DirectConnection));
    QCOMPARE(flyViewSettings->localGridPlanAnchorVehicleId()->rawValue().toUInt(), 0U);
    QCOMPARE(gridView->property("planAnchorNorth").toDouble(), 0.0);
}

// ============================================================================
// Bagian 3: mission-creation parity (Lampiran D)
// ============================================================================

/// A pattern is not always built end to end: an operator reviewing item 2 of 4 needs a new item to
/// land after item 2, not appended past item 4. This is the whole point of tying insertion to
/// MissionController's own current-item bookkeeping instead of always inserting at -1.
void LocalGridViewTest::_insertAfterSelectedItem_landsInTheMiddleOfThePlan_test()
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
    // takeoff, wp(10,0), wp(20,0) -- built end to end, the way a pattern normally is
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 10.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 3);
    const int firstWaypointVisualIndex = points.property(1).property(QStringLiteral("index")).toInt();

    // Select the first waypoint, not the last item, then place a new one
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, firstWaypointVisualIndex)));
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 15.0), Q_ARG(QVariant, 5.0)));
    QVERIFY(added.toBool());

    points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 4);
    // takeoff, wp(10,0), the new wp(15,5), wp(20,0) -- in that order
    QVERIFY(qAbs(points.property(1).property(QStringLiteral("north")).toNumber() - 10.0) < 0.05);
    QVERIFY(qAbs(points.property(2).property(QStringLiteral("north")).toNumber() - 15.0) < 0.05);
    QVERIFY(qAbs(points.property(2).property(QStringLiteral("east")).toNumber() - 5.0) < 0.05);
    QVERIFY(qAbs(points.property(3).property(QStringLiteral("north")).toNumber() - 20.0) < 0.05);
}

/// Replaces _selectNewestItem, which picked the newest item off the end of the plan -- correct only
/// while every insert landed there. Once an insert can land anywhere, following the controller's own
/// current item is the only version of this that opens the item just placed rather than whatever
/// happens to sit last.
void LocalGridViewTest::_insertAfterSelectedItem_selectsTheNewItemNotTheLastOne_test()
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
                                      Q_ARG(QVariant, 10.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    const int firstWaypointVisualIndex = points.property(1).property(QStringLiteral("index")).toInt();
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, firstWaypointVisualIndex)));

    QVariant placed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, placed),
                                      Q_ARG(QVariant, 15.0), Q_ARG(QVariant, 5.0)));
    QVERIFY(placed.toBool());

    points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 4);
    const int newItemVisualIndex = points.property(2).property(QStringLiteral("index")).toInt();
    const int lastItemVisualIndex = points.property(3).property(QStringLiteral("index")).toInt();

    QVERIFY2(gridView->property("selectedWaypointIndex").toInt() == newItemVisualIndex,
             "the item just placed must be the one selected");
    QVERIFY2(gridView->property("selectedWaypointIndex").toInt() != lastItemVisualIndex,
             "picking the newest item off the end of the plan is exactly the bug this replaces");
}

/// The takeoff is pinned to the origin, and the rule that keeps anything else from landing in front
/// of it falls out of the same mechanism that makes mid-plan insertion work at all: the smallest
/// insert index the controller can ever hand back is one past the takeoff itself.
void LocalGridViewTest::_nothingCanBeInsertedBeforeTheTakeoff_test()
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
                                      Q_ARG(QVariant, 10.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    const int takeoffVisualIndex = points.property(0).property(QStringLiteral("index")).toInt();
    QVERIFY(points.property(0).property(QStringLiteral("isPinned")).toBool());

    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, takeoffVisualIndex)));
    QVariant placed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, placed),
                                      Q_ARG(QVariant, 30.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(placed.toBool());

    points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 3);
    QVERIFY2(points.property(0).property(QStringLiteral("isPinned")).toBool(),
             "the takeoff must stay the plan's first item even when it is the one selected");
    QVERIFY(qAbs(points.property(1).property(QStringLiteral("north")).toNumber() - 30.0) < 0.05);
}

/// "Land here" builds a plain waypoint and swaps its command afterward, so it never goes through
/// insertLandItem and never picks up isInsertLandValid's own refusal on its own. Without
/// canInsertLandHere it would happily insert a landing in the middle of a pattern with legs after it
/// the aircraft would never fly.
void LocalGridViewTest::_landHere_refusesAMidPlanSpot_test()
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
                                      Q_ARG(QVariant, 10.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

    QVERIFY2(gridView->property("canInsertLandHere").toBool(),
             "landing past the end of the plan must still be offered");

    // Select the first waypoint, mimicking what the real controller's isInsertLandValid computes
    // for any item that is not the plan's last fly-through one
    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    const int firstWaypointVisualIndex = points.property(1).property(QStringLiteral("index")).toInt();
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, firstWaypointVisualIndex)));
    stub->setProperty("isInsertLandValid", false);

    QVERIFY2(!gridView->property("canInsertLandHere").toBool(),
             "landing here must be refused once it would not land past the end of the plan");

    QVariant placed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addMissionItemAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, placed),
                                      Q_ARG(QVariant, QStringLiteral("landHere")),
                                      Q_ARG(QVariant, 5.0), Q_ARG(QVariant, 5.0)));
    QVERIFY2(!placed.toBool(), "a refused landing must not be inserted");
    QCOMPARE(stub->property("insertCount").toInt(), 2);
}

/// An armed tool places at the point already selected, which is the entire reason to arm one
/// instead of opening the click panel every time. Clearing the selection before placing would turn
/// every click after the first back into a silent append.
void LocalGridViewTest::_armedTool_placesAfterTheSelectionWithoutClearingIt_test()
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
                                      Q_ARG(QVariant, 10.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    const int firstWaypointVisualIndex = points.property(1).property(QStringLiteral("index")).toInt();
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, firstWaypointVisualIndex)));
    QCOMPARE(gridView->property("selectedWaypointIndex").toInt(), firstWaypointVisualIndex);

    gridView->setProperty("armedTool", QStringLiteral("waypoint"));

    QVariant placedTool;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "placeArmedTool", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, placedTool),
                                      Q_ARG(QVariant, 15.0), Q_ARG(QVariant, 5.0)));
    QVERIFY(placedTool.toBool());

    points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 4);
    // takeoff, wp(10,0), the placed one, wp(20,0) -- landed right after the selection, not appended
    QVERIFY(qAbs(points.property(2).property(QStringLiteral("north")).toNumber() - 15.0) < 0.05);
    QVERIFY(qAbs(points.property(3).property(QStringLiteral("north")).toNumber() - 20.0) < 0.05);
    QCOMPARE(gridView->property("armedTool").toString(), QStringLiteral("waypoint"));
}

/// Four quick clicks with the tool still armed is how a pattern gets built on a grid with no map to
/// click a drop panel open on for each one. Each placement has to continue from the one before it,
/// not from wherever the operator's selection happened to be when the tool was first armed.
void LocalGridViewTest::_armedTool_chainsSeveralPlacementsInARow_test()
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

    gridView->setProperty("armedTool", QStringLiteral("waypoint"));

    // The box pattern the project's own mission script flies, placed as four quick clicks
    const double cornerNorth[] = {20.0, 20.0, 0.0, 0.0};
    const double cornerEast[]  = {0.0, 20.0, 20.0, 0.0};
    for (int i = 0; i < 4; i++) {
        QVariant placed;
        QVERIFY(QMetaObject::invokeMethod(gridView.get(), "placeArmedTool", Qt::DirectConnection,
                                          Q_RETURN_ARG(QVariant, placed),
                                          Q_ARG(QVariant, cornerNorth[i]), Q_ARG(QVariant, cornerEast[i])));
        QVERIFY(placed.toBool());
    }

    // A takeoff plus the four corners, in the order they were clicked -- not reordered, and not
    // appended past a selection that never moved once the tool was armed
    const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 5);
    for (int i = 0; i < 4; i++) {
        const QJSValue point = points.property(i + 1);
        QVERIFY(qAbs(point.property(QStringLiteral("north")).toNumber() - cornerNorth[i]) < 0.05);
        QVERIFY(qAbs(point.property(QStringLiteral("east")).toNumber() - cornerEast[i]) < 0.05);
    }
}

/// The editor is a column of numbers read at a flight line, and it used to carry a paragraph of
/// background between every two of them. Those notes are still true and still reachable -- the
/// header carries one switch for all of them -- but they are not what the panel opens saying.
///
/// The switch governs background only. A note that reports what has happened to the item in hand,
/// like an altitude the ceiling has just pulled down, is not the operator's to switch off.
void LocalGridViewTest::_backgroundNotesWaitToBeAskedFor_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    Fact *const helpSetting = SettingsManager::instance()->flyViewSettings()->showLocalGridPlanHelp();
    QVERIFY(helpSetting);
    helpSetting->setRawValue(false);

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    QVariant added;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 10.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

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

    // The waypoint, opened, which is the only state any of these notes appear in
    const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, points.property(1).property(QStringLiteral("index")).toInt())));

    // Walked as items rather than as QObjects: the rows come out of a Repeater, which gives its
    // delegates a parent item and no QObject parent at all
    auto *const listItem = qobject_cast<QQuickItem *>(list.get());
    QVERIFY(listItem);
    const auto hintVisible = [listItem]() {
        const QList<QQuickItem *> found = collectItemsNamed(listItem, QStringLiteral("localGrid_dragHint"));
        return !found.isEmpty() && found.first()->isVisible();
    };
    const auto hintBuilt = [listItem]() {
        return !collectItemsNamed(listItem, QStringLiteral("localGrid_dragHint")).isEmpty();
    };

    QTRY_VERIFY_WITH_TIMEOUT(hintBuilt(), TestTimeout::mediumMs());
    QVERIFY2(!hintVisible(), "the editor opened carrying background nobody asked for");

    helpSetting->setRawValue(true);
    QTRY_VERIFY_WITH_TIMEOUT(hintVisible(), TestTimeout::mediumMs());

    // And the switch is in the header, where it is on screen whether or not a row is open
    QVERIFY2(!collectItemsNamed(listItem, QStringLiteral("localGrid_missionListHelpToggle")).isEmpty(),
             "there is no way back to the notes the editor stopped showing");

    helpSetting->setRawValue(false);
    QTRY_VERIFY_WITH_TIMEOUT(!hintVisible(), TestTimeout::mediumMs());
}

/// Insert-after and duplicate are the only way to reach those edits now that the row carries them as
/// header icons rather than a row of buttons, so their presence on the open row is worth pinning --
/// and their absence where the plan forbids them: the takeoff may not be duplicated, since only a
/// plan's first item may be one.
void LocalGridViewTest::_rowEditIconsAppearOnlyWhereTheEditIsAllowed_test()
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
                                      Q_ARG(QVariant, 10.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

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

    auto *const listItem = qobject_cast<QQuickItem *>(list.get());
    QVERIFY(listItem);

    // Every row builds its own pair of icons and hides them unless it is the open one, so the
    // question is always which row is offering the edit -- asking the list for the first icon of a
    // given name answers for the takeoff, whichever row happens to be open.
    const auto shownOn = [listItem](int visualItemIndex, const QString &name) {
        const QList<QQuickItem *> rows = collectItemsNamed(listItem, QStringLiteral("localGrid_missionItemRow"));
        for (QQuickItem *const row : rows) {
            if (row->property("visualItemIndex").toInt() != visualItemIndex) {
                continue;
            }
            const QList<QQuickItem *> found = collectItemsNamed(row, name);
            return !found.isEmpty() && found.first()->isVisible();
        }
        return false;
    };
    const auto shownAnywhere = [listItem](const QString &name) {
        const QList<QQuickItem *> found = collectItemsNamed(listItem, name);
        for (QQuickItem *const item : found) {
            if (item->isVisible()) {
                return true;
            }
        }
        return false;
    };

    const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    const int takeoffIndex  = points.property(0).property(QStringLiteral("index")).toInt();
    const int waypointIndex = points.property(1).property(QStringLiteral("index")).toInt();

    // Nothing open: no edit icons anywhere in the list
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "clearWaypointSelection", Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(!shownAnywhere(QStringLiteral("localGrid_rowDuplicateButton")), TestTimeout::mediumMs());
    QVERIFY(!shownAnywhere(QStringLiteral("localGrid_rowInsertAfterButton")));

    // The waypoint opened: it may be duplicated and, sitting at the end of the plan, may not be
    // insert-split (nothing follows it)
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, waypointIndex)));
    QTRY_VERIFY_WITH_TIMEOUT(shownOn(waypointIndex, QStringLiteral("localGrid_rowDuplicateButton")),
                             TestTimeout::mediumMs());
    QVERIFY2(!shownOn(waypointIndex, QStringLiteral("localGrid_rowInsertAfterButton")),
             "the plan's last flown-through item has no leg after it to split");

    // The takeoff opened: it is the one item that may not be duplicated, and the leg to the waypoint
    // below it is the one that may be split
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, takeoffIndex)));
    QTRY_VERIFY_WITH_TIMEOUT(shownOn(takeoffIndex, QStringLiteral("localGrid_rowInsertAfterButton")),
                             TestTimeout::mediumMs());
    QVERIFY2(!shownOn(takeoffIndex, QStringLiteral("localGrid_rowDuplicateButton")),
             "only a plan's first item may be a takeoff, so a copy of it is refused");
    QVERIFY2(!shownAnywhere(QStringLiteral("localGrid_rowDuplicateButton")),
             "the row that was closed must have taken its icons with it");
}

/// The switch that puts the background notes back is inside the mission list's header, and the whole
/// of that header is a click target that folds the panel. The header's own mouse area was declared
/// after the row holding the switch, which puts it on top of the switch: every press meant for the
/// notes folded the panel instead, and there was no way back to them at all.
///
/// Pressed rather than set, because setting the fact directly is exactly the path that passed while
/// the control it belongs to could not be reached.
void LocalGridViewTest::_theHelpSwitchTakesTheTapItIsGiven_test()
{
    QVERIFY(vehicle());
    const QGeoCoordinate origin(47.3977419, 8.5455938, 488.0);
    QVERIFY(setEstimatorOrigin(vehicle(), mockLink(), origin));

    Fact *const helpSetting = SettingsManager::instance()->flyViewSettings()->showLocalGridPlanHelp();
    QVERIFY(helpSetting);
    helpSetting->setRawValue(false);

    MAKE_GRID_VIEW(gridView);
    QQmlComponent stubComponent(&gridViewEngine);
    QString stubError;
    const QScopedPointer<QObject> stub(createMissionControllerStub(stubComponent, stubError));
    QVERIFY2(stub, qPrintable(stubError));
    gridView->setProperty("missionController", QVariant::fromValue(stub.get()));

    const auto press = [](QQuickWindow &window, QQuickItem *item) {
        const QPointF centre = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
    };

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
    list->setProperty("collapsed", false);

    QQuickWindow listWindow;
    QVERIFY(_showInWindow(listWindow, list.get()));

    auto *const listItem = qobject_cast<QQuickItem *>(list.get());
    QVERIFY(listItem);
    QQuickItem *const listToggle =
        collectItemsNamed(listItem, QStringLiteral("localGrid_missionListHelpToggle")).value(0);
    QVERIFY(listToggle);
    QTRY_VERIFY_WITH_TIMEOUT(listToggle->width() > 0, TestTimeout::mediumMs());

    press(listWindow, listToggle);
    QTRY_VERIFY_WITH_TIMEOUT(helpSetting->rawValue().toBool(), TestTimeout::mediumMs());
    QVERIFY2(!list->property("collapsed").toBool(), "the press folded the panel instead of answering");
}

/// The green disc says which item the aircraft is flying to; it does not say what altitude that leg
/// holds or what speed it is being flown at, and those are inside the row. Following opens it, so
/// the numbers that matter in the air arrive without the operator hunting for the disc on every leg.
///
/// It has to lose every argument with the operator. Opening any item by hand suspends it, and only
/// closing that item hands the panel back -- from the next item on, never by reopening the row that
/// was just closed.
void LocalGridViewTest::_theListFollowsTheItemBeingFlownTo_test()
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
    for (const double north : {10.0, 20.0}) {
        QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                          Q_RETURN_ARG(QVariant, added),
                                          Q_ARG(QVariant, north), Q_ARG(QVariant, 0.0)));
        QVERIFY(added.toBool());
    }

    // takeoff, then the two waypoints
    const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 3);
    const auto sequenceOf = [&points](int row) {
        return points.property(row).property(QStringLiteral("sequence")).toInt();
    };
    const auto indexOf = [&points](int row) {
        return points.property(row).property(QStringLiteral("index")).toInt();
    };

    const auto selected = [&gridView]() { return gridView->property("selectedWaypointIndex").toInt(); };

    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "clearWaypointSelection", Qt::DirectConnection));
    QCOMPARE(selected(), -1);

    // Nothing opens while the aircraft is on the ground. A plan being built carries a mission index
    // too, and pulling the panel onto it would take it away from the item being edited.
    stub->setProperty("currentMissionIndex", sequenceOf(1));
    QCOMPARE(selected(), -1);

    vehicle()->setArmedShowError(true);
    QTRY_VERIFY_WITH_TIMEOUT(vehicle()->armed(), TestTimeout::longMs());

    // The item being flown to, not the one just finished. missionPoints computes its own
    // isVehicleTarget flag from this same sequence, and the follow must not depend on that binding
    // having run first -- read that way it opened the previous leg on every advance.
    stub->setProperty("currentMissionIndex", sequenceOf(2));
    QVERIFY2(selected() == indexOf(2), "the list opened an item other than the one being flown to");

    // The operator opens the takeoff to check the height it climbs to. The aircraft moving on must
    // not take that away from them.
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, indexOf(0))));
    QCOMPARE(selected(), indexOf(0));
    stub->setProperty("currentMissionIndex", sequenceOf(1));
    QVERIFY2(selected() == indexOf(0), "the aircraft advancing pulled the panel off the item being read");

    // Closing it gives the list back -- at the next item, not by reopening what was just closed
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "clearWaypointSelection", Qt::DirectConnection));
    QCOMPARE(selected(), -1);
    stub->setProperty("currentMissionIndex", sequenceOf(2));
    QCOMPARE(selected(), indexOf(2));

    vehicle()->setArmedShowError(false);
    QTRY_VERIFY_WITH_TIMEOUT(!vehicle()->armed(), TestTimeout::longMs());
}

/// Every tool the strip can arm has to place something. "landHere" armed cleanly, lit its button and
/// then had every tap on the grid fall through placeArmedTool's default case -- which from the
/// operator's side is a button that does nothing and a grid that has stopped responding.
///
/// The landing is also the one tool that puts itself down: a plan has a single ending, so leaving it
/// armed would leave the button lit over a tool the plan refuses every further use of.
void LocalGridViewTest::_everyArmableToolPlacesSomething_test()
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

    // Each of the three the tool strip arms, placed in turn onto the plan the one before it left
    const QStringList tools = {QStringLiteral("waypoint"), QStringLiteral("roi"), QStringLiteral("landHere")};
    for (const QString &tool : tools) {
        gridView->setProperty("armedTool", tool);
        QCOMPARE(gridView->property("armedTool").toString(), tool);

        QVariant placed;
        QVERIFY(QMetaObject::invokeMethod(gridView.get(), "placeArmedTool", Qt::DirectConnection,
                                          Q_RETURN_ARG(QVariant, placed),
                                          Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 10.0)));
        QVERIFY2(placed.toBool(), qPrintable(QStringLiteral("the %1 tool armed but placed nothing").arg(tool)));
    }

    // The landing put itself down; the two before it stayed in hand, which is what building a
    // pattern out of several of each depends on
    QCOMPARE(gridView->property("armedTool").toString(), QString());

    // takeoff, waypoint, ROI, landing -- the ROI carries a coordinate, so all four are on the grid
    const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 4);
}

/// Plan mode is what puts an insert tool in the operator's hand, so leaving it has to take the tool
/// back. A tool that outlived the mode would turn the next tap on the grid -- a tap meant to read a
/// point, now that the click panel offers nothing else -- into an edit of the plan.
void LocalGridViewTest::_planEditMode_dropsTheArmedToolWhenItEnds_test()
{
    MAKE_GRID_VIEW(gridView);

    gridView->setProperty("planEditMode", true);
    gridView->setProperty("armedTool", QStringLiteral("waypoint"));
    QCOMPARE(gridView->property("armedTool").toString(), QStringLiteral("waypoint"));

    gridView->setProperty("planEditMode", false);
    QCOMPARE(gridView->property("armedTool").toString(), QString());
}

/// Switching the grid off is switching plan mode off. The view is hidden rather than destroyed, so
/// a mode left standing would come back with the plan buttons on the tool strip and nothing under
/// them to tap -- and with a tool still armed for a grid the operator can no longer see.
void LocalGridViewTest::_planEditMode_endsWhenTheGridIsHidden_test()
{
    MAKE_GRID_VIEW(gridView);

    gridView->setProperty("planEditMode", true);
    gridView->setProperty("armedTool", QStringLiteral("waypoint"));

    gridView->setProperty("visible", false);
    QVERIFY(!gridView->property("planEditMode").toBool());
    QCOMPARE(gridView->property("armedTool").toString(), QString());
}

/// The state the tool strip refuses every insert but Take off in, and the state the grid puts one
/// line of explanation on screen for. It has to describe the plan rather than the mode: outside plan
/// mode there is nothing to explain, and the moment the takeoff exists the refusal is over.
void LocalGridViewTest::_planNeedsTakeoffFirst_clearsOnceTheTakeoffIsPlaced_test()
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

    // Nothing to say while the strip is showing the flying controls
    QVERIFY(!gridView->property("planNeedsTakeoffFirst").toBool());

    gridView->setProperty("planEditMode", true);
    QVERIFY(gridView->property("planNeedsTakeoffFirst").toBool());

    QVariant added;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "insertTakeoffAtOrigin", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added)));
    QVERIFY(added.toBool());
    QVERIFY(gridView->property("planHasTakeoff").toBool());
    QVERIFY(!gridView->property("planNeedsTakeoffFirst").toBool());
}

/// An ROI carries a real coordinate and belongs on the grid, but the aircraft is never routed to
/// it -- so it must still be drawn, while the leg reaching the item after it is measured from the
/// last item actually flown to, not from the ROI.
void LocalGridViewTest::_roiIsDrawnButNotFlownThrough_test()
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
                                      Q_ARG(QVariant, 10.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

    gridView->setProperty("armedTool", QStringLiteral("roi"));
    QVariant roiPlaced;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "placeArmedTool", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, roiPlaced),
                                      Q_ARG(QVariant, 5.0), Q_ARG(QVariant, 5.0)));
    QVERIFY(roiPlaced.toBool());
    QCOMPARE(stub->property("roiCount").toInt(), 1);

    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

    // Drawn: the ROI has a real position and belongs on the grid, listed alongside every other item
    const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 4);
    bool foundRoi = false;
    int lastVisualIndex = -1;
    for (int i = 0; i < 4; i++) {
        const QJSValue point = points.property(i);
        lastVisualIndex = point.property(QStringLiteral("index")).toInt();
        if ((qAbs(point.property(QStringLiteral("north")).toNumber() - 5.0) < 0.05)
            && (qAbs(point.property(QStringLiteral("east")).toNumber() - 5.0) < 0.05)) {
            foundRoi = true;
            QVERIFY2(point.property(QStringLiteral("onGrid")).toBool(),
                     "an ROI has a real coordinate and belongs on the grid");
            QVERIFY2(!point.property(QStringLiteral("flyThrough")).toBool(), "an ROI is never flown to");
        }
    }
    QVERIFY2(foundRoi, "the ROI must still be listed among the plan's points");

    // Not flown through: the leg reaching the last waypoint is measured from the waypoint before
    // the ROI, not from the ROI itself
    QVariant legStart;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "legStartFor", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, legStart), Q_ARG(QVariant, lastVisualIndex)));
    const QVariantMap legStartMap = legStart.toMap();
    QVERIFY(qAbs(legStartMap.value(QStringLiteral("north")).toDouble() - 10.0) < 0.05);
    QVERIFY(qAbs(legStartMap.value(QStringLiteral("east")).toDouble()) < 0.05);
}

/// A plan arriving whole from the vehicle -- a download, a clear, a load -- is not the plan whatever
/// was selected belonged to. Left alone, the same index could now name a completely different item
/// and silently reopen its editor without anything having been clicked.
void LocalGridViewTest::_selectionIsClearedWhenThePlanArrivesWhole_test()
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
                                      Q_ARG(QVariant, 10.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    const int waypointVisualIndex = points.property(1).property(QStringLiteral("index")).toInt();
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, waypointVisualIndex)));
    QCOMPARE(gridView->property("selectedWaypointIndex").toInt(), waypointVisualIndex);

    QVERIFY(QMetaObject::invokeMethod(stub.get(), "visualItemsReset", Qt::DirectConnection));

    QVERIFY2(gridView->property("selectedWaypointIndex").toInt() == -1,
             "a freshly arrived plan must not leave a stale index open as if it were still selected");
}

/// Building a pattern one leg at a time means the common edit is repeating a point -- flying the
/// same corner at a second altitude or speed to compare them, without retyping it.
void LocalGridViewTest::_duplicateItem_copiesPositionAltitudeAndSpeed_test()
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
                                      Q_ARG(QVariant, 10.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    const int waypointVisualIndex = points.property(1).property(QStringLiteral("index")).toInt();

    QObject *const original = stub->property("lastInsertedItem").value<QObject *>();
    QVERIFY(original);
    QObject *const originalAltitude = original->property("altitude").value<QObject *>();
    QVERIFY(originalAltitude);
    originalAltitude->setProperty("rawValue", 8.0);
    QObject *const originalSpeed = original->property("speedSection").value<QObject *>();
    QVERIFY(originalSpeed);
    originalSpeed->setProperty("specifyFlightSpeed", true);
    QObject *const originalSpeedFact = originalSpeed->property("flightSpeed").value<QObject *>();
    QVERIFY(originalSpeedFact);
    originalSpeedFact->setProperty("rawValue", 3.0);

    QVariant duplicated;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "duplicateItem", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, duplicated),
                                      Q_ARG(QVariant, waypointVisualIndex)));
    QVERIFY(duplicated.toBool());

    points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 3);
    // Right after the item it was copied from
    QVERIFY(qAbs(points.property(2).property(QStringLiteral("north")).toNumber() - 10.0) < 0.05);

    QObject *const copy = stub->property("lastInsertedItem").value<QObject *>();
    QVERIFY(copy);
    QVERIFY2(copy != original, "the duplicate must be a new item, not the same one selected twice");
    QObject *const copyAltitude = copy->property("altitude").value<QObject *>();
    QVERIFY(copyAltitude);
    QCOMPARE(copyAltitude->property("rawValue").toDouble(), 8.0);
    QObject *const copySpeed = copy->property("speedSection").value<QObject *>();
    QVERIFY(copySpeed);
    QVERIFY(copySpeed->property("specifyFlightSpeed").toBool());
    QObject *const copySpeedFact = copySpeed->property("flightSpeed").value<QObject *>();
    QVERIFY(copySpeedFact);
    QCOMPARE(copySpeedFact->property("rawValue").toDouble(), 3.0);
}

/// The common edit to a pattern already on the grid is turning one leg into two, and a waypoint
/// invented at the midpoint of the leg it splits is the only version of that which does not ask the
/// operator to work out a position for it by hand.
void LocalGridViewTest::_insertBetween_splitsTheLegAtItsMidpoint_test()
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
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 20.0)));
    QVERIFY(added.toBool());

    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 3);
    const int firstLegIndex = points.property(1).property(QStringLiteral("index")).toInt();

    QVariant split;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "insertWaypointBetween", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, split),
                                      Q_ARG(QVariant, firstLegIndex)));
    QVERIFY(split.toBool());

    points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 4);
    // The midpoint of (20,0) and (20,20) is (20,10)
    QVERIFY(qAbs(points.property(2).property(QStringLiteral("north")).toNumber() - 20.0) < 0.05);
    QVERIFY(qAbs(points.property(2).property(QStringLiteral("east")).toNumber() - 10.0) < 0.05);

    // The plan's last item has no leg after it to split
    const int lastVisualIndex = points.property(3).property(QStringLiteral("index")).toInt();
    QVariant refused;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "insertWaypointBetween", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, refused),
                                      Q_ARG(QVariant, lastVisualIndex)));
    QVERIFY2(!refused.toBool(), "there is no leg after the plan's last item to split");
}

/// The one thing a waypoint says besides where it is that an ArduCopter ever reads. Its mission
/// records are 15 bytes and cannot carry a delay and a radius both, so for every non-Plane build the
/// firmware keeps param1 and drops the rest -- and what it keeps it flies, holding the aircraft on
/// the point until the seconds run out. Offering it on a takeoff or a landing would be offering a
/// number those commands spend on something else entirely.
void LocalGridViewTest::_holdTimeIsOfferedOnWaypointsAlone_test()
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

    // A takeoff of its own accord, then the waypoint, then a landing: one plan holding all three of
    // the commands this grid places
    QVariant added;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 10.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addMissionItemAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added), Q_ARG(QVariant, QStringLiteral("land")),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

    const QVariantList items = stub->property("items").toList();
    QCOMPARE(items.count(), 3);
    QObject *const takeoff = items.at(0).value<QObject *>();
    QObject *const waypoint = items.at(1).value<QObject *>();
    QObject *const landing = items.at(2).value<QObject *>();
    QVERIFY(takeoff && waypoint && landing);

    // Published the way the command tree publishes it for an ArduPilot multirotor: in the advanced
    // list, because the tree marks this parameter advanced, and named for its label
    Fact *const hold = publishItemFact(waypoint, "textFieldFactsAdvanced", QStringLiteral("Hold"));
    QVERIFY(hold);

    // And the same name published on the takeoff, which is not the reason the field stays off it.
    // The guard is the command: param1 on a takeoff is a different quantity, and a grid that went by
    // the name alone would edit it the moment some firmware's tree happened to label it this way.
    QVERIFY(publishItemFact(takeoff, "textFieldFactsAdvanced", QStringLiteral("Hold")));

    // The item's own fact, not a copy of it: what the operator types into the field is the number
    // the item carries into the upload
    QVariant found;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "waypointHoldTimeFact", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, found), Q_ARG(QVariant, 1)));
    QCOMPARE(found.value<QObject *>(), static_cast<QObject *>(hold));

    for (const int index : { 0, 2 }) {
        QVERIFY(QMetaObject::invokeMethod(gridView.get(), "waypointHoldTimeFact", Qt::DirectConnection,
                                          Q_RETURN_ARG(QVariant, found), Q_ARG(QVariant, index)));
        QVERIFY2(found.value<QObject *>() == nullptr,
                 "a takeoff and a landing spend param1 on something else, so there is no wait to offer");
    }

    // And an index that names no item at all answers the same way rather than reaching into the
    // list past its end -- the panel asks with whatever index it last held, and a plan can shrink
    // under it
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "waypointHoldTimeFact", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, found), Q_ARG(QVariant, 99)));
    QVERIFY(found.value<QObject *>() == nullptr);
}

/// The totals panel has to add the waits itself: QGC's own flight-status calculation has no hold
/// term in it at all. A sum that missed one would show a plan as taking less time than it takes,
/// which is the number a battery is sized against.
void LocalGridViewTest::_holdSecondsAreSummedAcrossThePlan_test()
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
    for (const double north : { 10.0, 20.0 }) {
        QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                          Q_RETURN_ARG(QVariant, added),
                                          Q_ARG(QVariant, north), Q_ARG(QVariant, 0.0)));
        QVERIFY(added.toBool());
    }

    QVERIFY2(qFuzzyIsNull(gridView->property("missionHoldSeconds").toDouble()),
             "a plan whose waypoints publish no wait waits for nothing");

    const QVariantList items = stub->property("items").toList();
    QCOMPARE(items.count(), 3);
    Fact *const firstHold = publishItemFact(items.at(1).value<QObject *>(), "textFieldFactsAdvanced",
                                            QStringLiteral("Hold"));
    Fact *const secondHold = publishItemFact(items.at(2).value<QObject *>(), "textFieldFactsAdvanced",
                                             QStringLiteral("Hold"));
    QVERIFY(firstHold && secondHold);

    firstHold->setRawValue(5.0);
    secondHold->setRawValue(7.0);

    // Waited on rather than read once: the sum is a binding over every hold in the plan, and it is
    // re-run when one of them changes rather than at the moment it is asked for
    QTRY_COMPARE_WITH_TIMEOUT(gridView->property("missionHoldSeconds").toDouble(), 12.0,
                              TestTimeout::shortMs());

    // Editing one re-runs the sum. This is the whole reason the totals are a binding and not a
    // number worked out once when the panel was built.
    secondHold->setRawValue(20.0);
    QTRY_COMPARE_WITH_TIMEOUT(gridView->property("missionHoldSeconds").toDouble(), 25.0,
                              TestTimeout::shortMs());
}

/// What the panel shows, end to end: the distance QGC measured for the plan the grid drew, and a
/// duration that is the flying plus every wait. The duration is the one number here that does not
/// come from QGC as-is, and a quietly short one is worse than none at all.
void LocalGridViewTest::_planTotalsIncludeTheWaits_test()
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

    auto *const gridItem = qobject_cast<QQuickItem *>(gridView.get());
    QVERIFY(gridItem);
    QObject *const stats = gridItem->findChild<QObject *>(QStringLiteral("localGrid_missionStats"));
    QVERIFY2(stats, "the totals panel has to be findable, or nothing below is testing it");
    QVERIFY2(!stats->property("visible").toBool(),
             "an empty plan has no totals, and a panel saying so is chrome over the grid");

    // A 20 m square, which is the pattern this grid exists to fly: four corners, four legs of 20 m
    constexpr double kCornerNorths[] = { 20.0, 20.0, 0.0, 0.0 };
    constexpr double kCornerEasts[]  = {  0.0, 20.0, 20.0, 0.0 };
    QVariant added;
    for (size_t i = 0; i < std::size(kCornerNorths); i++) {
        QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                          Q_RETURN_ARG(QVariant, added),
                                          Q_ARG(QVariant, kCornerNorths[i]), Q_ARG(QVariant, kCornerEasts[i])));
        QVERIFY(added.toBool());
    }

    // What MissionController's own flight-status pass produces for that square at the 1 m/s this
    // grid writes onto every waypoint it places. Set here rather than derived, because reproducing
    // QGC's distance and speed integration would test a copy of it instead of what the grid does
    // with what it produced.
    stub->setProperty("missionTotalDistance", 80.0);
    stub->setProperty("missionTime", 80.0);

    const QVariantList items = stub->property("items").toList();
    QCOMPARE(items.count(), 5);
    Fact *const hold = publishItemFact(items.at(1).value<QObject *>(), "textFieldFactsAdvanced",
                                       QStringLiteral("Hold"));
    QVERIFY(hold);
    hold->setRawValue(10.0);

    QTRY_COMPARE_WITH_TIMEOUT(gridView->property("missionHoldSeconds").toDouble(), 10.0,
                              TestTimeout::shortMs());
    QCOMPARE(gridView->property("missionDistanceMetres").toDouble(), 80.0);
    QVERIFY2(qFuzzyCompare(gridView->property("missionDurationSeconds").toDouble(), 90.0),
             "the plan takes the flying plus the waiting, and the calculator counts only the flying");
    QVERIFY(gridView->property("missionStatsKnown").toBool());

    QTRY_VERIFY_WITH_TIMEOUT(stats->property("visible").toBool(), TestTimeout::shortMs());

    // Opened by hand: it is folded by default, because a total is read while a pattern is being
    // built and not again while it is flown
    stats->setProperty("collapsed", false);

    QObject *const distanceLabel = gridItem->findChild<QObject *>(QStringLiteral("localGrid_missionStatsDistance"));
    QObject *const durationLabel = gridItem->findChild<QObject *>(QStringLiteral("localGrid_missionStatsDuration"));
    QVERIFY(distanceLabel && durationLabel);

    // Asked of the same transform the panel asks, so this holds whichever distance units the
    // application is set to rather than only the metric ones the numbers above are in
    QObject *const transform = gridView->property("gridTransform").value<QObject *>();
    QVERIFY(transform);
    QVariant display;
    QVERIFY(QMetaObject::invokeMethod(transform, "toDisplay", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, display), Q_ARG(QVariant, 80.0)));
    const QString expectedDistance = QStringLiteral("%1 %2")
                                         .arg(qRound(display.toDouble()))
                                         .arg(transform->property("displayUnits").toString());
    QCOMPARE(distanceLabel->property("text").toString(), expectedDistance);

    // Minutes and seconds, which is the form a plan is compared against a battery in
    QCOMPARE(durationLabel->property("text").toString(), QStringLiteral("1:30"));

    // And the panel says where the extra time came from, because it is the one line on it that QGC
    // did not produce
    QObject *const holdNote = gridItem->findChild<QObject *>(QStringLiteral("localGrid_missionStatsHoldNote"));
    QVERIFY(holdNote);
    QVERIFY(holdNote->property("visible").toBool());
    QVERIFY(holdNote->property("text").toString().contains(QStringLiteral("10")));
}

/// Without GNSS the compass is the only absolute reference the aircraft has, and which way the nose
/// points while a leg is flown is part of what the flow sensor measures. A waypoint cannot say it --
/// the 15-byte record drops param4 the same way it drops the radius -- so the heading is an item of
/// its own, and an item that carries no position must not be drawn on the grid or measured into the
/// leg beside it.
void LocalGridViewTest::_yawItemCarriesAHeadingAndNoLeg_test()
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

    QCOMPARE(gridView->property("commandConditionYaw").toInt(), static_cast<int>(MAV_CMD_CONDITION_YAW));

    QVariant added;
    for (const double east : { 0.0, 20.0 }) {
        QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                          Q_RETURN_ARG(QVariant, added),
                                          Q_ARG(QVariant, 20.0), Q_ARG(QVariant, east)));
        QVERIFY(added.toBool());
    }

    // Placed on the first of the two waypoints, so the yaw item lands between them rather than at
    // the end of the plan
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, 1)));

    QVariant inserted;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "insertConditionYaw", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, inserted), Q_ARG(QVariant, 270.0)));
    QVERIFY(inserted.toBool());

    QVariantList items = stub->property("items").toList();
    QCOMPARE(items.count(), 4);
    QObject *const yawItem = items.at(2).value<QObject *>();
    QVERIFY2(yawItem, "the yaw item belongs right after the waypoint it was placed on");
    QCOMPARE(yawItem->property("command").toInt(), static_cast<int>(MAV_CMD_CONDITION_YAW));

    // Listed, because it is an item the aircraft will fly, and nowhere on the grid, because it has
    // no position to be drawn at
    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 4);
    const QJSValue yawPoint = points.property(2);
    QCOMPARE(yawPoint.property(QStringLiteral("index")).toInt(), 2);
    QVERIFY2(!yawPoint.property(QStringLiteral("onGrid")).toBool(),
             "an item with no coordinate has nowhere on the grid to be drawn");
    QVERIFY2(!yawPoint.property(QStringLiteral("flyThrough")).toBool(),
             "the aircraft is not routed through a heading");

    // The leg reaching the last waypoint is still measured from the waypoint before the yaw item,
    // not from the yaw item -- the same path a cancelled ROI takes through this view
    QVariant legStart;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "legStartFor", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, legStart), Q_ARG(QVariant, 3)));
    const QVariantMap legStartMap = legStart.toMap();
    QVERIFY(qAbs(legStartMap.value(QStringLiteral("north")).toDouble() - 20.0) < 0.05);
    QVERIFY(qAbs(legStartMap.value(QStringLiteral("east")).toDouble()) < 0.05);

    // The heading itself, once the command tree publishes the parameter for the command the item was
    // just given -- which is what the real controller does as the command is written, and what the
    // stand-in leaves to the test so that the name comes from the tree rather than from the stub
    Fact *const heading = publishItemFact(yawItem, "textFieldFacts", QStringLiteral("Heading"));
    QVERIFY(heading);

    QVariant applied;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "setWaypointYawHeading", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, applied),
                                      Q_ARG(QVariant, 2), Q_ARG(QVariant, 270.0)));
    QVERIFY(applied.toBool());

    QVariant read;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "waypointYawHeading", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, read), Q_ARG(QVariant, 2)));
    QVERIFY2(qAbs(read.toDouble() - 270.0) < 1e-9,
             "the grid speaks the aircraft's convention: 270 is west, not an angle out of range");

    // Past the wrap is an ordinary thing to type while rotating a pattern, and 370 means 10
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "setWaypointYawHeading", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, applied),
                                      Q_ARG(QVariant, 2), Q_ARG(QVariant, 370.0)));
    QVERIFY(applied.toBool());
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "waypointYawHeading", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, read), Q_ARG(QVariant, 2)));
    QVERIFY(qAbs(read.toDouble() - 10.0) < 1e-9);

    // And a plain waypoint has no heading of its own to read or write, whatever it publishes
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "waypointYawHeading", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, read), Q_ARG(QVariant, 1)));
    QVERIFY(qIsNaN(read.toDouble()));
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "setWaypointYawHeading", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, applied),
                                      Q_ARG(QVariant, 1), Q_ARG(QVariant, 90.0)));
    QVERIFY2(!applied.toBool(), "a waypoint's own yaw never reaches the aircraft, so it is not offered one");
}

/// A pattern flown indoors is square to the walls or it is not, and the angle that makes it square
/// is one number rather than a new position for every waypoint. It turns about the point the pattern
/// starts from -- not about the origin, which for a pattern already moved to the aircraft is a point
/// the pattern no longer has anything to do with.
void LocalGridViewTest::_rotatePlan_turnsThePatternAboutWhereItStarts_test()
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
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "resetPlanAnchor", Qt::DirectConnection));

    QVariant added;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

    // A quarter turn clockwise takes a point due north of the pivot to due east of it
    QVariant turned;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "rotatePlan", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, turned), Q_ARG(QVariant, 90.0)));
    QCOMPARE(turned.toInt(), 1);

    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 2);
    QVERIFY(qAbs(points.property(1).property(QStringLiteral("north")).toNumber()) < 0.05);
    QVERIFY(qAbs(points.property(1).property(QStringLiteral("east")).toNumber() - 20.0) < 0.05);

    // The takeoff does not turn with it. It is pinned to the origin because a multirotor climbs in
    // place whatever coordinate is uploaded with it.
    QVERIFY(qAbs(points.property(0).property(QStringLiteral("north")).toNumber()) < 0.05);
    QVERIFY(qAbs(points.property(0).property(QStringLiteral("east")).toNumber()) < 0.05);

    // Moved ten metres north on purpose, which is what makes the pivot visible: the pattern now
    // starts somewhere other than the origin, and the anchor is what says so
    QVariant moved;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "nudgePlan", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved),
                                      Q_ARG(QVariant, 10.0), Q_ARG(QVariant, 0.0)));
    QCOMPARE(moved.toInt(), 1);
    QCOMPARE(gridView->property("planAnchorNorth").toDouble(), 10.0);

    // The waypoint is now at (10, 20), due east of the anchor at (10, 0). Another quarter turn
    // clockwise puts it due south of that anchor -- at (-10, 0).
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "rotatePlan", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, turned), Q_ARG(QVariant, 90.0)));
    QCOMPARE(turned.toInt(), 1);

    points = gridView->property("missionPoints").value<QJSValue>();
    const double north = points.property(1).property(QStringLiteral("north")).toNumber();
    const double east = points.property(1).property(QStringLiteral("east")).toNumber();
    QVERIFY2(qAbs(north + 10.0) < 0.05,
             qPrintable(QStringLiteral("turned about the origin instead of the anchor: north %1").arg(north)));
    QVERIFY2(qAbs(east) < 0.05,
             qPrintable(QStringLiteral("turned about the origin instead of the anchor: east %1").arg(east)));

    // And turning does not move the anchor: the pattern still starts where it started
    QCOMPARE(gridView->property("planAnchorNorth").toDouble(), 10.0);

    // A turn of a whole revolution changes nothing rather than rewriting every item to where it
    // already was
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "rotatePlan", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, turned), Q_ARG(QVariant, 360.0)));
    QCOMPARE(turned.toInt(), 0);
}

/// A yaw item has no position to turn -- what turns is the heading it holds. Left behind, the pattern
/// comes out at the right angle with the nose pointing the old way, and on an aircraft measuring with
/// optical flow that is not a cosmetic difference.
void LocalGridViewTest::_rotatePlan_turnsYawHeadingsWithThePattern_test()
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
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "resetPlanAnchor", Qt::DirectConnection));

    QVariant added;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

    QVariant inserted;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "insertConditionYaw", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, inserted), Q_ARG(QVariant, 0.0)));
    QVERIFY(inserted.toBool());

    const QVariantList items = stub->property("items").toList();
    QCOMPARE(items.count(), 3);
    Fact *const heading = publishItemFact(items.at(2).value<QObject *>(), "textFieldFacts",
                                          QStringLiteral("Heading"));
    QVERIFY(heading);

    // Set past three quarters, so the turn below has to wrap rather than run off the end of the compass
    QVariant applied;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "setWaypointYawHeading", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, applied),
                                      Q_ARG(QVariant, 2), Q_ARG(QVariant, 350.0)));
    QVERIFY(applied.toBool());

    QVariant turned;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "rotatePlan", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, turned), Q_ARG(QVariant, 90.0)));
    QVERIFY2(turned.toInt() == 2, "the waypoint turns and so does the heading beside it");

    QVariant read;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "waypointYawHeading", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, read), Q_ARG(QVariant, 2)));
    QVERIFY2(qAbs(read.toDouble() - 80.0) < 1e-9,
             "350 turned a quarter clockwise is 80, not 440");
}

/// The anchor is what says where the pattern starts, and a move made on purpose changes that as
/// surely as one made to follow the aircraft. Left behind, the grid goes on offering to move a
/// pattern to where it already claims it is.
void LocalGridViewTest::_nudgePlan_movesTheAnchorWithThePattern_test()
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
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "resetPlanAnchor", Qt::DirectConnection));

    QVariant added;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());

    // The aircraft is standing five metres up the field, so the offer to move the plan to it is five
    // metres -- the number the anchor is subtracted from
    sendLocalPosition(vehicle(), 5.0F, 0.0F, -1.0F);
    QTRY_VERIFY_WITH_TIMEOUT(gridView->property("positionValid").toBool(), TestTimeout::mediumMs());
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(gridView->property("reanchorNorthMetres").toDouble() - 5.0) < 0.05,
                             TestTimeout::mediumMs());

    QVariant moved;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "nudgePlan", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved),
                                      Q_ARG(QVariant, 2.0), Q_ARG(QVariant, 0.0)));
    QCOMPARE(moved.toInt(), 1);

    const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QVERIFY(qAbs(points.property(1).property(QStringLiteral("north")).toNumber() - 22.0) < 0.05);

    QCOMPARE(gridView->property("planAnchorNorth").toDouble(), 2.0);
    QVERIFY2(qAbs(gridView->property("reanchorNorthMetres").toDouble() - 3.0) < 0.05,
             "the pattern moved two metres further from the aircraft, so the offer to move it back must shrink by two");
}

/// Both of these rewrite the plan, and both are shut for the same two reasons the move to the
/// aircraft is: an aircraft already flying the pattern would have its route changed underneath it,
/// and a transfer in progress is sending the very items being rewritten. Shut with the reason said
/// out loud, because a dead button teaches an operator the feature is broken.
void LocalGridViewTest::_patternShapingIsRefusedWhileArmedOrSyncing_test()
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
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "resetPlanAnchor", Qt::DirectConnection));

    // Nothing drawn yet: there is no reason to give, because a grid with no pattern on it explains
    // itself
    QVERIFY(!gridView->property("canTransformPlan").toBool());
    QCOMPARE(gridView->property("transformBlockedReason").toString(), QString());

    QVariant added;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(added.toBool());
    QVERIFY(gridView->property("canTransformPlan").toBool());

    plan->setProperty("syncInProgress", true);
    QVERIFY(!gridView->property("canTransformPlan").toBool());
    QVERIFY(!gridView->property("transformBlockedReason").toString().isEmpty());

    QVariant turned;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "rotatePlan", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, turned), Q_ARG(QVariant, 90.0)));
    QCOMPARE(turned.toInt(), 0);
    plan->setProperty("syncInProgress", false);

    vehicle()->setArmedShowError(true);
    QTRY_VERIFY_WITH_TIMEOUT(vehicle()->armed(), TestTimeout::longMs());
    QTRY_COMPARE_WITH_TIMEOUT(gridView->property("canTransformPlan").toBool(), false, TestTimeout::mediumMs());
    QVERIFY(!gridView->property("transformBlockedReason").toString().isEmpty());

    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "rotatePlan", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, turned), Q_ARG(QVariant, 90.0)));
    QCOMPARE(turned.toInt(), 0);
    QVariant moved;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "nudgePlan", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved),
                                      Q_ARG(QVariant, 2.0), Q_ARG(QVariant, 0.0)));
    QCOMPARE(moved.toInt(), 0);

    // The pattern is where it was drawn, not somewhere between
    const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QVERIFY(qAbs(points.property(1).property(QStringLiteral("north")).toNumber() - 20.0) < 0.05);

    vehicle()->setArmedShowError(false);
    QTRY_VERIFY_WITH_TIMEOUT(!vehicle()->armed(), TestTimeout::longMs());
}

// ============================================================================
// Bagian 6b: undo (Lampiran H)
// ============================================================================

/// Placing is one tap with nothing guarding it, so it has to be takeable back -- and taking it back
/// has to leave the plan exactly as it was, not merely one item shorter.
///
/// The empty-plan case is the one that makes this more than a removal: the first waypoint of a plan
/// also brings a takeoff in with it, and an undo that removed only the waypoint would leave behind a
/// takeoff nobody asked for.
void LocalGridViewTest::_undoTakesBackAPlacement_test()
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

    QVERIFY2(!gridView->property("canUndo").toBool(), "nothing has happened yet, so there is nothing to undo");

    QVariant placed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, placed),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(placed.toBool());

    // The takeoff came along with it
    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 2);
    QVERIFY2(gridView->property("canUndo").toBool(), "a placement must be takeable back");

    QVariant undone;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "undoLastAction", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, undone)));
    QVERIFY(undone.toBool());

    points = gridView->property("missionPoints").value<QJSValue>();
    QVERIFY2(points.property(QStringLiteral("length")).toInt() == 0,
             "undoing the first placement must take the takeoff it brought with it as well");
    QVERIFY2(!gridView->property("canUndo").toBool(),
             "the control must go once its action has been taken back");
}

/// The accident 6a reduced but could not remove: a waypoint selected on a touch screen and nudged in
/// the process. Undo has to put it back exactly, not approximately.
void LocalGridViewTest::_undoPutsAMovedWaypointBack_test()
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

    QVariant placed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, placed),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 5.0)));
    QVERIFY(placed.toBool());

    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    const int waypointIndex = points.property(1).property(QStringLiteral("index")).toInt();

    QVariant moved;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "moveWaypointTo", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, moved),
                                      Q_ARG(QVariant, waypointIndex),
                                      Q_ARG(QVariant, 31.0), Q_ARG(QVariant, -9.0)));
    QVERIFY(moved.toBool());

    points = gridView->property("missionPoints").value<QJSValue>();
    QVERIFY(qAbs(points.property(1).property(QStringLiteral("north")).toNumber() - 31.0) < 0.05);

    QVariant undone;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "undoLastAction", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, undone)));
    QVERIFY(undone.toBool());

    points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 2);
    QVERIFY2(qAbs(points.property(1).property(QStringLiteral("north")).toNumber() - 20.0) < 0.05,
             "the waypoint must come back to the offsets it was moved from");
    QVERIFY(qAbs(points.property(1).property(QStringLiteral("east")).toNumber() - 5.0) < 0.05);
}

/// A delete is one tap on a trash icon with no confirmation behind it, and it takes the altitude,
/// the speed and the wait typed into the item along with the item itself. Undo has to bring all of
/// it back -- restoring the position alone would hand back something that looks right and flies
/// differently.
void LocalGridViewTest::_undoRestoresADeletedItemWholly_test()
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

    QVariant placed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, placed),
                                      Q_ARG(QVariant, 12.0), Q_ARG(QVariant, -4.0)));
    QVERIFY(placed.toBool());

    QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    const int waypointIndex = points.property(1).property(QStringLiteral("index")).toInt();

    // Given the fields an operator would have typed before losing it. Whole numbers because the
    // stub's Facts default to int32 -- the values matter, their precision does not.
    QObject *const item = stub->property("lastInsertedItem").value<QObject *>();
    QVERIFY(item);
    QObject *const altitude = item->property("altitude").value<QObject *>();
    QVERIFY(altitude);
    altitude->setProperty("rawValue", 6.0);
    Fact *const hold = publishItemFact(item, "textFieldFactsAdvanced", QStringLiteral("Hold"));
    QVERIFY(hold);
    hold->setRawValue(9.0);

    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "selectWaypoint", Qt::DirectConnection,
                                      Q_ARG(QVariant, waypointIndex)));
    QVariant removed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "removeSelectedWaypoint", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, removed)));
    QVERIFY(removed.toBool());
    QCOMPARE(gridView->property("missionPoints").value<QJSValue>().property(QStringLiteral("length")).toInt(), 1);
    QVERIFY2(gridView->property("canUndo").toBool(), "a delete with no confirmation behind it must be takeable back");

    QVariant undone;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "undoLastAction", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, undone)));
    QVERIFY(undone.toBool());

    points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 2);
    QVERIFY2(qAbs(points.property(1).property(QStringLiteral("north")).toNumber() - 12.0) < 0.05,
             "the restored item must come back where it was");
    QVERIFY(qAbs(points.property(1).property(QStringLiteral("east")).toNumber() + 4.0) < 0.05);

    const int restoredIndex = points.property(1).property(QStringLiteral("index")).toInt();
    QVariant altitudeFactValue;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "waypointAltitudeFact", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, altitudeFactValue),
                                      Q_ARG(QVariant, restoredIndex)));
    QObject *const restoredAltitude = altitudeFactValue.value<QObject *>();
    QVERIFY2(restoredAltitude, "the restored item carries no altitude at all");
    QVERIFY2(qFuzzyCompare(restoredAltitude->property("rawValue").toDouble(), 6.0),
             "the altitude typed into the item must come back with it");
}

/// Turning the whole pattern is one tap that moves every item. Undo turns it back through the same
/// angle about the same anchor, which is the inverse rather than a stored copy -- so the check is
/// that every offset lands back where it started.
void LocalGridViewTest::_undoTurnsThePatternBack_test()
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

    QVariant placed;
    const double north[] = {20.0, 20.0};
    const double east[]  = {0.0, 20.0};
    for (int i = 0; i < 2; i++) {
        QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                          Q_RETURN_ARG(QVariant, placed),
                                          Q_ARG(QVariant, north[i]), Q_ARG(QVariant, east[i])));
        QVERIFY(placed.toBool());
    }

    QVariant turned;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "rotatePlan", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, turned), Q_ARG(QVariant, 90.0)));
    QVERIFY2(turned.toInt() > 0, "the pattern never turned");
    QVERIFY2(gridView->property("canUndo").toBool(), "turning the plan must be takeable back");

    QVariant undone;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "undoLastAction", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, undone)));
    QVERIFY(undone.toBool());

    const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 3);
    for (int i = 0; i < 2; i++) {
        const QJSValue point = points.property(i + 1);
        QVERIFY2(qAbs(point.property(QStringLiteral("north")).toNumber() - north[i]) < 0.05,
                 "every item must come back to the offsets the turn moved it from");
        QVERIFY(qAbs(point.property(QStringLiteral("east")).toNumber() - east[i]) < 0.05);
    }
}

/// "Set this altitude on all" is the widest single tap on the grid: one number replaces every
/// altitude in the plan, and none of the old ones are readable from anything left on screen.
void LocalGridViewTest::_undoRestoresTheAltitudesOneTapReplaced_test()
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

    QVariant placed;
    for (int i = 0; i < 2; i++) {
        QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                          Q_RETURN_ARG(QVariant, placed),
                                          Q_ARG(QVariant, 10.0 * (i + 1)), Q_ARG(QVariant, 0.0)));
        QVERIFY(placed.toBool());
    }

    // Two different altitudes, so a restore that simply writes one number everywhere would show up
    const QJSValue before = gridView->property("missionPoints").value<QJSValue>();
    const int firstIndex  = before.property(1).property(QStringLiteral("index")).toInt();
    const int secondIndex = before.property(2).property(QStringLiteral("index")).toInt();

    const auto altitudeOf = [&gridView](int index) -> QObject * {
        QVariant value;
        if (!QMetaObject::invokeMethod(gridView.get(), "waypointAltitudeFact", Qt::DirectConnection,
                                       Q_RETURN_ARG(QVariant, value), Q_ARG(QVariant, index))) {
            return nullptr;
        }
        return value.value<QObject *>();
    };

    QObject *const firstAltitude = altitudeOf(firstIndex);
    QObject *const secondAltitude = altitudeOf(secondIndex);
    QVERIFY(firstAltitude);
    QVERIFY(secondAltitude);
    firstAltitude->setProperty("rawValue", 4.0);
    secondAltitude->setProperty("rawValue", 7.0);

    QVariant changed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "setAllWaypointAltitudes", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, changed), Q_ARG(QVariant, 9.0)));
    QVERIFY(changed.toInt() > 0);
    QCOMPARE(altitudeOf(firstIndex)->property("rawValue").toDouble(), 9.0);

    QVariant undone;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "undoLastAction", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, undone)));
    QVERIFY(undone.toBool());

    QVERIFY2(qFuzzyCompare(altitudeOf(firstIndex)->property("rawValue").toDouble(), 4.0),
             "each item must get its own altitude back, not a single shared one");
    QVERIFY2(qFuzzyCompare(altitudeOf(secondIndex)->property("rawValue").toDouble(), 7.0),
             "each item must get its own altitude back, not a single shared one");
}

/// The fly view's controller is a mirror of the vehicle: a completed transfer rebuilds every item.
/// A recorded action describes the plan that was there before that, so applying its inverse
/// afterwards would edit an item it was never about.
void LocalGridViewTest::_undoIsDroppedWhenThePlanArrivesFromTheVehicle_test()
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

    QVariant placed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, placed),
                                      Q_ARG(QVariant, 10.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(placed.toBool());
    QVERIFY(gridView->property("canUndo").toBool());

    QVERIFY(QMetaObject::invokeMethod(stub.get(), "visualItemsReset", Qt::DirectConnection));

    QVERIFY2(!gridView->property("canUndo").toBool(),
             "an action describing the previous plan must not survive the arrival of a new one");

    QVariant undone;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "undoLastAction", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, undone)));
    QVERIFY2(!undone.toBool(), "there must be nothing left to take back");
}

/// One level, deliberately. Two actions then one undo takes back the second and offers nothing
/// further -- a stack of claims about a plan the vehicle may already have replaced is what this
/// avoids.
void LocalGridViewTest::_undoRemembersOnlyTheLastAction_test()
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

    QVariant placed;
    for (int i = 0; i < 2; i++) {
        QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                          Q_RETURN_ARG(QVariant, placed),
                                          Q_ARG(QVariant, 10.0 * (i + 1)), Q_ARG(QVariant, 0.0)));
        QVERIFY(placed.toBool());
    }
    // takeoff plus two waypoints
    QCOMPARE(gridView->property("missionPoints").value<QJSValue>().property(QStringLiteral("length")).toInt(), 3);

    QVariant undone;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "undoLastAction", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, undone)));
    QVERIFY(undone.toBool());

    // Only the second placement went; the first waypoint and its takeoff stay
    QCOMPARE(gridView->property("missionPoints").value<QJSValue>().property(QStringLiteral("length")).toInt(), 2);
    QVERIFY2(!gridView->property("canUndo").toBool(),
             "one level means the first placement is not offered up after the second is taken back");
}

/// The bug an operator met first: build a plan, and every waypoint after the first became waypoint
/// one.
///
/// setCurrentPlanViewSeqNum finds the item whose *first* sequence number matches what it is handed.
/// The fly view handed it lastSequenceNumber instead -- in MissionController's own initialisation
/// and in the grid's clearWaypointSelection, which was written to mirror it. The two are equal only
/// for an item that occupies one place in the uploaded mission, and every waypoint this grid places
/// carries a speed, so it is flown as NAV_WAYPOINT followed by DO_CHANGE_SPEED and occupies two.
///
/// Nothing matched, the controller was left with no current item, currentPlanViewVIIndex stayed at
/// -1, and _insertIndex turned that into 0 -- the mission settings item's own slot, in front of the
/// whole plan.
///
/// This went unseen because the stand-in was kinder than the real thing: its items default to
/// lastSequenceNumber == sequenceNumber, so the lookup always succeeded. The span is set explicitly
/// here, which is the shape a real speed-carrying waypoint has.
void LocalGridViewTest::_insertAppendsWhenAnItemSpansTwoSequenceNumbers_test()
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

    QVariant placed;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, placed),
                                      Q_ARG(QVariant, 10.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(placed.toBool());

    // The waypoint just placed carries a speed, so it spans two numbers the way the real one does
    QObject *const firstWaypoint = stub->property("lastInsertedItem").value<QObject *>();
    QVERIFY(firstWaypoint);
    firstWaypoint->setProperty("lastSequenceNumber",
                               firstWaypoint->property("sequenceNumber").toInt() + 1);

    // Clicking bare grid deselects, which is what puts the controller back at the end of the plan --
    // the path that was handing over the wrong number
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "clearWaypointSelection", Qt::DirectConnection));

    QVariant insertIndex;
    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "_insertIndex", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, insertIndex)));
    QVERIFY2(insertIndex.toInt() != 0,
             qPrintable(QStringLiteral("an insert must never land on the mission settings item's own "
                                       "slot; got index %1").arg(insertIndex.toInt())));

    QVERIFY(QMetaObject::invokeMethod(gridView.get(), "addWaypointAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, placed),
                                      Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(placed.toBool());

    // takeoff, then 10 north, then 20 north -- in the order they were placed
    const QJSValue points = gridView->property("missionPoints").value<QJSValue>();
    QCOMPARE(points.property(QStringLiteral("length")).toInt(), 3);
    QVERIFY2(qAbs(points.property(1).property(QStringLiteral("north")).toNumber() - 10.0) < 0.05,
             "the first waypoint must stay first");
    QVERIFY2(qAbs(points.property(2).property(QStringLiteral("north")).toNumber() - 20.0) < 0.05,
             "the second waypoint must land after it, not in front of the whole plan");
}
