// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

Menu {
    id: m
    padding: 6
    font.family: Theme.uiFont
    font.pixelSize: 14
    background: Rectangle {
        implicitWidth: 230
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.border
    }
    delegate: HMenuItem {}
}
