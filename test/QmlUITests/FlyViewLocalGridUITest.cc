#include "FlyViewLocalGridUITest.h"

#include <QtCore/QSet>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include "FlyViewSettings.h"
#include "MockLink.h"
#include "SettingsManager.h"

UT_REGISTER_TEST(FlyViewLocalGridUITest, TestLabel::Integration)

namespace {

/// @return the number of distinct colours in the image, giving up once @a limit is reached.
/// A view that never painted comes back as one flat colour.
int countDistinctColours(const QImage &image, int limit)
{
    QSet<QRgb> colours;
    for (int y = 0; (y < image.height()) && (colours.size() < limit); y++) {
        for (int x = 0; (x < image.width()) && (colours.size() < limit); x++) {
            colours.insert(image.pixel(x, y));
        }
    }
    return static_cast<int>(colours.size());
}

} // namespace

void FlyViewLocalGridUITest::cleanup()
{
    // The setting is persisted, so leaving it on would put every later test's fly view on the grid
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(false);
    QmlUITestBase::cleanup();
}

void FlyViewLocalGridUITest::_gridReplacesTheMapAndPaints_test()
{
    // Set before the UI boots, so the fly view comes up on the grid rather than switching to it
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> & /*mockLink*/, Vehicle * /*vehicle*/) {
            QQuickItem *const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");
            QVERIFY2((gridView->width() > 0) && (gridView->height() > 0), "the grid was given no room to draw in");

            // Let the canvases paint and a few telemetry frames land
            QTRY_VERIFY_WITH_TIMEOUT(gridView->property("positionValid").toBool(), TestTimeout::longMs());

            const QImage frame = _window->grabWindow();
            QVERIFY2(!frame.isNull(), "the window produced no frame to check");

            // A band in the upper middle: clear of the toolbar, the tool strip, the compass rose, the
            // readout and scale bar, and the vehicle marker sitting dead centre while the view is
            // following it. Only grid lines and
            // their labels fall here, so a flat colour means the grid did not draw. Sampling the
            // centre instead would pass on the vehicle marker alone.
            const QRect gridOnly(frame.width() * 2 / 5, frame.height() / 5,
                                 frame.width() / 5, frame.height() / 10);
            QVERIFY(gridOnly.isValid());

            const int colours = countDistinctColours(frame.copy(gridOnly), 16);
            QVERIFY2(colours >= 3,
                     qPrintable(QStringLiteral("the grid area rendered %1 distinct colours, which is a blank field")
                                    .arg(colours)));
        });
}
