import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Runs the bench optical flow scale calibration and reports whether it passed.
///
/// The propeller warning is not decoration: this asks the operator to rotate a powered vehicle by
/// hand, so the dialog refuses to start while armed and requires the removal to be confirmed.
QGCPopupDialog {
    id:         _root
    title:      qsTr("Optical Flow Calibration")
    buttons:    Dialog.Close

    property var    _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property var    _calibrator:    _activeVehicle ? _activeVehicle.opticalFlowCalibrator : null
    property bool   _armed:         _activeVehicle ? _activeVehicle.armed : false
    property bool   _canStart:      _calibrator && !_armed && propellersRemovedCheckBox.checked
    property real   _fieldWidth:    ScreenTools.defaultFontPixelWidth * 52

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    ColumnLayout {
        spacing: ScreenTools.defaultFontPixelHeight / 2

        QGCLabel {
            Layout.preferredWidth:  _fieldWidth
            wrapMode:               Text.WordWrap
            visible:                !_activeVehicle
            color:                  qgcPal.warningText
            text:                   qsTr("No vehicle connected.")
        }

        // ---------------- Preparation ----------------

        ColumnLayout {
            Layout.preferredWidth:  _fieldWidth
            spacing:                ScreenTools.defaultFontPixelHeight / 2
            visible:                _calibrator && !_calibrator.running && !_calibrator.finished

            QGCLabel {
                Layout.fillWidth:   true
                wrapMode:           Text.WordWrap
                text:               _calibrator ? _calibrator.instruction : ""
            }

            QGCLabel {
                Layout.fillWidth:   true
                wrapMode:           Text.WordWrap
                font.pointSize:     ScreenTools.smallFontPointSize
                text:               qsTr("Rotate the vehicle around the sensor rather than swinging it at arm's length, " +
                                         "and do not yaw. Translation and yaw both corrupt the fit.")
            }

            QGCLabel {
                Layout.fillWidth:   true
                wrapMode:           Text.WordWrap
                color:              qgcPal.warningText
                visible:            _armed
                text:               qsTr("The vehicle is armed. Disarm before calibrating.")
            }

            QGCCheckBox {
                id:     propellersRemovedCheckBox
                text:   qsTr("I have removed the propellers")
            }

            QGCButton {
                text:       qsTr("Start")
                enabled:    _canStart
                onClicked:  _calibrator.start()
            }
        }

        // ---------------- Collecting ----------------

        ColumnLayout {
            Layout.preferredWidth:  _fieldWidth
            spacing:                ScreenTools.defaultFontPixelHeight / 3
            visible:                _calibrator && _calibrator.running

            QGCLabel {
                Layout.fillWidth:   true
                wrapMode:           Text.WordWrap
                font.bold:          true
                text:               _calibrator ? _calibrator.instruction : ""
            }

            GridLayout {
                columns:        2
                columnSpacing:  ScreenTools.defaultFontPixelWidth * 2

                QGCLabel { text: qsTr("Roll samples") }
                QGCLabel {
                    text:   _calibrator ? _calibrator.rollSampleCount + " / " + 100 : ""
                    color:  (_calibrator && _calibrator.rollSampleCount >= 100) ? qgcPal.colorGreen : qgcPal.text
                }

                QGCLabel { text: qsTr("Pitch samples") }
                QGCLabel {
                    text:   _calibrator ? _calibrator.pitchSampleCount + " / " + 100 : ""
                    color:  (_calibrator && _calibrator.pitchSampleCount >= 100) ? qgcPal.colorGreen : qgcPal.text
                }

                QGCLabel { text: qsTr("Image quality") }
                QGCLabel {
                    text:   _calibrator ? _calibrator.currentQuality : ""
                    // Below 50 the fit is being read off images the sensor cannot track well
                    color:  (_calibrator && _calibrator.currentQuality >= 50) ? qgcPal.colorGreen : qgcPal.colorRed
                }

                QGCLabel { text: qsTr("Discarded") }
                QGCLabel {
                    text: _calibrator
                              ? qsTr("%1 low quality, %2 yaw").arg(_calibrator.rejectedQualityCount).arg(_calibrator.rejectedYawCount)
                              : ""
                }
            }

            RowLayout {
                QGCButton {
                    text:       qsTr("Finish")
                    enabled:    _calibrator && (_calibrator.rollSampleCount > 0 || _calibrator.pitchSampleCount > 0)
                    onClicked:  _calibrator.finish()
                }

                QGCButton {
                    text:       qsTr("Cancel")
                    onClicked:  _calibrator.cancel()
                }
            }
        }

        // ---------------- Result ----------------

        ColumnLayout {
            Layout.preferredWidth:  _fieldWidth
            spacing:                ScreenTools.defaultFontPixelHeight / 2
            visible:                _calibrator && _calibrator.finished

            QGCLabel {
                Layout.fillWidth:   true
                wrapMode:           Text.WordWrap
                font.bold:          true
                font.pointSize:     ScreenTools.mediumFontPointSize
                color:              (_calibrator && _calibrator.succeeded) ? qgcPal.colorGreen : qgcPal.colorRed
                text:               (_calibrator && _calibrator.succeeded) ? qsTr("PASSED") : qsTr("FAILED")
            }

            QGCFlickable {
                Layout.fillWidth:       true
                Layout.preferredHeight: Math.min(resultLabel.contentHeight, ScreenTools.defaultFontPixelHeight * 18)
                contentHeight:          resultLabel.contentHeight
                flickableDirection:     Flickable.VerticalFlick
                clip:                   true

                QGCLabel {
                    id:                 resultLabel
                    width:              parent.width
                    wrapMode:           Text.WordWrap
                    font.family:        ScreenTools.fixedFontFamily
                    font.pointSize:     ScreenTools.smallFontPointSize
                    text:               _calibrator ? _calibrator.resultSummary : ""
                }
            }

            QGCLabel {
                Layout.fillWidth:   true
                wrapMode:           Text.WordWrap
                visible:            _calibrator && _calibrator.hasSuggestions
                text:               qsTr("Writing these values changes the vehicle permanently. Record again " +
                                         "afterwards: the slope should then come out near 1.00.")
            }

            RowLayout {
                visible: _calibrator && _calibrator.hasSuggestions

                QGCButton {
                    text:       qsTr("Write to Vehicle")
                    primary:    true
                    onClicked:  _calibrator.applySuggestions()
                }
            }

            QGCButton {
                text:       qsTr("Calibrate Again")
                onClicked:  _calibrator.cancel()
            }
        }
    }
}
