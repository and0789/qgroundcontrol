#include "OpticalFlowCalibrator.h"

#include <QtCore/QStringList>

#include <cmath>

#include "Fact.h"
#include "ParameterManager.h"
#include "Vehicle.h"

namespace {

/// -1 selects the vehicle's default component, matching how QML looks parameters up
constexpr int kDefaultComponentId = -1;

} // namespace

OpticalFlowCalibrator::OpticalFlowCalibrator(Vehicle *vehicle)
    : QObject(vehicle)
    , _vehicle(vehicle)
{
    _updateInstruction();
}

bool OpticalFlowCalibrator::enoughSamples() const
{
    return (_rollSamples.count() >= kMinSamplesPerAxis) && (_pitchSamples.count() >= kMinSamplesPerAxis);
}

void OpticalFlowCalibrator::start()
{
    if (!_vehicle) {
        return;
    }

    _reset();
    _state = CollectingRoll;
    (void) connect(_vehicle, &Vehicle::mavlinkMessageReceived, this, &OpticalFlowCalibrator::_mavlinkMessageReceived,
                   Qt::UniqueConnection);
    _setFlowMessageRate(kCalibrationFlowRateHz);

    _updateInstruction();
    emit stateChanged();
    emit resultChanged();
}

void OpticalFlowCalibrator::cancel()
{
    if (_vehicle) {
        (void) disconnect(_vehicle, &Vehicle::mavlinkMessageReceived, this, &OpticalFlowCalibrator::_mavlinkMessageReceived);
    }

    _setFlowMessageRate(0);
    _reset();
    _state = Idle;

    _updateInstruction();
    emit stateChanged();
    emit resultChanged();
}

void OpticalFlowCalibrator::_setFlowMessageRate(int rateHz)
{
    if (!_vehicle) {
        return;
    }

    // Nothing to hand back if the rate was never taken, and asking anyway would put a command on a
    // link that may be the vehicle's only one
    if ((rateHz == 0) && !_flowRateRaised) {
        return;
    }

    _vehicle->setMessageRate(static_cast<uint8_t>(_vehicle->defaultComponentId()), MAVLINK_MSG_ID_OPTICAL_FLOW, rateHz);
    _flowRateRaised = (rateHz > 0);
}

void OpticalFlowCalibrator::_reset()
{
    _rollSamples.clear();
    _pitchSamples.clear();
    _rejectedQualityCount = 0;
    _rejectedYawCount = 0;
    _warningCount = 0;
    _currentQuality = 0;
    _yawRate = 0.0;
    _succeeded = false;
    _resultSummary.clear();
    _suggestionSummary.clear();
    _suggestedFxValid = false;
    _suggestedFyValid = false;

    emit progressChanged();
}

void OpticalFlowCalibrator::_mavlinkMessageReceived(const mavlink_message_t &message)
{
    switch (message.msgid) {
    case MAVLINK_MSG_ID_ATTITUDE: {
        mavlink_attitude_t attitude{};
        mavlink_msg_attitude_decode(&message, &attitude);
        _yawRate = attitude.yawspeed;
        break;
    }
    case MAVLINK_MSG_ID_OPTICAL_FLOW:
        _handleOpticalFlow(message);
        break;
    default:
        break;
    }
}

void OpticalFlowCalibrator::_handleOpticalFlow(const mavlink_message_t &message)
{
    if (!running()) {
        return;
    }

    mavlink_optical_flow_t opticalFlow{};
    mavlink_msg_optical_flow_decode(&message, &opticalFlow);

    _currentQuality = opticalFlow.quality;

    if (opticalFlow.quality < kMinQuality) {
        _rejectedQualityCount++;
        emit progressChanged();
        return;
    }

    if (std::abs(_yawRate) > kMaxYawRate) {
        _rejectedYawCount++;
        emit progressChanged();
        return;
    }

    // flow_comp carries (flowRate - bodyRate), so the body rate comes back by subtracting it
    const double flowX = opticalFlow.flow_rate_x;
    const double flowY = opticalFlow.flow_rate_y;
    const double bodyX = flowX - opticalFlow.flow_comp_m_x;
    const double bodyY = flowY - opticalFlow.flow_comp_m_y;

    // A sample only counts when one axis is clearly moving and the other is not. Mixed rotation
    // leaks one axis into the other's flow and biases its scale.
    if ((std::abs(bodyX) >= kMinBodyRate) && (std::abs(bodyY) < kMinBodyRate)) {
        _rollSamples.append({ bodyX, flowX, flowY });
    } else if ((std::abs(bodyY) >= kMinBodyRate) && (std::abs(bodyX) < kMinBodyRate)) {
        _pitchSamples.append({ bodyY, flowY, flowX });
    }

    if ((_state == CollectingRoll) && (_rollSamples.count() >= kMinSamplesPerAxis)) {
        _state = CollectingPitch;
        emit stateChanged();
    }

    _updateInstruction();
    emit progressChanged();
}

void OpticalFlowCalibrator::_updateInstruction()
{
    switch (_state) {
    case Idle:
        _instruction = tr("Remove the propellers. Hold the vehicle level at eye level, about 1 to 1.5 m "
                          "above a textured, evenly lit surface.");
        break;
    case CollectingRoll:
        _instruction = tr("Rotate about ROLL, about ±15°, roughly one swing per second. Turn the vehicle "
                          "around the sensor, do not swing it at arm's length, and do not yaw.");
        break;
    case CollectingPitch:
        _instruction = enoughSamples()
                           ? tr("Enough data collected on both axes. Press Finish to see the result.")
                           : tr("Now rotate about PITCH, about ±15°, roughly one swing per second.");
        break;
    case Finished:
        _instruction = tr("Calibration finished.");
        break;
    }
}

void OpticalFlowCalibrator::finish()
{
    if (_vehicle) {
        (void) disconnect(_vehicle, &Vehicle::mavlinkMessageReceived, this, &OpticalFlowCalibrator::_mavlinkMessageReceived);
    }

    _setFlowMessageRate(0);

    _state = Finished;
    _suggestedFxValid = false;
    _suggestedFyValid = false;
    _warningCount = 0;

    QStringList lines;
    lines.append(tr("Samples used: %1 roll, %2 pitch").arg(_rollSamples.count()).arg(_pitchSamples.count()));
    lines.append(tr("Discarded: %1 low quality, %2 too much yaw").arg(_rejectedQualityCount).arg(_rejectedYawCount));

    _suggestedFxValid = _reportAxis(tr("X (ROLL)"), _xScalerParameterName, _rollSamples, lines,
                                    _suggestedFxScaler);
    _suggestedFyValid = _reportAxis(tr("Y (PITCH)"), _yScalerParameterName, _pitchSamples, lines,
                                    _suggestedFyScaler);

    // Any axis that could not be analysed fails the whole run: a scale correction applied to one
    // axis while the other is unknown leaves the sensor half calibrated.
    _succeeded = !_rollSamples.isEmpty() && !_pitchSamples.isEmpty();
    for (const QString &line : lines) {
        if (line.contains(tr("FAILED"))) {
            _succeeded = false;
            break;
        }
    }

    if (_succeeded && hasWarnings()) {
        // A pass carrying warnings usually means the fit described the hand movement as much as the
        // sensor, so it is reported apart from a clean pass rather than buried under it
        lines.prepend(tr("RESULT: PASSED WITH WARNINGS — read the warnings below before writing anything."));
    } else if (_succeeded) {
        lines.prepend(hasSuggestions() ? tr("RESULT: PASSED — new scaler values suggested below.")
                                       : tr("RESULT: PASSED — flow scale is already accurate, nothing to change."));
    } else {
        lines.prepend(tr("RESULT: FAILED — read the notes below, fix the cause, and record again."));
    }

    _resultSummary = lines.join(QStringLiteral("\n"));

    QStringList suggestions;
    if (_suggestedFxValid) {
        suggestions.append(QStringLiteral("%1 %2").arg(_xScalerParameterName)
                               .arg(_suggestedFxScaler, 0, 10));
    }
    if (_suggestedFyValid) {
        suggestions.append(QStringLiteral("%1 %2").arg(_yScalerParameterName)
                               .arg(_suggestedFyScaler, 0, 10));
    }
    _suggestionSummary = suggestions.join(QStringLiteral("\n"));

    _updateInstruction();
    emit stateChanged();
    emit progressChanged();
    emit resultChanged();
}

bool OpticalFlowCalibrator::_reportAxis(const QString &axisLabel, const QString &parameterName,
                                        const QList<Sample_s> &samples, QStringList &lines, int &suggestedScaler)
{
    lines.append(QString());
    lines.append(tr("--- Axis %1 (%2) ---").arg(axisLabel, parameterName));

    const Fit_s sameAxis = _fitThroughOrigin(samples, false);
    if (!sameAxis.valid) {
        lines.append(tr("FAILED: no usable samples. Rotation too gentle, too much yaw, or image quality "
                        "below %1.").arg(kMinQuality));
        return false;
    }

    lines.append(tr("Samples: %1").arg(sameAxis.count));
    lines.append(tr("Slope (flow/body): %1").arg(sameAxis.slope, 0, 'f', 4));
    lines.append(tr("R²: %1").arg(sameAxis.r2, 0, 'f', 4));

    const Fit_s crossAxis = _fitThroughOrigin(samples, true);
    if (crossAxis.valid) {
        lines.append(tr("Cross axis slope: %1 (should be near 0)").arg(crossAxis.slope, 0, 'f', 4));
        if (std::abs(crossAxis.slope) > std::abs(sameAxis.slope)) {
            // FLOW_ORIENT_YAW is in centi-degrees, so the value to enter is 100x the angle. Naming
            // the number outright avoids entering 90 and rotating by less than a degree.
            lines.append(tr("FAILED: the cross axis responds more than the axis itself, so the sensor is "
                            "mounted rotated by 90°. Set FLOW_ORIENT_YAW to 9000 or -9000 (the parameter "
                            "is in centi-degrees, so 90 would mean 0.9°), then record again."));
            return false;
        }
    }

    if (sameAxis.slope < 0) {
        lines.append(tr("FAILED: negative slope means flow opposes the gyro, so the sensor is mounted "
                        "rotated by 180°. Set FLOW_ORIENT_YAW to 18000 (the parameter is in "
                        "centi-degrees, so 180 would mean 1.8°), then record again."));
        return false;
    }

    if (sameAxis.r2 < kMinR2) {
        // Translation adds flow the gyro never saw, which pushes the slope above 1. A slope above 1
        // paired with a poor fit is therefore as likely to be the operator's hand as the sensor.
        lines.append(tr("Warning: noisy data (R² below %1). The vehicle was probably translating rather "
                        "than rotating in place, or the surface lacks texture. Hold it higher and turn it "
                        "around the sensor, then record again before writing anything.")
                         .arg(kMinR2, 0, 'f', 2));
        _warningCount++;
    }
    if (sameAxis.count < kMinSamplesPerAxis) {
        lines.append(tr("Warning: few samples (%1 below %2). Use more rotation swings.")
                         .arg(sameAxis.count).arg(kMinSamplesPerAxis));
        _warningCount++;
    }

    double oldScaler = 0;
    if (!_scalerParameterValue(parameterName, oldScaler)) {
        lines.append(tr("FAILED: %1 is not available on this vehicle, so no new value can be computed.")
                         .arg(parameterName));
        return false;
    }

    if (std::abs(1.0 - sameAxis.slope) < kAccurateTolerance) {
        lines.append(tr("Flow scale is already accurate (error below %1%). Leave %2 at %3.")
                         .arg(kAccurateTolerance * 100, 0, 'f', 0).arg(parameterName).arg(oldScaler, 0, 'f', 0));
        return false;
    }

    int newScaler = 0;
    if (!_scalerFromSlope(sameAxis.slope, oldScaler, newScaler)) {
        // A slope this small means the sensor barely reported any flow while the vehicle was
        // clearly rotating, so there is no scale to correct, only a sensor that is not tracking.
        lines.append(tr("FAILED: the slope is too close to zero to derive a scale from. The sensor "
                        "reported almost no flow while the vehicle was rotating. Check the surface "
                        "texture and lighting."));
        return false;
    }

    lines.append(tr("Flow reads %1% too %2.")
                     .arg(std::abs(1.0 - sameAxis.slope) * 100, 0, 'f', 1)
                     .arg(sameAxis.slope < 1.0 ? tr("small") : tr("large")));
    lines.append(tr("%1: %2 → suggested %3").arg(parameterName).arg(oldScaler, 0, 'f', 0).arg(newScaler));

    if (std::abs(newScaler) >= kScalerMax) {
        lines.append(tr("Warning: the suggestion hit the %1 limit. A correction this large points at "
                        "FLOW_POS_* or the holding height rather than at sensor scale.").arg(kScalerMax));
        _warningCount++;
    }

    suggestedScaler = newScaler;
    return true;
}

OpticalFlowCalibrator::Fit_s OpticalFlowCalibrator::_fitThroughOrigin(const QList<Sample_s> &samples, bool crossAxis)
{
    Fit_s fit;
    if (samples.isEmpty()) {
        return fit;
    }

    // Forced through the origin because flow is physically zero at zero body rate. Allowing an
    // intercept would absorb gyro bias into the scale.
    double sumXX = 0.0;
    double sumXY = 0.0;
    for (const Sample_s &sample : samples) {
        const double y = crossAxis ? sample.flowCrossAxis : sample.flowSameAxis;
        sumXX += sample.bodyRate * sample.bodyRate;
        sumXY += sample.bodyRate * y;
    }

    if (sumXX <= 0.0) {
        return fit;
    }

    const double slope = sumXY / sumXX;

    double sumResidual = 0.0;
    double sumTotal = 0.0;
    for (const Sample_s &sample : samples) {
        const double y = crossAxis ? sample.flowCrossAxis : sample.flowSameAxis;
        const double residual = y - (slope * sample.bodyRate);
        sumResidual += residual * residual;
        sumTotal += y * y;
    }

    fit.valid = true;
    fit.slope = slope;
    // Uncentered R², which is the correct form for a fit without an intercept
    fit.r2 = (sumTotal > 0.0) ? (1.0 - (sumResidual / sumTotal)) : 0.0;
    fit.count = samples.count();
    return fit;
}

bool OpticalFlowCalibrator::_scalerFromSlope(double slope, double oldScaler, int &newScaler)
{
    // Written as a positive test so that a NaN slope is rejected as well
    if (!(slope > 0.0)) {
        return false;
    }

    // Firmware applies scale = 1 + 0.001 * FLOW_F?SCALER, and the samples were already recorded
    // with the old scaler applied, so the correction divides rather than adds.
    const double oldScale = 1.0 + (0.001 * oldScaler);
    const double rawScaler = 1000.0 * ((oldScale / slope) - 1.0);
    if (!std::isfinite(rawScaler)) {
        return false;
    }

    // Bounded as a double before rounding. A slope near zero puts this value far outside the range
    // of int, and qRound aborts on that rather than saturating, so bounding afterwards is too late.
    newScaler = qRound(qBound(static_cast<double>(kScalerMin), rawScaler, static_cast<double>(kScalerMax)));
    return true;
}

bool OpticalFlowCalibrator::_scalerParameterValue(const QString &parameterName, double &value) const
{
    if (!_vehicle || !_vehicle->parameterManager()->parameterExists(kDefaultComponentId, parameterName)) {
        return false;
    }

    Fact *const fact = _vehicle->parameterManager()->getParameter(kDefaultComponentId, parameterName);
    if (!fact) {
        return false;
    }

    value = fact->rawValue().toDouble();
    return true;
}

bool OpticalFlowCalibrator::applySuggestions()
{
    if (!_vehicle || !hasSuggestions()) {
        return false;
    }

    bool written = false;
    if (_suggestedFxValid && _vehicle->parameterManager()->parameterExists(kDefaultComponentId, _xScalerParameterName)) {
        _vehicle->parameterManager()->getParameter(kDefaultComponentId, _xScalerParameterName)
            ->setRawValue(_suggestedFxScaler);
        written = true;
    }
    if (_suggestedFyValid && _vehicle->parameterManager()->parameterExists(kDefaultComponentId, _yScalerParameterName)) {
        _vehicle->parameterManager()->getParameter(kDefaultComponentId, _yScalerParameterName)
            ->setRawValue(_suggestedFyScaler);
        written = true;
    }

    return written;
}
