#include "LocalGridProjectionTest.h"

#include <QtPositioning/QGeoCoordinate>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtTest/QTest>

namespace {

/// Arbitrary but real origin, away from the equator so a longitude degree is clearly not a latitude
/// degree and a north/east swap cannot pass unnoticed
const QGeoCoordinate kOrigin(47.3977419, 8.5455938, 488.0);

QObject *createProjection(QQmlComponent &component, QString &error)
{
    component.setData(R"(
        import QGroundControl.FlyView

        LocalGridProjection { }
    )", QUrl());
    if (!component.isReady()) {
        error = component.errorString();
        return nullptr;
    }

    QObject *const projection = component.create();
    if (!projection) {
        error = component.errorString();
    }
    return projection;
}

/// @return false when the projection declined to convert
bool northEastFrom(QObject *projection, const QGeoCoordinate &origin, const QGeoCoordinate &coordinate,
                   double &north, double &east)
{
    QVariant result;
    const bool invoked = QMetaObject::invokeMethod(projection, "northEastFrom", Qt::DirectConnection,
                                                   Q_RETURN_ARG(QVariant, result),
                                                   Q_ARG(QVariant, QVariant::fromValue(origin)),
                                                   Q_ARG(QVariant, QVariant::fromValue(coordinate)));
    if (!invoked || result.isNull() || !result.isValid()) {
        return false;
    }

    const QVariantMap offsets = result.toMap();
    if (offsets.isEmpty()) {
        return false;
    }
    north = offsets.value(QStringLiteral("north")).toDouble();
    east = offsets.value(QStringLiteral("east")).toDouble();
    return true;
}

QGeoCoordinate coordinateAt(QObject *projection, const QGeoCoordinate &origin, double north, double east)
{
    QVariant result;
    const bool invoked = QMetaObject::invokeMethod(projection, "coordinateAt", Qt::DirectConnection,
                                                   Q_RETURN_ARG(QVariant, result),
                                                   Q_ARG(QVariant, QVariant::fromValue(origin)),
                                                   Q_ARG(QVariant, north), Q_ARG(QVariant, east));
    return invoked ? result.value<QGeoCoordinate>() : QGeoCoordinate();
}

} // namespace

#define MAKE_PROJECTION(name)                                     \
    QQmlEngine name##Engine;                                      \
    name##Engine.addImportPath(QStringLiteral("qrc:/qml"));       \
    QQmlComponent name##Component(&name##Engine);                 \
    QString name##Error;                                          \
    const QScopedPointer<QObject> name(                           \
        createProjection(name##Component, name##Error));          \
    QVERIFY2(name, qPrintable(name##Error))

/// Due north must be north with no east, and due east must be east with no north. A swap here draws
/// the whole plan rotated a quarter turn, which still looks like a plan.
void LocalGridProjectionTest::_northAndEastMatchTheirBearings_test()
{
    MAKE_PROJECTION(projection);

    struct Case_s {
        double bearing;
        double expectedNorth;
        double expectedEast;
    };
    const Case_s cases[] = {
        {   0.0,  100.0,    0.0 },
        {  90.0,    0.0,  100.0 },
        { 180.0, -100.0,    0.0 },
        { 270.0,    0.0, -100.0 },
    };

    for (const Case_s &testCase : cases) {
        const QGeoCoordinate target = kOrigin.atDistanceAndAzimuth(100.0, testCase.bearing);

        double north = 0.0;
        double east = 0.0;
        QVERIFY(northEastFrom(projection.get(), kOrigin, target, north, east));

        QVERIFY2(qAbs(north - testCase.expectedNorth) < 0.05,
                 qPrintable(QStringLiteral("bearing %1 gave north %2").arg(testCase.bearing).arg(north)));
        QVERIFY2(qAbs(east - testCase.expectedEast) < 0.05,
                 qPrintable(QStringLiteral("bearing %1 gave east %2").arg(testCase.bearing).arg(east)));
    }
}

/// Clicking the grid converts metres to a coordinate; drawing the plan converts it back. The two
/// have to agree, or a waypoint jumps the moment it is drawn.
void LocalGridProjectionTest::_offsetsAndCoordinatesRoundTrip_test()
{
    MAKE_PROJECTION(projection);

    const QPointF offsets[] = {
        QPointF(0.0, 0.0), QPointF(20.0, 20.0), QPointF(-35.5, 12.25), QPointF(400.0, -750.0),
    };

    for (const QPointF &offset : offsets) {
        const QGeoCoordinate coordinate = coordinateAt(projection.get(), kOrigin, offset.x(), offset.y());
        QVERIFY(coordinate.isValid());

        double north = 0.0;
        double east = 0.0;
        QVERIFY(northEastFrom(projection.get(), kOrigin, coordinate, north, east));

        QVERIFY2(qAbs(north - offset.x()) < 0.01,
                 qPrintable(QStringLiteral("north %1 came back as %2").arg(offset.x()).arg(north)));
        QVERIFY2(qAbs(east - offset.y()) < 0.01,
                 qPrintable(QStringLiteral("east %1 came back as %2").arg(offset.y()).arg(east)));
    }
}

/// A vehicle without an estimator origin has no anchor for this frame. Inventing one would place
/// waypoints against a fiction, so both directions must decline instead.
void LocalGridProjectionTest::_invalidOriginYieldsNothing_test()
{
    MAKE_PROJECTION(projection);

    double north = 0.0;
    double east = 0.0;
    QVERIFY(!northEastFrom(projection.get(), QGeoCoordinate(), kOrigin, north, east));
    QVERIFY(!northEastFrom(projection.get(), kOrigin, QGeoCoordinate(), north, east));

    QVERIFY(!coordinateAt(projection.get(), QGeoCoordinate(), 10.0, 10.0).isValid());
    QVERIFY(!coordinateAt(projection.get(), kOrigin, qQNaN(), 10.0).isValid());
}

UT_REGISTER_TEST(LocalGridProjectionTest, TestLabel::Unit)
