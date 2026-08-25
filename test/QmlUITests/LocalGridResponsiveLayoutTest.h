#pragma once

#include <QtCore/QRectF>
#include <QtCore/QString>

#include "QmlUITestBase.h"

class QQuickItem;
class QQuickWindow;

/// Whether the Local Grid's floating panels fit the window they are drawn in, at sizes from a phone
/// in portrait to a desktop monitor.
///
/// Every panel here anchors itself independently against the window's edges and against its
/// neighbours' measured size, and none of it is exercised anywhere near a phone-sized window: the
/// desktop this was built on always had room to spare. This is the test that would have said so
/// before an operator's phone did.
class LocalGridResponsiveLayoutTest : public QmlUITestBase
{
    Q_OBJECT

protected slots:
    void init() override;
    void cleanup() override;

private slots:
    void _panelsStayInsideThePhoneWindow_test();
    void _panelsDoNotOverlapAtAnySize_test();
    void _theNonGpsPanelStaysOnScreenAndOffTheScaleBar_test();
    void _clickPanelStaysInsideTheWindowNearAnEdge_test();
    void _clickPanelDoesNotCoverThePointItDescribes_test();
    void _chromeStaysWithinBudgetAtAnySize_test();

private:
    /// The rect a panel occupies in window coordinates, or an empty rect if it is not currently
    /// visible. A panel that is not shown -- the airspeed strip on a vehicle with no pitot, say --
    /// has nothing to collide with and nothing to run off the edge, so it is excluded rather than
    /// failed.
    QRectF _windowRectFor(const QString& objectName);

    /// Resizes the window and waits for the panels to finish reflowing against the new size.
    /// Returns false if the layout never settles.
    bool _resizeAndSettle(int width, int height);
};
