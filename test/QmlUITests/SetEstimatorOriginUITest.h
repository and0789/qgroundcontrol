#pragma once

#include "QmlUITestBase.h"

/// Drives the estimator origin dialog through the running UI.
///
/// The origin is the bridge between the frame the estimator flies in and the coordinates a mission
/// is stored against, and until it exists nothing else about flying without GNSS works. Its only
/// previous entry point was a map click, which is unavailable both on the local grid and, more to
/// the point, anywhere a map cannot be loaded.
class SetEstimatorOriginUITest : public QmlUITestBase
{
    Q_OBJECT

protected slots:
    void init() override;
    void cleanup() override;

private slots:
    void _originCanBeSetFromTheGridWithoutAMap_test();
};
