import QtQuick 2.6
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0
import com.jolla.keyboard 1.0
import xyz.birdzhang.moyu.ime 1.0

Item {
    id: root
    width: 1080
    height: 2340
    property bool ready: false
    property alias inputMethod: simulatedInputMethod
    property QtObject clipboard: Clipboard
    property alias layoutDefinition: config.keyboardLayout
    property alias portrait: keyboard.portraitMode
    property alias split: keyboard.splitEnabled
    property alias symbolMode: keyboard.inSymView
    property alias expandedPaste: keyboard.expandedPaste
    property alias resetShiftCount: keyboard.resetShiftCount
    property Item handler: handlerLoader.item
    property Item loadedLayout: layoutLoader.item
    property Item verticalCandidates: verticalLoader.item

    QtObject {
        id: simulatedInputMethod
        property bool active: true
        property bool surroundingTextValid: true
        property string surroundingText: ""
        property int cursorPosition: 0
        property string committedText: ""
        property string displayedPreedit: ""
        property bool contextEchoEnabled: true
        property bool contextPending: false
        property string pendingSurroundingText: ""
        property int pendingCursorPosition: 0
        property bool hiddenText: false
        property bool predictionEnabled: true
        property int contentType: 0
        property int appOrientation: 0
        property int enterKeyType: 0
        property string enterKeyLabel: ""
        property bool enterKeyEnabled: true
        property int inputMethodHints: 0
        property string inputMethodQuery: ""
        property QtObject actionKeyOverride: QtObject {
            property string icon: ""
            property string label: "Enter"
            property bool enabled: true
            property bool highlighted: false
        }

        signal editorStateUpdate()
        signal focusTargetChanged(bool activeEditor)
        signal inputMethodReset()

        function sendCommit(text) {
            committedText += text
            displayedPreedit = ""
            var context = contextPending ? pendingSurroundingText : surroundingText
            var cursor = contextPending ? pendingCursorPosition : cursorPosition
            cursor = Math.max(0, Math.min(cursor, context.length))
            pendingSurroundingText = context.substring(0, cursor) + text + context.substring(cursor)
            pendingCursorPosition = cursor + text.length
            contextPending = true
            if (contextEchoEnabled)
                flushContext()
        }
        function sendPreedit(text) { displayedPreedit = text }
        function sendKey(key, modifiers, text, eventType) {
            if (key === Qt.Key_Backspace && cursorPosition > 0) {
                surroundingText = surroundingText.substring(0, cursorPosition - 1)
                        + surroundingText.substring(cursorPosition)
                cursorPosition--
                editorStateUpdate()
            }
        }
        function flushContext() {
            surroundingText = pendingSurroundingText
            cursorPosition = pendingCursorPosition
            contextPending = false
            editorStateUpdate()
        }
    }

    ConfigurationGroup {
        id: config
        path: "/app/xyz.birdzhang.moyu.ime"
        property string keyboardLayout: ""
    }

    QtObject {
        id: geometry
        property int keyHeightPortrait: 180
        property int keyHeightLandscape: 130
        property int punctuationKeyPortait: 97
        property int punctuationKeyLandscape: 180
        property int shiftKeyWidthPortrait: 162
        property int shiftKeyWidthPortraitNarrow: 135
        property int shiftKeyWidthLandscape: 247
        property int shiftKeyWidthLandscapeNarrow: 173
        property int shiftKeyWidthLandscapeSplit: 173
        property int functionKeyWidthPortrait: 214
        property int functionKeyWidthLandscape: 326
        property int symbolKeyWidthPortraitNarrow: 214
        property int symbolKeyWidthLandscapeNarrow: 326
        property int symbolKeyWidthLandscapeNarrowSplit: 225
        property int middleBarWidth: 270
        property int cornerPadding: 0
        property int keyRadius: 8
        property int popperRadius: 8
        property int clearPasteMargin: 20
        property int clearPasteTouchDelta: 10
    }

    QtObject {
        id: canvas
        property int activeIndex: 0
        property QtObject layoutModel: QtObject {
            property var inputHandlers: ({"MoyuPinyinHandler.qml": handlerLoader.item})
            property bool inputHandlersInitialized: true
        }
    }

    Item {
        id: keyboard
        width: root.width
        height: root.height
        property Item layout: layoutLoader.item
        property bool portraitMode: true
        property bool splitEnabled: false
        property bool inSymView: false
        property bool inSymView2: false
        property bool pasteEnabled: Clipboard.hasText && !simulatedInputMethod.hiddenText
        property bool expandedPaste: false
        property bool shiftKeyPressed: false
        property bool moving: false
        property bool hasHorizontalPadding: false
        property bool chineseOverrideForEnter: false
        property bool isShiftLocked: false
        property bool autocaps: false
        property color popperBackgroundColor: "black"
        property int shiftState: ShiftState.NoShift
        property int characterKeyCounter: 0
        property int resetShiftCount: 0
        property int currentLayoutHeight: layout ? layout.height : 0
        property Item pasteInputHandler: null
        function cycleShift() {}
        function resetShift() {
            resetShiftCount++
            shiftState = ShiftState.NoShift
        }
    }

    Item {
        id: simulatedKey
        property string text: ""
        property int key: Qt.Key_unknown
        property int keyType: KeyType.CharacterKey
        property bool repeat: false
    }

    Loader {
        id: handlerLoader
        active: root.ready
        source: testHandlerUrl
        onLoaded: item.active = true
    }

    Loader {
        id: layoutLoader
        parent: keyboard
        width: parent.width
        active: root.ready && handlerLoader.status === Loader.Ready
        source: testLayoutUrl
    }

    Loader {
        id: verticalLoader
        parent: keyboard
        sourceComponent: keyboard.layout && keyboard.layout.splitActive
                         ? root.handler.verticalItem : null
        width: geometry.middleBarWidth
        height: keyboard.currentLayoutHeight
        visible: item !== null && !keyboard.moving
    }

    function click(text, key) {
        simulatedKey.text = text
        simulatedKey.key = key
        handler._handleKeyClick(simulatedKey)
        handler.pressedKey = null
    }

    function clickPaste(button) {
        button.clicked(null)
    }

    function setContext(text, cursor) {
        simulatedInputMethod.contextPending = false
        simulatedInputMethod.surroundingText = text
        simulatedInputMethod.cursorPosition = cursor
        simulatedInputMethod.committedText = ""
        simulatedInputMethod.editorStateUpdate()
    }

    function defaultDefinition() {
        return JSON.stringify(MoyuLayout.defaultLayout())
    }

    function clickCharacter(caption) {
        for (var rowIndex = 0; rowIndex < loadedLayout.children.length; ++rowIndex) {
            var row = loadedLayout.children[rowIndex]
            if (!row.visible || !row.hasOwnProperty("followRowHeight"))
                continue
            for (var keyIndex = 0; keyIndex < row.children.length; ++keyIndex) {
                var key = row.children[keyIndex]
                if (key.active && key.caption === caption) {
                    handler._handleKeyClick(key)
                    handler.pressedKey = null
                    return true
                }
            }
        }
        return false
    }

    function snapshot() {
        var rows = []
        for (var rowIndex = 0; rowIndex < loadedLayout.children.length; ++rowIndex) {
            var row = loadedLayout.children[rowIndex]
            if (!row.visible || !row.hasOwnProperty("followRowHeight"))
                continue
            var keys = []
            for (var keyIndex = 0; keyIndex < row.children.length; ++keyIndex) {
                var key = row.children[keyIndex]
                if (!key.active)
                    continue
                keys.push({caption: key.caption, x: key.x, y: key.y, width: key.width,
                           height: key.height, text: key.text})
            }
            rows.push({x: row.x, y: row.y, width: row.width, height: row.height, keys: keys})
        }
        return JSON.stringify(rows)
    }
}
