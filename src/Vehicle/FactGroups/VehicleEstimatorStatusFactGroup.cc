#include "VehicleEstimatorStatusFactGroup.h"
#include "Vehicle.h"

VehicleEstimatorStatusFactGroup::VehicleEstimatorStatusFactGroup(QObject *parent)
    : FactGroup(500, QStringLiteral(":/json/Vehicle/EstimatorStatusFactGroup.json"), parent)
{
    _addFact(&_goodAttitudeEstimateFact);
    _addFact(&_goodHorizVelEstimateFact);
    _addFact(&_goodVertVelEstimateFact);
    _addFact(&_goodHorizPosRelEstimateFact);
    _addFact(&_goodHorizPosAbsEstimateFact);
    _addFact(&_goodVertPosAbsEstimateFact);
    _addFact(&_goodVertPosAGLEstimateFact);
    _addFact(&_goodConstPosModeEstimateFact);
    _addFact(&_goodPredHorizPosRelEstimateFact);
    _addFact(&_goodPredHorizPosAbsEstimateFact);
    _addFact(&_gpsGlitchFact);
    _addFact(&_accelErrorFact);
    _addFact(&_velRatioFact);
    _addFact(&_horizPosRatioFact);
    _addFact(&_vertPosRatioFact);
    _addFact(&_magRatioFact);
    _addFact(&_haglRatioFact);
    _addFact(&_tasRatioFact);
    _addFact(&_horizPosAccuracyFact);
    _addFact(&_vertPosAccuracyFact);
}

void VehicleEstimatorStatusFactGroup::handleMessage(Vehicle *vehicle, const mavlink_message_t &message)
{
    Q_UNUSED(vehicle);

    // ArduPilot never sends ESTIMATOR_STATUS. It reports the same estimator state through
    // EKF_STATUS_REPORT from its own dialect, so listening only for the common message left this
    // whole group empty on every ArduPilot vehicle.
    switch (message.msgid) {
    case MAVLINK_MSG_ID_ESTIMATOR_STATUS:
        _handleEstimatorStatus(message);
        break;
    case MAVLINK_MSG_ID_EKF_STATUS_REPORT:
        _handleEkfStatusReport(message);
        break;
    default:
        return;
    }

    _setTelemetryAvailable(true);
}

void VehicleEstimatorStatusFactGroup::_handleEstimatorStatus(const mavlink_message_t &message)
{
    mavlink_estimator_status_t estimatorStatus{};
    mavlink_msg_estimator_status_decode(&message, &estimatorStatus);

    goodAttitudeEstimate()->setRawValue(!!(estimatorStatus.flags & ESTIMATOR_ATTITUDE));
    goodHorizVelEstimate()->setRawValue(!!(estimatorStatus.flags & ESTIMATOR_VELOCITY_HORIZ));
    goodVertVelEstimate()->setRawValue(!!(estimatorStatus.flags & ESTIMATOR_VELOCITY_VERT));
    goodHorizPosRelEstimate()->setRawValue(!!(estimatorStatus.flags & ESTIMATOR_POS_HORIZ_REL));
    goodHorizPosAbsEstimate()->setRawValue(!!(estimatorStatus.flags & ESTIMATOR_POS_HORIZ_ABS));
    goodVertPosAbsEstimate()->setRawValue(!!(estimatorStatus.flags & ESTIMATOR_POS_VERT_ABS));
    goodVertPosAGLEstimate()->setRawValue(!!(estimatorStatus.flags & ESTIMATOR_POS_VERT_AGL));
    goodConstPosModeEstimate()->setRawValue(!!(estimatorStatus.flags & ESTIMATOR_CONST_POS_MODE));
    goodPredHorizPosRelEstimate()->setRawValue(!!(estimatorStatus.flags & ESTIMATOR_PRED_POS_HORIZ_REL));
    goodPredHorizPosAbsEstimate()->setRawValue(!!(estimatorStatus.flags & ESTIMATOR_PRED_POS_HORIZ_ABS));
    gpsGlitch()->setRawValue(!!(estimatorStatus.flags & ESTIMATOR_GPS_GLITCH));
    accelError()->setRawValue(!!(estimatorStatus.flags & ESTIMATOR_ACCEL_ERROR));
    velRatio()->setRawValue(estimatorStatus.vel_ratio);
    horizPosRatio()->setRawValue(estimatorStatus.pos_horiz_ratio);
    vertPosRatio()->setRawValue(estimatorStatus.pos_vert_ratio);
    magRatio()->setRawValue(estimatorStatus.mag_ratio);
    haglRatio()->setRawValue(estimatorStatus.hagl_ratio);
    tasRatio()->setRawValue(estimatorStatus.tas_ratio);
    horizPosAccuracy()->setRawValue(estimatorStatus.pos_horiz_accuracy);
    vertPosAccuracy()->setRawValue(estimatorStatus.pos_vert_accuracy);
}

void VehicleEstimatorStatusFactGroup::_handleEkfStatusReport(const mavlink_message_t &message)
{
    mavlink_ekf_status_report_t ekfStatus{};
    mavlink_msg_ekf_status_report_decode(&message, &ekfStatus);

    // EKF_STATUS_FLAGS uses the same bit positions as ESTIMATOR_STATUS_FLAGS for everything below,
    // so the health flags carry over unchanged.
    goodAttitudeEstimate()->setRawValue(!!(ekfStatus.flags & EKF_ATTITUDE));
    goodHorizVelEstimate()->setRawValue(!!(ekfStatus.flags & EKF_VELOCITY_HORIZ));
    goodVertVelEstimate()->setRawValue(!!(ekfStatus.flags & EKF_VELOCITY_VERT));
    goodHorizPosRelEstimate()->setRawValue(!!(ekfStatus.flags & EKF_POS_HORIZ_REL));
    goodHorizPosAbsEstimate()->setRawValue(!!(ekfStatus.flags & EKF_POS_HORIZ_ABS));
    goodVertPosAbsEstimate()->setRawValue(!!(ekfStatus.flags & EKF_POS_VERT_ABS));
    goodVertPosAGLEstimate()->setRawValue(!!(ekfStatus.flags & EKF_POS_VERT_AGL));
    goodConstPosModeEstimate()->setRawValue(!!(ekfStatus.flags & EKF_CONST_POS_MODE));
    goodPredHorizPosRelEstimate()->setRawValue(!!(ekfStatus.flags & EKF_PRED_POS_HORIZ_REL));
    goodPredHorizPosAbsEstimate()->setRawValue(!!(ekfStatus.flags & EKF_PRED_POS_HORIZ_ABS));
    gpsGlitch()->setRawValue(!!(ekfStatus.flags & EKF_GPS_GLITCHING));

    // The fields are named "variance" but carry the same normalised innovation test ratios that
    // ESTIMATOR_STATUS calls ratios, on the same scale, so they fill the same facts.
    velRatio()->setRawValue(ekfStatus.velocity_variance);
    horizPosRatio()->setRawValue(ekfStatus.pos_horiz_variance);
    vertPosRatio()->setRawValue(ekfStatus.pos_vert_variance);
    magRatio()->setRawValue(ekfStatus.compass_variance);
    haglRatio()->setRawValue(ekfStatus.terrain_alt_variance);
    tasRatio()->setRawValue(ekfStatus.airspeed_variance);

    // EKF_STATUS_REPORT carries no position accuracy, so those facts are deliberately left alone
    // rather than filled with a stand-in that would read as a measurement.
}
