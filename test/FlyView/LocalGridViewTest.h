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
    void _landingTakesTheAltitudeOfTheItemBeforeIt_test();
    void _gridSaysWhetherThePlanAlreadyHasATakeoff_test();
    void _everyItemAboveTheCeilingIsFound_test();
    void _missionItemRowOpensOnlyWhenCurrent_test();
    void _missionItemRowNamesTheItemItHolds_test();
    void _missionListShowsOneRowPerDrawnItem_test();
    void _missionListOpensTheRowTheGridHasSelected_test();
    void _gridShowsThePlanAsAList_test();
    void _listMarksTheWaypointTheVehicleIsFlyingTo_test();
    void _listStaysFoldedUntilThePlanHasSomething_test();
    void _listIsAsTallAsItsRowsUntilItRunsOutOfRoom_test();
    void _typeIsChangedFromTheRowHeader_test();
    void _selectedRowIsBroughtIntoView_test();
    void _headingIsExposedAsANumberAndUnknownStaysUnknown_test();
    void _readoutFoldsUntilThereIsTelemetryAndNeverFoldsAWarning_test();
    void _gridOffsetsBecomeTheCoordinateACorrectionSends_test();
    void _planIsMovedByOneOffsetKeepingItsShape_test();
    void _positionCorrectionIsOfferedOnlyAgainstAnOrigin_test();
    void _driftIsTheReportedPositionMovingWhileParked_test();
    void _parkedAwayFromTheOriginIsNotDrift_test();
    void _armedVehicleIsNotWatchedForDrift_test();
    void _aPlacedWaypointCarriesTheGridsOwnSpeed_test();
    void _oneSpeedCanBeSetOnEveryWaypoint_test();
    void _planIsNumberedInTheOrderItIsFlown_test();
    void _rowIsMarkedWhereverTheVehicleIsInsideIt_test();
    void _takeoffWithNoCoordinateIsStillPartOfThePlan_test();
    void _planCanBeMovedToStartFromTheAircraft_test();
    void _movingThePlanToTheAircraftTwiceMovesItOnce_test();
    void _aFreshPatternIsNotOffsetByTheLastPlansMove_test();
    void _planSaysWhereTheVehicleWouldPickItUp_test();
    void _uploadIsRefusedWhenThePreCheckSaysSo_test();
    void _planAnchorOutlivesTheView_test();

    // Bagian 3: mission-creation parity (Lampiran D)
    void _insertAfterSelectedItem_landsInTheMiddleOfThePlan_test();
    void _insertAfterSelectedItem_selectsTheNewItemNotTheLastOne_test();
    void _nothingCanBeInsertedBeforeTheTakeoff_test();
    void _landHere_refusesAMidPlanSpot_test();
    void _armedTool_placesAfterTheSelectionWithoutClearingIt_test();
    void _armedTool_chainsSeveralPlacementsInARow_test();
    void _roiIsDrawnButNotFlownThrough_test();
    void _selectionIsClearedWhenThePlanArrivesWhole_test();
    void _duplicateItem_copiesPositionAltitudeAndSpeed_test();
    void _insertBetween_splitsTheLegAtItsMidpoint_test();

    // Bagian 4: item detail and plan totals (Lampiran E)
    void _holdTimeIsOfferedOnWaypointsAlone_test();
    void _holdSecondsAreSummedAcrossThePlan_test();
    void _planTotalsIncludeTheWaits_test();
    void _yawItemCarriesAHeadingAndNoLeg_test();

private:
    static bool _showInWindow(QQuickWindow &window, QObject *list);
};
