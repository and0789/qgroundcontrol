# ============================================================================
# FlyView Tests
# Tests for FlyView QML support objects
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        LocalGridAltitudeLimitTest.cc
        LocalGridAltitudeLimitTest.h
        LocalGridProjectionTest.cc
        LocalGridProjectionTest.h
        LocalGridTrailTest.cc
        LocalGridTrailTest.h
        LocalGridTransformTest.cc
        LocalGridTransformTest.h
        LocalGridViewTest.cc
        LocalGridViewTest.h
        NonGpsFlowHealthTest.cc
        NonGpsFlowHealthTest.h
        NonGpsPreFlightChecksTest.cc
        NonGpsPreFlightChecksTest.h
        NonGpsStatusPanelTest.cc
        NonGpsStatusPanelTest.h
        ProximityRadarValuesTest.cc
        ProximityRadarValuesTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(LocalGridAltitudeLimitTest LABELS Integration Vehicle)
add_qgc_test(LocalGridProjectionTest LABELS Unit)
add_qgc_test(LocalGridTrailTest LABELS Unit)
add_qgc_test(LocalGridTransformTest LABELS Unit)
# 94 subtests, each building QML in its own window: ~110s locally against the
# 120s Integration default, so an unrelated rebuild or a busy machine is enough
# to trip it. EXTENDED also scales to 360s under sanitizers.
add_qgc_test(LocalGridViewTest LABELS Integration Vehicle TIMEOUT ${QGC_TEST_TIMEOUT_EXTENDED})
add_qgc_test(NonGpsFlowHealthTest LABELS Integration Vehicle)
add_qgc_test(NonGpsPreFlightChecksTest LABELS Integration Vehicle)
add_qgc_test(NonGpsStatusPanelTest LABELS Integration Vehicle)
add_qgc_test(ProximityRadarValuesTest LABELS Integration Vehicle)
