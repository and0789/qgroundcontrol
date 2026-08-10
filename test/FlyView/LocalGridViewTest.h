#pragma once

#include "BaseClasses/VehicleTest.h"

class QQuickWindow;

/// Tests how the local grid view follows a vehicle.
///
/// The drawing is not asserted on -- what is, is the state the drawing reads: whether there is a
/// position at all, where it is in the estimator's frame, and where the view is looking. A grid
/// centred on stale or absent telemetry is the failure that matters, because it looks exactly like
/// a grid centred on a stationary aircraft.
class LocalGridViewTest : public VehicleTest
{
    Q_OBJECT

public:
    explicit LocalGridViewTest(QObject *parent = nullptr);

protected slots:
    void init() override;

private slots:
    void _withoutVehicle_reportsNoPosition_test();
    void _localPosition_isReadInEstimatorFrame_test();
    void _followingVehicle_keepsItCentred_test();
    void _centreOnOrigin_stopsFollowing_test();
    void _trailAccumulatesFromTelemetry_test();
    void _estimatorHealthUnknownUntilReported_test();
    void _estimatorLosingAiding_isReportedAsSevere_test();
    void _estimatorRejectingMeasurements_isReported_test();
    void _positionGoingQuiet_isReportedAsStale_test();
    void _repeatedIdenticalPositions_keepTheEstimateFresh_test();
    void _withoutEstimatorOrigin_refusesWaypoints_test();
    void _whileThePlanIsTransferring_refusesWaypoints_test();
    void _waypointPlacedInMetres_reachesThePlanAsACoordinate_test();
    void _planIsDrawnInGridMetres_test();
    void _deleteRemovesTheSelectedWaypoint_test();
    void _deleteWithoutASelection_doesNothing_test();
    void _dragMovesTheWaypointToTheDroppedOffsets_test();
    void _bearingAndRangeAgreeWithOffsets_test();
    void _legIsMeasuredFromThePreviousWaypoint_test();
    void _takeoffAndLandingUseTheirOwnInsertions_test();
    void _firstItemOfAnEmptyPlanBecomesATakeoff_test();
    void _homeItemIsNotDrawnAsAWaypoint_test();
    void _takeoffIsHeldOnTheOrigin_test();
    void _itemTypeChangesInPlace_test();
    void _altitudeCanBeAppliedToEveryItem_test();
    void _everyItemAboveTheCeilingIsFound_test();
    void _waypointPanelCarriesTheEditorFields_test();
    void _missionItemRowOpensOnlyWhenCurrent_test();
    void _missionItemRowNamesTheItemItHolds_test();
    void _missionListShowsOneRowPerDrawnItem_test();
    void _missionListOpensTheRowTheGridHasSelected_test();
    void _gridShowsThePlanAsAList_test();
    void _listMarksTheWaypointTheVehicleIsFlyingTo_test();

private:
    static bool _showInWindow(QQuickWindow &window, QObject *list);
};
