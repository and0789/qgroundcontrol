import QtQuick
import QtQuick.Controls

import QGroundControl
import QGroundControl.Controls

Button {
    id:             control
    objectName:     toolStripAction ? toolStripAction.objectName : ""
    // The label's unwrapped width, not its laid-out one. The laid-out width is bound to this button
    // so the text can wrap inside it, and reading it back here would close that loop.
    // contentLayoutItem was read for this before, which is an Item and carries no contentWidth at
    // all -- so this was NaN, and every caller has been setting an explicit width over it.
    width:          innerText.implicitWidth + (contentMargins * 2)
    height:         width
    hoverEnabled:   !ScreenTools.isMobile
    enabled:        toolStripAction ? toolStripAction.enabled : true
    visible:        toolStripAction ? toolStripAction.visible : true
    imageSource:    (toolStripAction && modelData) ? (toolStripAction.showAlternateIcon ? modelData.alternateIconSource : modelData.iconSource) : ""
    text:           toolStripAction ? toolStripAction.text : ""
    checked:        toolStripAction ? toolStripAction.checked : false
    checkable:      toolStripAction ? (toolStripAction.dropPanelComponent || (modelData && modelData.checkable)) : false

    /// Whether checking this button leaves the rest of the strip alone, and survives one of them
    /// being checked. A mode switch is not one of the tools it switches between: the fly view's Plan
    /// button stays lit while a plan tool is armed underneath it.
    property bool   nonExclusive:       toolStripAction ? toolStripAction.nonExclusive : false

    property var    toolStripAction:    undefined
    property var    dropPanel:          undefined
    property alias  radius:             buttonBkRect.radius
    property alias  fontPointSize:      innerText.font.pointSize
    property alias  imageSource:        innerImage.source
    property alias  contentWidth:       innerText.contentWidth

    property bool forceImageScale11: false
    // The icon gives up room when the label needs a second line, so both still fit inside the
    // square. A one-line button keeps the scale it has always had, so the strip does not change
    // shape everywhere to accommodate the few names that are long.
    property real imageScale:        forceImageScale11 && (text == "") ? 0.8
                                                                       : (innerText.lineCount > 1 ? 0.42 : 0.6)
    // Measured off the font rather than off the laid-out label. Taken from the label itself this
    // closes a loop the moment the label is allowed to wrap: the margins set how wide the label may
    // be, the width decides whether it wraps, and wrapping changes its height -- which was the
    // margins. One line's worth of the same font answers the same question and depends on nothing.
    property real contentMargins:    singleLineMetrics.height * 0.1

    TextMetrics {
        id:     singleLineMetrics
        font:   innerText.font
        text:   "X"
    }

    property color _currentContentColor:  (checked || pressed) ? qgcPal.buttonHighlightText : qgcPal.text
    property color _currentContentColorSecondary:  (checked || pressed) ? qgcPal.text : qgcPal.buttonHighlight

    signal dropped(int index)

    onCheckedChanged: { if (toolStripAction) toolStripAction.checked = checked }

    onClicked: {
        if (mainWindow.allowViewSwitch()) {
            dropPanel.hide()
            if (!toolStripAction.dropPanelComponent) {
                toolStripAction.triggered(this)
            } else if (checked) {
                var panelEdgeTopPoint = mapToItem(_root, width, 0)
                dropPanel.show(panelEdgeTopPoint, toolStripAction.dropPanelComponent, this)
                checked = true
                control.dropped(index)
            }
        } else if (checkable) {
            checked = !checked
        }
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: control.enabled }

    contentItem: Item {
        id:                 contentLayoutItem
        anchors.fill:       parent
        anchors.margins:    contentMargins

        Column {
            anchors.centerIn:   parent
            spacing:            0

            Image {
                id:                         innerImageColorful
                height:                     contentLayoutItem.height * imageScale
                width:                      contentLayoutItem.width  * imageScale
                smooth:                     true
                mipmap:                     true
                fillMode:                   Image.PreserveAspectFit
                antialiasing:               true
                sourceSize.height:          height
                sourceSize.width:           width
                anchors.horizontalCenter:   parent.horizontalCenter
                source:                     control.imageSource
                visible:                    source != "" && !!modelData && modelData.fullColorIcon
            }

            QGCColoredImage {
                id:                         innerImage
                height:                     contentLayoutItem.height * imageScale
                width:                      contentLayoutItem.width  * imageScale
                smooth:                     true
                mipmap:                     true
                color:                      _currentContentColor
                fillMode:                   Image.PreserveAspectFit
                antialiasing:               true
                sourceSize.height:          height
                sourceSize.width:           width
                anchors.horizontalCenter:   parent.horizontalCenter
                visible:                    source != "" && !(modelData && modelData.fullColorIcon)

                QGCColoredImage {
                    id:                         innerImageSecondColor
                    source:                     modelData ? modelData.alternateIconSource : ""
                    height:                     contentLayoutItem.height * imageScale
                    width:                      contentLayoutItem.width  * imageScale
                    smooth:                     true
                    mipmap:                     true
                    color:                      _currentContentColorSecondary
                    fillMode:                   Image.PreserveAspectFit
                    antialiasing:               true
                    sourceSize.height:          height
                    sourceSize.width:           width
                    anchors.horizontalCenter:   parent.horizontalCenter
                    visible:                    source != "" && !!modelData && modelData.biColorIcon
                }
            }

            QGCLabel {
                id:                         innerText
                text:                       control.text
                color:                      _currentContentColor
                anchors.horizontalCenter:   parent.horizontalCenter
                font.bold:                  !innerImage.visible && !innerImageColorful.visible
                opacity:                    !innerImage.visible ? 0.8 : 1.0
                // The strip is a fixed seven characters wide and clips what overflows it, so a
                // longer name used to run off both edges at once: "Hide Non-GPS" reached the strip
                // reading "ide Non-GP", which names neither the state it is in nor the one it
                // switches to. Wrapped onto a second line instead, and elided only if even that
                // cannot hold it -- a button whose label is a guess is a button nobody presses.
                width:                      contentLayoutItem.width
                horizontalAlignment:        Text.AlignHCenter
                wrapMode:                   Text.WordWrap
                maximumLineCount:           2
                elide:                      Text.ElideRight
            }
        }
    }

    background: Rectangle {
        id:     buttonBkRect
        color:  (control.checked || control.pressed) ?
                    qgcPal.buttonHighlight :
                    ((control.enabled && control.hovered) ? qgcPal.toolStripHoverColor : "transparent")
    }
}
