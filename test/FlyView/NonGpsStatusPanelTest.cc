#include "NonGpsStatusPanelTest.h"

#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtTest/QTest>

#include "FactGroup.h"
#include "Vehicle.h"

namespace {

struct PanelFact_s {
    const char *factGroupName;
    const char *factName;
};

/// Every value NonGpsStatusPanel.qml displays, in the same order as the panel
constexpr PanelFact_s kPanelFacts[] = {
    { "opticalFlow", "quality" },
    { "opticalFlow", "flowCompMagnitude" },
    { "opticalFlow", "flowCompX" },
    { "opticalFlow", "flowCompY" },
    { "opticalFlow", "groundDistance" },
    { "distanceSensor", "rotationPitch270" },
    { "estimatorStatus", "magRatio" },
    { "estimatorStatus", "goodHorizPosRelEstimate" },
    { "estimatorStatus", "goodHorizVelEstimate" },
    { "estimatorStatus", "goodConstPosModeEstimate" },
    { "estimatorStatus", "velRatio" },
    { "estimatorStatus", "horizPosRatio" },
    { "estimatorStatus", "haglRatio" },
    { "localPosition", "x" },
    { "localPosition", "y" },
    { "localPosition", "z" },
    { "localPosition", "vx" },
    { "localPosition", "vy" },
    { "localPosition", "vz" },
    { "vibration", "xAxis" },
    { "vibration", "yAxis" },
    { "vibration", "zAxis" },
};

/// Sends OPTICAL_FLOW the way the vehicle does, with the quality figure and the height that message
/// carries in the same frame
void sendOpticalFlow(Vehicle *vehicle, int quality, float groundDistance)
{
    mavlink_optical_flow_t opticalFlow{};
    opticalFlow.quality = static_cast<uint8_t>(quality);
    opticalFlow.ground_distance = groundDistance;

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

/// Every flag a healthy solution sets, so a test that means to lose aiding loses only aiding
constexpr uint16_t kHealthyEkfFlags = EKF_ATTITUDE | EKF_VELOCITY_HORIZ | EKF_VELOCITY_VERT
                                      | EKF_POS_HORIZ_REL | EKF_POS_HORIZ_ABS | EKF_POS_VERT_ABS;

/// Builds the object the panel gets its explanation from, bound to @a vehicle.
/// @return the created object, or nullptr with @a error describing why not
QObject *createAidingReason(QQmlComponent &component, Vehicle *vehicle, QString &error)
{
    component.setData(R"(
        import QtQuick
        import QGroundControl.FlyView

        NonGpsAidingReason { }
    )", QUrl());
    if (!component.isReady()) {
        error = component.errorString();
        return nullptr;
    }

    QObject *const aidingReason = component.create();
    if (!aidingReason) {
        error = component.errorString();
        return nullptr;
    }

    aidingReason->setProperty("vehicle", QVariant::fromValue(vehicle));
    return aidingReason;
}

} // namespace

#define MAKE_AIDING_REASON(name)                                                    \
    QQmlEngine name##Engine;                                                        \
    name##Engine.addImportPath(QStringLiteral("qrc:/qml"));                         \
    QQmlComponent name##Component(&name##Engine);                                   \
    QString name##Error;                                                            \
    const QScopedPointer<QObject> name(                                             \
        createAidingReason(name##Component, vehicle(), name##Error));               \
    QVERIFY2(name, qPrintable(name##Error))

void NonGpsStatusPanelTest::_panelFactsExist_test()
{
    QVERIFY(vehicle());

    for (const PanelFact_s &panelFact : kPanelFacts) {
        FactGroup *const factGroup = vehicle()->getFactGroup(QString::fromLatin1(panelFact.factGroupName));
        QVERIFY2(factGroup, panelFact.factGroupName);
        QVERIFY2(factGroup->factExists(QString::fromLatin1(panelFact.factName)),
                 qPrintable(QStringLiteral("%1.%2 is displayed by the panel but does not exist")
                                .arg(QLatin1String(panelFact.factGroupName), QLatin1String(panelFact.factName))));
    }
}

/// The compass rows read off the vehicle itself rather than a named fact group, so they would slip
/// past the sweep above.
void NonGpsStatusPanelTest::_panelVehicleValuesExist_test()
{
    QVERIFY(vehicle());

    QVERIFY2(vehicle()->factExists(QStringLiteral("heading")), "the panel displays vehicle.heading");

    // Read together to tell "no magnetometer" from "magnetometer reporting a fault"
    const QMetaObject *const metaObject = vehicle()->metaObject();
    QVERIFY(metaObject->indexOfProperty("sensorsPresentBits") >= 0);
    QVERIFY(metaObject->indexOfProperty("sensorsUnhealthyBits") >= 0);
}

/// Builds the panel for real against the connected vehicle, and checks it laid out rows. This is
/// narrower than it looks: QML answers a missing property on a QObject or an enum namespace with
/// undefined and no warning, so this catches load failures and undeclared identifiers, not renamed
/// facts. The name sweeps above are what guard those.
void NonGpsStatusPanelTest::_panelBuildsWithoutBindingErrors_test()
{
    QVERIFY(vehicle());

    // Laying out real labels makes the headless runner resolve fonts the first time
    ignoreLogMessage("qt.qpa.fonts", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Populating font family aliases")));

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QGroundControl.FlyView

        NonGpsStatusPanel { }
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    const QScopedPointer<QObject> panel(component.create());
    QVERIFY2(panel, qPrintable(component.errorString()));

    // A panel that collapsed to nothing has no rows, which no amount of quiet logging would show
    QVERIFY(panel->property("implicitHeight").toReal() > 0);
    QVERIFY(panel->property("implicitWidth").toReal() > 0);
}

/// The compass row is coloured against a threshold, and zero would colour green. It reads as green
/// only because the fact starts at NaN, so an unflown vehicle shows "--" rather than a flawless
/// compass -- a default of 0 here would be indistinguishable from a perfect one.
void NonGpsStatusPanelTest::_magRatioStartsUnknownRatherThanZero_test()
{
    QVERIFY(vehicle());

    FactGroup *const estimatorStatus = vehicle()->getFactGroup(QStringLiteral("estimatorStatus"));
    QVERIFY(estimatorStatus);
    QVERIFY2(qIsNaN(estimatorStatus->getFact(QStringLiteral("magRatio"))->rawValue().toDouble()),
             "an unpopulated mag ratio must not read as a healthy 0");

    mavlink_ekf_status_report_t ekfStatus{};
    ekfStatus.compass_variance = 0.25F;
    mavlink_message_t message{};
    (void) mavlink_msg_ekf_status_report_encode(vehicle()->id(), MAV_COMP_ID_AUTOPILOT1, &message, &ekfStatus);
    estimatorStatus->handleMessage(vehicle(), message);

    QCOMPARE(estimatorStatus->getFact(QStringLiteral("magRatio"))->rawValue().toFloat(), 0.25F);
}

/// "Aiding: NO" is not a state to explain until the estimator has actually claimed it. Explaining it
/// from an unset flag would put a red sentence under every vehicle that has not reported yet.
void NonGpsStatusPanelTest::_aidingReasonSilentUntilAidingIsLost_test()
{
    QVERIFY(vehicle());
    MAKE_AIDING_REASON(aidingReason);

    QVERIFY2(!aidingReason->property("aidingLost").toBool(),
             "an estimator that has said nothing has not reported lost aiding");
    QCOMPARE(aidingReason->property("reason").toString(), QString());

    sendEkfStatus(vehicle(), kHealthyEkfFlags, 0.2F, 0.2F);
    QVERIFY(!aidingReason->property("aidingLost").toBool());
    QCOMPARE(aidingReason->property("reason").toString(), QString());

    sendEkfStatus(vehicle(), kHealthyEkfFlags | EKF_CONST_POS_MODE, 0.2F, 0.2F);
    QVERIFY(aidingReason->property("aidingLost").toBool());
    QVERIFY(!aidingReason->property("reason").toString().isEmpty());

    // And it clears again, rather than latching the way telemetryAvailable does
    sendEkfStatus(vehicle(), kHealthyEkfFlags, 0.2F, 0.2F);
    QVERIFY(!aidingReason->property("aidingLost").toBool());
    QCOMPARE(aidingReason->property("reason").toString(), QString());
}

/// Flow that is not arriving at all explains the lost aiding on its own, and is the one cause the
/// operator can rule in without reading any other row.
void NonGpsStatusPanelTest::_aidingLostWithoutFlow_namesTheMissingFlow_test()
{
    QVERIFY(vehicle());
    MAKE_AIDING_REASON(aidingReason);

    sendEkfStatus(vehicle(), kHealthyEkfFlags | EKF_CONST_POS_MODE, 0.2F, 0.2F);

    const QString reason = aidingReason->property("reason").toString();
    QVERIFY2(reason.contains(QStringLiteral("optical flow telemetry"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("no flow at all must be named as the cause, got: %1").arg(reason)));
}

/// Flow arriving with a quality the aircraft cannot navigate on is the next thing to rule out, and
/// naming the number saves the operator comparing two rows against a threshold they have to know.
void NonGpsStatusPanelTest::_aidingLostWithPoorFlowQuality_namesTheQuality_test()
{
    QVERIFY(vehicle());
    MAKE_AIDING_REASON(aidingReason);

    sendOpticalFlow(vehicle(), 12 /* quality */, 3.0F /* ground distance */);
    sendEkfStatus(vehicle(), kHealthyEkfFlags | EKF_CONST_POS_MODE, 0.2F, 0.2F);

    const QString reason = aidingReason->property("reason").toString();
    QVERIFY2(reason.contains(QStringLiteral("quality"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("a failing quality figure must be named, got: %1").arg(reason)));
    QVERIFY2(reason.contains(QStringLiteral("12")),
             qPrintable(QStringLiteral("the reading itself must be quoted, got: %1").arg(reason)));
}

/// Flow measures an angular rate. Without a height it cannot become a velocity however clean it
/// looks, so a good quality figure with no height still explains the lost aiding -- and a
/// rangefinder streaming zeros is not a height.
void NonGpsStatusPanelTest::_aidingLostWithoutHeight_namesTheMissingHeight_test()
{
    QVERIFY(vehicle());
    MAKE_AIDING_REASON(aidingReason);

    // Negative ground distance is how OPTICAL_FLOW says it has no height, and zero is what a
    // rangefinder that got no return reports
    sendOpticalFlow(vehicle(), 90 /* quality */, -1.0F /* ground distance */);
    sendDownwardRangefinder(vehicle(), 0.0);
    sendEkfStatus(vehicle(), kHealthyEkfFlags | EKF_CONST_POS_MODE, 0.2F, 0.2F);

    const QString reason = aidingReason->property("reason").toString();
    QVERIFY2(reason.contains(QStringLiteral("nothing to scale it into a velocity")),
             qPrintable(QStringLiteral("the missing height must be named, got: %1").arg(reason)));

    // A real reading moves the explanation on to the estimator's own ratios
    sendDownwardRangefinder(vehicle(), 4.0);
    sendEkfStatus(vehicle(), kHealthyEkfFlags | EKF_CONST_POS_MODE, 1.6F, 0.2F);
    const QString afterReading = aidingReason->property("reason").toString();
    QVERIFY2(!afterReading.contains(QStringLiteral("nothing to scale it into a velocity")),
             qPrintable(QStringLiteral("a rangefinder that is reporting must stop the height being blamed, got: %1").arg(afterReading)));
    QVERIFY2(afterReading.contains(QStringLiteral("rejecting"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("with a height present the ratio is what is left to explain it, got: %1").arg(afterReading)));
}

/// With flow, a height and healthy ratios all present, the sensors are not the answer. Saying so is
/// more use than picking the least unlikely one to blame.
void NonGpsStatusPanelTest::_aidingLostWithHealthyInputs_saysTheCauseIsElsewhere_test()
{
    QVERIFY(vehicle());
    MAKE_AIDING_REASON(aidingReason);

    sendOpticalFlow(vehicle(), 90 /* quality */, 3.0F /* ground distance */);
    sendDownwardRangefinder(vehicle(), 3.0);
    sendEkfStatus(vehicle(), kHealthyEkfFlags | EKF_CONST_POS_MODE, 0.2F, 0.2F);

    const QString reason = aidingReason->property("reason").toString();
    QVERIFY(!reason.isEmpty());
    QVERIFY2(reason.contains(QStringLiteral("EK3_SRC1_VELXY")),
             qPrintable(QStringLiteral("with every input healthy the source parameter is what is left to check, got: %1").arg(reason)));

    // Rejection is the estimator's own verdict and outranks the fallback
    sendEkfStatus(vehicle(), kHealthyEkfFlags | EKF_CONST_POS_MODE, 0.2F, 1.7F);
    const QString rejecting = aidingReason->property("reason").toString();
    QVERIFY2(rejecting.contains(QStringLiteral("rejecting"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("a ratio past the gate must be reported as rejection, got: %1").arg(rejecting)));
    QVERIFY2(rejecting.contains(QStringLiteral("1.70")),
             qPrintable(QStringLiteral("the ratio itself must be quoted, got: %1").arg(rejecting)));
}

/// A rangefinder reporting zero is one that got no return, which the pre-flight check and the
/// waypoint ceiling both already refuse to read as a height. The panel row was rendering it in the
/// ordinary colour, where "0.00 m" is indistinguishable from an aircraft on the ground.
void NonGpsStatusPanelTest::_rangefinderZeroIsNotColouredAsAReading_test()
{
    QVERIFY(vehicle());

    ignoreLogMessage("qt.qpa.fonts", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Populating font family aliases")));

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QGroundControl.FlyView

        NonGpsStatusPanel { }
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    const QScopedPointer<QObject> panel(component.create());
    QVERIFY2(panel, qPrintable(component.errorString()));

    const auto colourFor = [&panel](double distance) {
        QVariant colour;
        const bool invoked = QMetaObject::invokeMethod(panel.get(), "_rangefinderColor", Qt::DirectConnection,
                                                       Q_RETURN_ARG(QVariant, colour), Q_ARG(QVariant, distance));
        return invoked ? colour : QVariant();
    };

    const QVariant unusable = colourFor(0.0);
    const QVariant reading = colourFor(3.5);
    const QVariant unreported = colourFor(qQNaN());

    QVERIFY(unusable.isValid() && reading.isValid() && unreported.isValid());
    QVERIFY2(unusable != reading, "a zero reading must not be coloured like a real height");
    QVERIFY2(unreported != unusable, "nothing received must not be coloured like a sensor reporting a fault");
    QCOMPARE(colourFor(-1.0), unusable);
}

UT_REGISTER_TEST(NonGpsStatusPanelTest, TestLabel::Integration, TestLabel::Vehicle)
