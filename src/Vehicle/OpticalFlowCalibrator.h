#pragma once

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtQmlIntegration/QtQmlIntegration>

#include "MAVLinkMessageType.h"

class Vehicle;

/// Runs the bench optical flow scale calibration live, with the propellers off and without flying.
///
/// The vehicle is rotated in place about roll and then pitch. Under pure rotation every bit of
/// image motion comes from that rotation, so the measured flow rate should equal the gyro body
/// rate. Fitting flow = m * body through the origin makes m the sensor's scale error, which
/// converts directly into FLOW_FXSCALER / FLOW_FYSCALER.
///
/// The same numbers are normally recovered afterwards from the OF messages in a .bin log. They are
/// all present in the live OPTICAL_FLOW stream too: flow_rate is the flow rate and the body rate is
/// flow_rate - flow_comp, since the flight code fills flow_comp with (flowRate - bodyRate). Reading
/// them live is what removes the power-down, microSD and offline-analysis round trip.
class OpticalFlowCalibrator : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool finished READ finished NOTIFY stateChanged)
    Q_PROPERTY(QString instruction READ instruction NOTIFY progressChanged)
    Q_PROPERTY(int rollSampleCount READ rollSampleCount NOTIFY progressChanged)
    Q_PROPERTY(int pitchSampleCount READ pitchSampleCount NOTIFY progressChanged)
    Q_PROPERTY(int rejectedQualityCount READ rejectedQualityCount NOTIFY progressChanged)
    Q_PROPERTY(int rejectedYawCount READ rejectedYawCount NOTIFY progressChanged)
    Q_PROPERTY(int currentQuality READ currentQuality NOTIFY progressChanged)
    Q_PROPERTY(bool enoughSamples READ enoughSamples NOTIFY progressChanged)
    Q_PROPERTY(bool succeeded READ succeeded NOTIFY resultChanged)
    Q_PROPERTY(QString resultSummary READ resultSummary NOTIFY resultChanged)
    Q_PROPERTY(bool hasSuggestions READ hasSuggestions NOTIFY resultChanged)
    Q_PROPERTY(QString suggestionSummary READ suggestionSummary NOTIFY resultChanged)
    /// The scale parameters this calibration writes. Named here rather than inline so the pair is
    /// stated once, and so tests can drive the arithmetic against parameters a mock vehicle has.
    Q_PROPERTY(QString xScalerParameterName MEMBER _xScalerParameterName NOTIFY scalerParameterNamesChanged)
    Q_PROPERTY(QString yScalerParameterName MEMBER _yScalerParameterName NOTIFY scalerParameterNamesChanged)

public:
    explicit OpticalFlowCalibrator(Vehicle *vehicle);

    enum State {
        Idle,
        CollectingRoll,
        CollectingPitch,
        Finished,
    };
    Q_ENUM(State)

    /// Rotation rate below which a sample carries too little signal to fit against
    static constexpr double kMinBodyRate = 0.10;
    /// Yaw rotates the image rather than shifting it, so it never reaches the body rate and only
    /// pollutes the fit
    static constexpr double kMaxYawRate = 0.10;
    static constexpr int kMinQuality = 50;
    /// Below this the fit is being read off too little motion to trust
    static constexpr int kMinSamplesPerAxis = 100;
    static constexpr double kMinR2 = 0.90;
    /// Under 2% error the correction is chasing measurement noise rather than fixing scale
    static constexpr double kAccurateTolerance = 0.02;
    static constexpr int kScalerMin = -800;
    static constexpr int kScalerMax = 800;

    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();
    /// Stops collecting and produces the verdict
    Q_INVOKABLE void finish();
    /// Writes the suggested scalers to the vehicle. Never called automatically: a wrong value here
    /// is written permanently and quietly degrades every later flight.
    Q_INVOKABLE bool applySuggestions();

    State state() const { return _state; }
    bool running() const { return (_state == CollectingRoll) || (_state == CollectingPitch); }
    bool finished() const { return _state == Finished; }
    QString instruction() const { return _instruction; }
    int rollSampleCount() const { return _rollSamples.count(); }
    int pitchSampleCount() const { return _pitchSamples.count(); }
    int rejectedQualityCount() const { return _rejectedQualityCount; }
    int rejectedYawCount() const { return _rejectedYawCount; }
    int currentQuality() const { return _currentQuality; }
    bool enoughSamples() const;
    bool succeeded() const { return _succeeded; }
    QString resultSummary() const { return _resultSummary; }
    bool hasSuggestions() const { return _suggestedFxValid || _suggestedFyValid; }
    QString suggestionSummary() const { return _suggestionSummary; }

signals:
    void stateChanged();
    void progressChanged();
    void resultChanged();
    void scalerParameterNamesChanged();

private slots:
    void _mavlinkMessageReceived(const mavlink_message_t &message);

private:
    struct Sample_s {
        double bodyRate;
        double flowSameAxis;
        double flowCrossAxis;
    };

    struct Fit_s {
        bool valid = false;
        double slope = 0.0;
        double r2 = 0.0;
        int count = 0;
    };

    void _handleOpticalFlow(const mavlink_message_t &message);
    void _reset();
    void _updateInstruction();
    /// @return true if a suggested scaler was produced for this axis
    bool _reportAxis(const QString &axisLabel, const QString &parameterName, const QList<Sample_s> &samples,
                     QStringList &lines, int &suggestedScaler);
    bool _scalerParameterValue(const QString &parameterName, double &value) const;

    static Fit_s _fitThroughOrigin(const QList<Sample_s> &samples, bool crossAxis);
    static int _scalerFromSlope(double slope, double oldScaler);

    Vehicle *_vehicle = nullptr;
    State _state = Idle;
    QString _instruction;
    QString _resultSummary;
    QString _suggestionSummary;
    bool _succeeded = false;

    QList<Sample_s> _rollSamples;
    QList<Sample_s> _pitchSamples;
    int _rejectedQualityCount = 0;
    int _rejectedYawCount = 0;
    int _currentQuality = 0;
    double _yawRate = 0.0;

    QString _xScalerParameterName = QStringLiteral("FLOW_FXSCALER");
    QString _yScalerParameterName = QStringLiteral("FLOW_FYSCALER");

    int _suggestedFxScaler = 0;
    int _suggestedFyScaler = 0;
    bool _suggestedFxValid = false;
    bool _suggestedFyValid = false;
};
