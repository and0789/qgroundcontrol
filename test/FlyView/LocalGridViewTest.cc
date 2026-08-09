#include "LocalGridViewTest.h"

#include <QtCore/QtNumeric>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtTest/QTest>

#include "FactGroup.h"
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

UT_REGISTER_TEST(LocalGridViewTest, TestLabel::Integration, TestLabel::Vehicle)
