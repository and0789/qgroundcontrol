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

} // namespace

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
