// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.0

KeyboardRow {
    property bool separateButtonSizesRequested: false
    readonly property bool fitToWidth: {
        var avoidance = parent ? parent.avoidanceWidth : 0
        if (avoidance > 0) {
            var split = splitIndex > 0 ? splitIndex : Math.floor(children.length / 2)
            var availableWidth = (width - avoidance) / 2
            return _fixedWidthsOverflow(availableWidth, 0, split)
                    || _fixedWidthsOverflow(availableWidth, split, children.length)
        }
        return _fixedWidthsOverflow(width, 0, children.length)
    }
    separateButtonSizes: separateButtonSizesRequested || fitToWidth

    function _maximumButtonWidthPart(shareableWidth, startIndex, endIndex) {
        var basicButtonCount = 0
        var originalWidth = shareableWidth
        var lastIndex = Math.min(endIndex, children.length)
        for (var index = startIndex; index < lastIndex; ++index) {
            var child = children[index]
            if (!child.active || _isExpandingKey(child))
                continue
            if (_isBasicButton(child))
                basicButtonCount++
            else
                shareableWidth -= child.implicitWidth
        }
        return basicButtonCount > 0 ? shareableWidth / basicButtonCount : originalWidth
    }

    function maximumControlKeyWidth(item) {
        var avoidance = parent ? parent.avoidanceWidth : 0
        var split = avoidance > 0 ? (splitIndex > 0 ? splitIndex : Math.floor(children.length / 2)) : children.length
        var itemIndex = 0
        for (var index = 0; index < children.length; ++index) {
            if (children[index] === item)
                itemIndex = index
        }
        var startIndex = itemIndex < split ? 0 : split
        var endIndex = Math.min(startIndex === 0 ? split : children.length, children.length)
        var activeCount = 0
        for (var index = startIndex; index < endIndex; ++index) {
            if (children[index].active)
                activeCount++
        }
        var availableWidth = avoidance > 0 ? (width - avoidance) / 2 : width
        return Math.max(0, Math.min(parent ? parent.keyHeight : 0, availableWidth / Math.max(1, activeCount)))
    }

    function _fixedWidthsOverflow(availableWidth, startIndex, endIndex) {
        var fixedWidth = 0
        var basicButtonCount = 0
        // Bindings run while dynamic rows are still creating or removing keys.
        var lastIndex = Math.min(endIndex, children.length)
        for (var index = startIndex; index < lastIndex; ++index) {
            var child = children[index]
            if (!child || !child.active)
                continue
            if (child.hasOwnProperty("symView") && !child.fixedWidth)
                basicButtonCount++
            else
                fixedWidth += child.implicitWidth
        }
        return fixedWidth > availableWidth
                || (basicButtonCount > 0 && fixedWidth >= availableWidth)
    }

    function _expandToFit(item) {
        if (!fitToWidth || item.hasOwnProperty("maximumWidth"))
            return false
        if (item.hasOwnProperty("symView") && !item.fixedWidth) {
            item.leftPadding = 0
            item.rightPadding = 0
        }
        return true
    }

    function _isExpandingKey(item) {
        if (item.hasOwnProperty("maximumWidth"))
            return false
        return _expandToFit(item)
                || (item.hasOwnProperty("expandingKey") && item.expandingKey)
    }
}
