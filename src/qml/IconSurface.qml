// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window

// Resize the actual rendering surface rather than scaling its rendered texture.
Item {
    id: surface
    objectName: "iconTraySurface"
    property real baseSize: 22
    property real scalePercent: 100
    property real availableWidth: baseSize
    property real availableHeight: baseSize
    property real devicePixelRatio: Screen.devicePixelRatio

    readonly property real pixelRatio: Math.max(1, devicePixelRatio)
    readonly property real availableEdge: Math.max(0, Math.min(availableWidth, availableHeight))
    readonly property real renderedSize: Math.max(0, Math.min(
        Math.round(baseSize * scalePercent / 100 * pixelRatio),
        Math.floor(availableEdge * pixelRatio))) / pixelRatio

    width: renderedSize
    height: renderedSize
    // Align both edges to physical pixels, including fractional layout positions.
    x: parent ? Math.round((parent.x + (parent.width - width) / 2) * pixelRatio) / pixelRatio - parent.x : 0
    y: parent ? Math.round((parent.y + (parent.height - height) / 2) * pixelRatio) / pixelRatio - parent.y : 0
}
