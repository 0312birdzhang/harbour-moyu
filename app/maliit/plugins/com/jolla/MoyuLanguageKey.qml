// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.0
import Sailfish.Silica 1.0

FunctionKey {
    id: languageKey
    objectName: "moyuLanguageKey"
    property real requestedWidth: punctuationKeyWidth
    readonly property real maximumWidth: parent ? parent.maximumControlKeyWidth(languageKey) : 0
    implicitWidth: Math.min(requestedWidth, maximumWidth)
    background.visible: false

    Canvas {
        objectName: "moyuLanguageIcon"
        anchors.centerIn: parent
        width: Math.min(Theme.iconSizeMedium, languageKey.width * 0.65, languageKey.height * 0.65)
        height: width
        visible: languageKey.caption.length === 0
        property color strokeColor: languageKey.pressed ? Theme.highlightColor : Theme.primaryColor
        onStrokeColorChanged: requestPaint()
        onPaint: {
            var context = getContext("2d")
            context.clearRect(0, 0, width, height)
            context.strokeStyle = strokeColor
            context.lineWidth = Math.max(1.5, width / 24)
            var center = width / 2
            var radius = width * 0.43
            context.beginPath()
            context.arc(center, center, radius, 0, Math.PI * 2)
            context.moveTo(center - radius, center)
            context.lineTo(center + radius, center)
            context.moveTo(center, center - radius)
            context.bezierCurveTo(center - radius * 0.7, center - radius,
                                 center - radius * 0.7, center + radius, center, center + radius)
            context.bezierCurveTo(center + radius * 0.7, center + radius,
                                 center + radius * 0.7, center - radius, center, center - radius)
            context.moveTo(center - radius * 0.87, center - radius / 2)
            context.lineTo(center + radius * 0.87, center - radius / 2)
            context.moveTo(center - radius * 0.87, center + radius / 2)
            context.lineTo(center + radius * 0.87, center + radius / 2)
            context.stroke()
        }
    }
    onClicked: keyboard.layout.handler.toggleLanguage()
}
