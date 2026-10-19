import QtQuick
import Halcyon

// Drag rows of a ListView into a new order. Lies over the list and takes only
// left presses on rows (header, scroll bar and right clicks go through), so a
// delegate that scrolls away while dragging cannot lose the grab.
MouseArea {
    id: d
    property ListView list
    property int scrollBarWidth: 16
    signal rowClicked(int row, int modifiers)
    signal rowDoubleClicked(int row)
    signal moveRequested(int from, int to) // to: insert before this row (count = at the end)

    property int from: -1
    property int target: -1
    property bool dragging: false
    property real pressY: 0
    property real lastY: 0
    property bool swallowClick: false

    acceptedButtons: Qt.LeftButton
    preventStealing: true

    function rowAt(y) {
        var p = mapToItem(d.list.contentItem, d.list.width / 2, y)
        return d.list.indexAt(p.x, p.y)
    }
    function updateTarget(y) {
        lastY = y
        var p = mapToItem(d.list.contentItem, d.list.width / 2, Math.max(0, Math.min(height - 1, y)))
        var r = d.list.indexAt(p.x, p.y)
        if (r < 0) {
            // above the first row (header) or below the last one
            target = p.y < (d.list.originY + (d.list.headerItem ? d.list.headerItem.height : 0)) ? 0 : d.list.count
            return
        }
        var item = d.list.itemAtIndex(r)
        target = item && p.y > item.y + item.height / 2 ? r + 1 : r
    }

    onPressed: (m) => {
        var r = rowAt(m.y)
        if (r < 0 || m.x > width - scrollBarWidth) {
            m.accepted = false
            return
        }
        from = r
        pressY = m.y
        dragging = false
        swallowClick = false
    }
    onPositionChanged: (m) => {
        if (from < 0)
            return
        if (!dragging && Math.abs(m.y - pressY) > 6)
            dragging = true
        if (dragging)
            updateTarget(m.y)
    }
    onReleased: {
        if (dragging) {
            swallowClick = true
            if (target >= 0 && target !== from && target !== from + 1)
                d.moveRequested(from, target)
        }
        dragging = false
        target = -1
    }
    onCanceled: { dragging = false; target = -1; from = -1 }
    onClicked: (m) => {
        if (!swallowClick && from >= 0)
            d.rowClicked(from, m.modifiers)
        swallowClick = false
    }
    onDoubleClicked: (m) => {
        var r = rowAt(m.y)
        if (r >= 0)
            d.rowDoubleClicked(r)
    }

    // Scroll while dragging near the top or bottom edge.
    Timer {
        interval: 16
        repeat: true
        running: d.dragging && (d.lastY < 48 || d.lastY > d.height - 48)
        onTriggered: {
            var l = d.list
            var step = d.lastY < 48 ? -Math.max(4, 48 - d.lastY) / 2 : Math.max(4, d.lastY - d.height + 48) / 2
            var maxY = l.originY + l.contentHeight - l.height
            l.contentY = Math.max(l.originY, Math.min(maxY, l.contentY + step))
            d.updateTarget(d.lastY)
        }
    }

    // Where the rows will land.
    Rectangle {
        visible: d.dragging && d.target >= 0
        x: 24
        width: d.width - 48
        height: 3
        radius: 1.5
        color: Theme.accent
        y: {
            if (!visible) return 0
            var l = d.list
            var scrolled = l.contentY // re-evaluate while scrolling
            var t = Math.min(d.target, l.count - 1)
            var item = l.itemAtIndex(t)
            if (!item) return d.target === 0 ? 0 : d.height - 2
            var yy = item.y + (d.target >= l.count ? item.height : 0)
            return d.mapFromItem(l.contentItem, 0, yy).y - 1
        }
    }
}
