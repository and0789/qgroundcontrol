#include "LocalGridTransformTest.h"

#include <cmath>

#include <QtCore/QtNumeric>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtTest/QTest>

namespace {

constexpr double kViewWidth = 800.0;
constexpr double kViewHeight = 600.0;

/// Creates a transform sized like a fly view, owned by the caller
QObject *createTransform(QQmlComponent &component, QString &error)
{
    component.setData(R"(
        import QGroundControl.FlyView

        LocalGridTransform { }
    )", QUrl());
    if (!component.isReady()) {
        error = component.errorString();
        return nullptr;
    }

    QObject *const transform = component.create();
    if (!transform) {
        error = component.errorString();
        return nullptr;
    }

    transform->setProperty("viewWidth", kViewWidth);
    transform->setProperty("viewHeight", kViewHeight);
    return transform;
}

double callDouble(QObject *object, const char *method, double argument)
{
    QVariant result;
    const bool invoked = QMetaObject::invokeMethod(object, method, Qt::DirectConnection,
                                                   Q_RETURN_ARG(QVariant, result),
                                                   Q_ARG(QVariant, argument));
    return invoked ? result.toDouble() : qQNaN();
}

} // namespace

#define MAKE_TRANSFORM(name)                                              \
    QQmlEngine name##Engine;                                              \
    name##Engine.addImportPath(QStringLiteral("qrc:/qml"));               \
    QQmlComponent name##Component(&name##Engine);                         \
    QString name##Error;                                                  \
    const QScopedPointer<QObject> name(                                   \
        createTransform(name##Component, name##Error));     \
    QVERIFY2(name, qPrintable(name##Error))

/// Screen to ground and back must be the same mapping in both directions. Waypoints are placed by
/// clicking, and read back as metres, so a mismatch would put a waypoint where it was not clicked.
void LocalGridTransformTest::_pixelAndGroundRoundTrip_test()
{
    MAKE_TRANSFORM(transform);

    transform->setProperty("centreNorth", 37.5);
    transform->setProperty("centreEast", -12.25);
    transform->setProperty("metresPerPixel", 0.4);

    const double norths[] = { 0.0, 5.0, -5.0, 137.5, -240.75 };
    for (const double north : norths) {
        const double pixelY = callDouble(transform.get(), "pixelYForNorth", north);
        QVERIFY(qIsFinite(pixelY));
        QVERIFY(qAbs(callDouble(transform.get(), "northForPixelY", pixelY) - north) < 1e-9);
    }

    const double easts[] = { 0.0, 5.0, -5.0, 137.5, -240.75 };
    for (const double east : easts) {
        const double pixelX = callDouble(transform.get(), "pixelXForEast", east);
        QVERIFY(qIsFinite(pixelX));
        QVERIFY(qAbs(callDouble(transform.get(), "eastForPixelX", pixelX) - east) < 1e-9);
    }
}

/// The one axis that flips. Screen y grows downwards, so getting this wrong mirrors the whole grid
/// north for south -- and a mirrored grid still looks entirely plausible.
void LocalGridTransformTest::_northRunsUpAndEastRunsRight_test()
{
    MAKE_TRANSFORM(transform);

    transform->setProperty("centreNorth", 0.0);
    transform->setProperty("centreEast", 0.0);
    transform->setProperty("metresPerPixel", 0.5);

    // The centre of the view is the centre of the frame
    QVERIFY(qAbs(callDouble(transform.get(), "pixelXForEast", 0.0) - (kViewWidth / 2)) < 1e-9);
    QVERIFY(qAbs(callDouble(transform.get(), "pixelYForNorth", 0.0) - (kViewHeight / 2)) < 1e-9);

    QVERIFY2(callDouble(transform.get(), "pixelYForNorth", 10.0) < (kViewHeight / 2),
             "further north must be higher up the screen");
    QVERIFY2(callDouble(transform.get(), "pixelXForEast", 10.0) > (kViewWidth / 2),
             "further east must be further right");
}

/// Grid lines have to land on numbers the operator can count in, in the unit they chose. Spacing
/// that is round in pixels gives labels like 3.7 and 7.4, which is a grid nobody counts squares on.
void LocalGridTransformTest::_gridStepIsARoundNumberOfDisplayedUnits_test()
{
    MAKE_TRANSFORM(transform);

    const double zoomLevels[] = { 0.02, 0.1, 0.25, 1.0, 4.0, 17.5 };
    for (const double metresPerPixel : zoomLevels) {
        transform->setProperty("metresPerPixel", metresPerPixel);

        const double step = callDouble(transform.get(), "gridStepMetres", 80.0);
        QVERIFY2(step > 0, "a visible zoom must produce a spacing");

        const double displayStep = callDouble(transform.get(), "toDisplay", step);
        const double magnitude = std::pow(10.0, std::floor(std::log10(displayStep)));
        const double mantissa = displayStep / magnitude;

        const bool isRound = (qAbs(mantissa - 1.0) < 1e-6) || (qAbs(mantissa - 2.0) < 1e-6)
                             || (qAbs(mantissa - 5.0) < 1e-6);
        QVERIFY2(isRound, qPrintable(QStringLiteral("spacing %1 is not 1, 2 or 5 times a power of ten")
                                         .arg(displayStep)));

        // Whatever it rounded to must still be a usable size on screen
        const double pixels = step / metresPerPixel;
        QVERIFY2((pixels > 20.0) && (pixels < 320.0),
                 qPrintable(QStringLiteral("spacing came out %1 px at %2 m/px").arg(pixels).arg(metresPerPixel)));
    }
}

/// Zooming about the cursor is what makes examining a corner of the pattern possible. If the ground
/// slides out from under the pointer, every zoom has to be followed by a pan to find the place again.
void LocalGridTransformTest::_zoomHoldsTheGroundUnderThePivot_test()
{
    MAKE_TRANSFORM(transform);

    transform->setProperty("centreNorth", 20.0);
    transform->setProperty("centreEast", -8.0);
    transform->setProperty("metresPerPixel", 0.5);

    const double pivotX = 620.0;
    const double pivotY = 130.0;
    const double groundEast = callDouble(transform.get(), "eastForPixelX", pivotX);
    const double groundNorth = callDouble(transform.get(), "northForPixelY", pivotY);

    QVERIFY(QMetaObject::invokeMethod(transform.get(), "zoomBy", Qt::DirectConnection,
                                      Q_ARG(QVariant, 0.5), Q_ARG(QVariant, pivotX), Q_ARG(QVariant, pivotY)));

    QVERIFY(qAbs(transform->property("metresPerPixel").toDouble() - 0.25) < 1e-9);
    QVERIFY(qAbs(callDouble(transform.get(), "pixelXForEast", groundEast) - pivotX) < 1e-6);
    QVERIFY(qAbs(callDouble(transform.get(), "pixelYForNorth", groundNorth) - pivotY) < 1e-6);
}

/// Unclamped zoom runs to a grid step of zero, which is a division the drawing code would loop on
/// forever, and to a scale where the whole flight is one pixel.
void LocalGridTransformTest::_zoomIsClampedToUsefulRange_test()
{
    MAKE_TRANSFORM(transform);

    const double minimum = transform->property("minMetresPerPixel").toDouble();
    const double maximum = transform->property("maxMetresPerPixel").toDouble();
    QVERIFY(minimum > 0);
    QVERIFY(maximum > minimum);

    QVERIFY(QMetaObject::invokeMethod(transform.get(), "zoomBy", Qt::DirectConnection,
                                      Q_ARG(QVariant, 1e-6), Q_ARG(QVariant, 400.0), Q_ARG(QVariant, 300.0)));
    QCOMPARE(transform->property("metresPerPixel").toDouble(), minimum);

    QVERIFY(QMetaObject::invokeMethod(transform.get(), "zoomBy", Qt::DirectConnection,
                                      Q_ARG(QVariant, 1e6), Q_ARG(QVariant, 400.0), Q_ARG(QVariant, 300.0)));
    QCOMPARE(transform->property("metresPerPixel").toDouble(), maximum);
}

/// Dragging right must carry the ground right with the hand. The sign here is easy to get backwards
/// and the result feels wrong without being obviously wrong.
void LocalGridTransformTest::_panMovesGroundWithTheCursor_test()
{
    MAKE_TRANSFORM(transform);

    transform->setProperty("centreNorth", 0.0);
    transform->setProperty("centreEast", 0.0);
    transform->setProperty("metresPerPixel", 0.5);

    const double startX = callDouble(transform.get(), "pixelXForEast", 0.0);
    const double startY = callDouble(transform.get(), "pixelYForNorth", 0.0);

    QVERIFY(QMetaObject::invokeMethod(transform.get(), "panByPixels", Qt::DirectConnection,
                                      Q_ARG(QVariant, 40.0), Q_ARG(QVariant, 25.0)));

    QVERIFY(qAbs(callDouble(transform.get(), "pixelXForEast", 0.0) - (startX + 40.0)) < 1e-6);
    QVERIFY(qAbs(callDouble(transform.get(), "pixelYForNorth", 0.0) - (startY + 25.0)) < 1e-6);
}

/// The opening view. A first frame showing one square, or the whole county, is one the operator has
/// to fix before the picture tells them anything.
void LocalGridTransformTest::_zoomToFitPutsTheRequestedSquareOnScreen_test()
{
    MAKE_TRANSFORM(transform);

    QVERIFY(QMetaObject::invokeMethod(transform.get(), "zoomToFit", Qt::DirectConnection,
                                      Q_ARG(QVariant, 40.0)));

    transform->setProperty("centreNorth", 0.0);
    transform->setProperty("centreEast", 0.0);

    // A 40 m box centred on the origin has to be inside the view, and not lost in the middle of it
    const double halfBox = 20.0;
    const double top = callDouble(transform.get(), "pixelYForNorth", halfBox);
    const double bottom = callDouble(transform.get(), "pixelYForNorth", -halfBox);
    QVERIFY(top > 0);
    QVERIFY(bottom < kViewHeight);
    QVERIFY2((bottom - top) > (kViewHeight / 2), "the box must fill a useful part of the view");
}

UT_REGISTER_TEST(LocalGridTransformTest, TestLabel::Unit)
