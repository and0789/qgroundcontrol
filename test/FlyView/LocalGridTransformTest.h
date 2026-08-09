#pragma once

#include "UnitTest.h"

/// Tests the arithmetic behind the local grid view.
///
/// None of this fails visibly. A grid drawn at the wrong scale, or one whose north runs the wrong
/// way, still looks like a perfectly good grid -- and the numbers read off it would be used to
/// judge how far a GNSS-denied aircraft had drifted.
class LocalGridTransformTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _pixelAndGroundRoundTrip_test();
    void _northRunsUpAndEastRunsRight_test();
    void _gridStepIsARoundNumberOfDisplayedUnits_test();
    void _zoomHoldsTheGroundUnderThePivot_test();
    void _zoomIsClampedToUsefulRange_test();
    void _panMovesGroundWithTheCursor_test();
    void _zoomToFitPutsTheRequestedSquareOnScreen_test();
};
