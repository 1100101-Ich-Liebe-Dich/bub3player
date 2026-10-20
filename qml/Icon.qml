// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import Halcyon

// Vector icon tinted in any colour (rendered by the icon image provider).
Image {
    id: root
    property string name
    property color color: Theme.text
    property int size: 20

    width: size
    height: size
    sourceSize: Qt.size(size * Math.max(1, Screen.devicePixelRatio), size * Math.max(1, Screen.devicePixelRatio))
    source: name !== "" ? "image://icon/" + name + "/" + Theme.hex(color) : ""
    fillMode: Image.PreserveAspectFit
    smooth: true
    cache: true
}
