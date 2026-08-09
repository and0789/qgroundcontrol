#include "LocalGridTrailTest.h"

#include <QtCore/QtNumeric>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtTest/QTest>

namespace {

/// Creates a trail, owned by the caller
QObject *createTrail(QQmlComponent &component, QString &error)
{
    component.setData(R"(
        import QGroundControl.FlyView

        LocalGridTrail { }
    )", QUrl());
    if (!component.isReady()) {
        error = component.errorString();
        return nullptr;
    }

    QObject *const trail = component.create();
    if (!trail) {
        error = component.errorString();
    }
    return trail;
}

bool addPoint(QObject *trail, double north, double east)
{
    QVariant result;
    const bool invoked = QMetaObject::invokeMethod(trail, "addPoint", Qt::DirectConnection,
                                                   Q_RETURN_ARG(QVariant, result),
                                                   Q_ARG(QVariant, north), Q_ARG(QVariant, east));
    return invoked && result.toBool();
}

/// The kept points, as (north, east) pairs
QList<QPointF> trailPoints(QObject *trail)
{
    QVariant result;
    if (!QMetaObject::invokeMethod(trail, "points", Qt::DirectConnection, Q_RETURN_ARG(QVariant, result))) {
        return {};
    }

    QList<QPointF> points;
    const QVariantList list = result.toList();
    for (const QVariant &entry : list) {
        const QVariantMap point = entry.toMap();
        points.append(QPointF(point.value(QStringLiteral("north")).toDouble(),
                              point.value(QStringLiteral("east")).toDouble()));
    }
    return points;
}

} // namespace

#define MAKE_TRAIL(name)                                          \
    QQmlEngine name##Engine;                                      \
    name##Engine.addImportPath(QStringLiteral("qrc:/qml"));       \
    QQmlComponent name##Component(&name##Engine);                 \
    QString name##Error;                                          \
    const QScopedPointer<QObject> name(                           \
        createTrail(name##Component, name##Error));               \
    QVERIFY2(name, qPrintable(name##Error))

/// Sampling on every update would fill the whole buffer during a hover and leave no room for the
/// transit that follows -- the part of the flight that carries the drift.
void LocalGridTrailTest::_sampledByDistanceNotByUpdate_test()
{
    MAKE_TRAIL(trail);
    trail->setProperty("minSampleMetres", 0.5);

    QVERIFY2(addPoint(trail.get(), 0.0, 0.0), "the first point is always kept");

    // Position noise on a stationary aircraft
    for (int i = 0; i < 50; i++) {
        addPoint(trail.get(), 0.01 * ((i % 2) ? 1 : -1), 0.02);
    }
    QCOMPARE(trail->property("pointCount").toInt(), 1);

    QVERIFY2(addPoint(trail.get(), 0.0, 0.6), "a real move must be kept");
    QCOMPARE(trail->property("pointCount").toInt(), 2);
}

/// The number shown as distance flown must be the length of the line on screen. A separately
/// accumulated total would keep counting through the noise the trail itself discards.
void LocalGridTrailTest::_pathLengthFollowsTheLineDrawn_test()
{
    MAKE_TRAIL(trail);
    trail->setProperty("minSampleMetres", 0.1);

    // A 10 m box, walked once
    addPoint(trail.get(), 0.0, 0.0);
    addPoint(trail.get(), 10.0, 0.0);
    addPoint(trail.get(), 10.0, 10.0);
    addPoint(trail.get(), 0.0, 10.0);
    addPoint(trail.get(), 0.0, 0.0);

    QCOMPARE(trail->property("pointCount").toInt(), 5);
    QVERIFY(qAbs(trail->property("pathLengthMetres").toDouble() - 40.0) < 1e-6);

    // Back at the origin after 40 m flown: range says nothing without it
    const QList<QPointF> points = trailPoints(trail.get());
    QCOMPARE(points.first(), QPointF(0.0, 0.0));
    QCOMPARE(points.last(), QPointF(0.0, 0.0));
}

/// The regression this design exists for. Truncating the oldest points would delete the departure
/// from the origin, and a return-to-home error is exactly the distance between that departure and
/// the arrival.
void LocalGridTrailTest::_thinningKeepsBothEndsOfTheFlight_test()
{
    MAKE_TRAIL(trail);
    trail->setProperty("minSampleMetres", 1.0);
    trail->setProperty("maxPoints", 16);

    for (int i = 0; i < 40; i++) {
        addPoint(trail.get(), i * 1.0, 0.0);
    }

    const int count = trail->property("pointCount").toInt();
    QVERIFY2(count <= 16, "the buffer must stay capped");
    QVERIFY2(count > 4, "thinning must not collapse the trail to a handful of points");

    const QList<QPointF> points = trailPoints(trail.get());
    QVERIFY2(points.first() == QPointF(0.0, 0.0), "the departure from the origin must survive thinning");

    // Sampling by distance leaves the newest point up to one spacing behind the aircraft, which the
    // drawing closes; what matters here is that the trail still reaches the far end of the flight.
    const double spacing = trail->property("effectiveSampleMetres").toDouble();
    QVERIFY2(spacing > 1.0, "spacing must widen instead of the flight being cut short");
    QVERIFY2((39.0 - points.last().x()) <= spacing,
             qPrintable(QStringLiteral("trail ends at %1, more than %2 m short of 39")
                            .arg(points.last().x()).arg(spacing)));
}

/// An unpopulated or lost position arrives as NaN. Pushing it would put a break in the line and
/// poison the distance flown with a NaN that never recovers.
void LocalGridTrailTest::_nonFinitePositionIsRejected_test()
{
    MAKE_TRAIL(trail);

    addPoint(trail.get(), 0.0, 0.0);
    QVERIFY(!addPoint(trail.get(), qQNaN(), 5.0));
    QVERIFY(!addPoint(trail.get(), 5.0, qQNaN()));

    QCOMPARE(trail->property("pointCount").toInt(), 1);
    QVERIFY(qIsFinite(trail->property("pathLengthMetres").toDouble()));
}

void LocalGridTrailTest::_resetClearsEverything_test()
{
    MAKE_TRAIL(trail);
    trail->setProperty("minSampleMetres", 1.0);
    trail->setProperty("maxPoints", 8);

    for (int i = 0; i < 30; i++) {
        addPoint(trail.get(), i * 1.0, 0.0);
    }
    QVERIFY(trail->property("pointCount").toInt() > 0);

    QVERIFY(QMetaObject::invokeMethod(trail.get(), "reset"));

    QCOMPARE(trail->property("pointCount").toInt(), 0);
    QCOMPARE(trail->property("pathLengthMetres").toDouble(), 0.0);
    // The widened spacing has to come back too, or the next flight is sampled coarsely for no reason
    QCOMPARE(trail->property("effectiveSampleMetres").toDouble(), 1.0);
}

UT_REGISTER_TEST(LocalGridTrailTest, TestLabel::Unit)
