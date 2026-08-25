#pragma once

#include "QmlUITestBase.h"

/// Boots the real fly view with the local grid enabled and a vehicle connected.
///
/// Everything else about the grid is tested against its properties, which says nothing about
/// whether it draws. A Canvas whose paint handler throws logs a warning and then renders nothing:
/// the view is present, correctly sized, reporting correct numbers, and blank. Strict log mode
/// turns that warning into a failure, and the grabbed frame catches a paint that simply never ran.
class FlyViewLocalGridUITest : public QmlUITestBase
{
    Q_OBJECT

protected slots:
    void init() override;
    void cleanup() override;

private slots:
    void _gridReplacesTheMapAndPaints_test();
    void _aWarningWrapsRatherThanWideningTheGrid_test();
    void _theReasonTheVehicleWillNotArmIsOnThePanel_test();
    void _aSilentRefusalMakesTheVehicleAskWhy_test();
    void _theAirspeedPanelFollowsTheSensor_test();
    void _planModeSwapsTheStripWithoutLosingTheMode_test();
    void _shapingThePatternIsReachableInPlanMode_test();
    void _aFingerWorksTheGridTheSameWayAMouseDoes_test();
    void _aFingerCanPickUpAWaypointAndMoveIt_test();
    void _theAfterFlightStripEntryOpensItsRepairs_test();
    void _theToolbarCarriesThePlanActionsAtEverySize_test();
};
