# ============================================================================
# FirmwarePlugin Unit Tests
# Tests for firmware-specific vehicle behaviour driven through FirmwarePlugin
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        GuidedActionBlockingTest.cc
        GuidedActionBlockingTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(GuidedActionBlockingTest LABELS Integration Vehicle)
