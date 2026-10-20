// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

ToolTip {
    id: tip
    delay: 650
    timeout: 4000
    padding: 8
    leftPadding: 10
    rightPadding: 10
    font.family: Theme.uiFont
    font.pixelSize: 12
    contentItem: Text {
        text: tip.text
        font: tip.font
        color: Theme.text
    }
    background: Rectangle {
        radius: 6
        color: Theme.surface
        border.color: Theme.border
    }
}
