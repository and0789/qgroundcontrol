# ============================================================================
# FlyView Tests
# Tests for FlyView QML support objects
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
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

add_qgc_test(LocalGridTransformTest LABELS Unit)
add_qgc_test(LocalGridViewTest LABELS Integration Vehicle)
add_qgc_test(NonGpsFlowHealthTest LABELS Integration Vehicle)
add_qgc_test(NonGpsPreFlightChecksTest LABELS Integration Vehicle)
add_qgc_test(NonGpsStatusPanelTest LABELS Integration Vehicle)
add_qgc_test(ProximityRadarValuesTest LABELS Integration Vehicle)
