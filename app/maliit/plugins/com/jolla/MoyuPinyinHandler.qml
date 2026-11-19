import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0
import com.jolla.keyboard 1.0
import xyz.birdzhang.moyu.ime 1.0
import "file:///usr/share/maliit/plugins/com/jolla" as KeyboardComponents
import com.jolla.keyboard 1.0 as KeyboardComponents

KeyboardComponents.InputHandler {
    id: handler
    property string preedit
    property var regLetter : /^[A-Za-z]+$/
    property int candidateSpaceIndex: -1
    property int maxsize: 80

    property bool composingEnabled: pinyinMode && !keyboard.inSymView
    property bool hasMore: composingEnabled && decoder.hasMore
    //mod start
    property bool pinyinMode: true
    property string predictionContext: ""
    property bool awaitingCommitContext: false
    property string commitBaseText: ""
    property int commitBaseCursor: 0
    property string commitExpectedText: ""
    property int commitExpectedCursor: 0

    ConfigurationGroup{
        id: config
        path: "/app/xyz.birdzhang.moyu.ime"
        property int pageSize: 20
        property int fetchSize: 15
    }

    onPinyinModeChanged: {
        if (handler.preedit.length > 0)
            commitPending(true)
        if (keyboard.layout)
            keyboard.layout.pinyinMode = handler.pinyinMode
        reset()
        getPredictions()
    }

    Component.onCompleted: schedulePredictions()

    onPreeditChanged: {
        predictionTimer.stop()
        if (preedit.length > 0)
            awaitingCommitContext = false
    }

    Connections {
        target: MInputMethodQuick
        ignoreUnknownSignals: true
        onCursorPositionChanged: schedulePredictions()
        onSurroundingTextChanged: schedulePredictions()
        onSurroundingTextValidChanged: schedulePredictions()
        onEditorStateUpdate: {
            // This signal follows the complete client state update. The getters
            // can change without distinct property notifications after a commit.
            if (awaitingCommitContext && MInputMethodQuick.surroundingTextValid
                    && (MInputMethodQuick.surroundingText !== commitBaseText
                        || MInputMethodQuick.cursorPosition !== commitBaseCursor))
                awaitingCommitContext = false
            schedulePredictions()
        }
        onActiveChanged: {
            awaitingCommitContext = false
            if (MInputMethodQuick.active)
                schedulePredictions()
        }
        onFocusTargetChanged: {
            awaitingCommitContext = false
            schedulePredictions()
        }
        onInputMethodReset: {
            awaitingCommitContext = false
            schedulePredictions()
        }
        onHiddenTextChanged: getPredictions()
    }

    Timer {
        id: predictionTimer
        interval: 40
        onTriggered: getPredictions()
    }

    onActiveChanged: {
        if (active) {
            decoder.reloadUserDictionaryIfChanged()
            if(pinyinMode){
                getPredictions();
            }
            if (keyboard.layout)
                keyboard.layout.pinyinMode = handler.pinyinMode
            keyboard.shiftKeyPressed = false
            // keyboard.cycleShift();
            keyboard.shiftState = ShiftState.NoShift;
            schedulePredictions()

        } else {
            decoder.flushCache();
            handler.clearPreedit()
        }
    }


    MoyuPinyin{
        id :decoder
        keyboardMode: true
        property var candidates: ListModel { }
        property var moreCandidates: ListModel { }
        property bool hasMore:false
        property bool fetchMany:false
        property int pageSize: config.pageSize;
        property int fetchSize: config.fetchSize;
        property int pred:0
        property int revision: 0
        property string compositionPreedit: ""

        signal candidatesUpdated

        Component.onCompleted:{
            // console.log("pageSize:"+config.pageSize)
            decoder.init();
            //decoder.setUserDictionary(true);
            
        }

        Component.onDestruction: {
            decoder.flushCache();
        }

        function getMoreCandidates(){
            var page = decoder.candidatePage(candidates.count, -1, revision)
            if (!page.ok)
                return
            moreCandidates.clear();
            for (var i = 0; i < page.candidates.length; i++) {
                moreCandidates.append({
                          text: page.candidates[i],
                          type: "partial",
                          segment: 0,
                          candidate: candidates.count + i
                         })
            }
            fetchMany = true
        }

        function populateCandidates(snapshot) {
            if (snapshot.input === undefined)
                return false
            handler.preedit = snapshot.input
            compositionPreedit = snapshot.preedit
            revision = snapshot.revision
            candidates.clear()
            moreCandidates.clear()
            fetchMany = false
            hasMore = snapshot.hasMore
            pred = snapshot.candidates.length
            for (var i = 0; i < snapshot.candidates.length; ++i) {
                candidates.append({text: snapshot.candidates[i], type: "full",
                                   segment: 0, candidate: i})
            }
            candidatesUpdated()
            return snapshot.ok
        }

        function update_candidates(str) {
            return populateCandidates(decoder.searchComposition(str, pageSize))
        }

        function acceptPhrase(index, expanded) {
            var model = expanded ? moreCandidates : candidates
            if (index < 0 || index >= model.count)
                return
            var item = model.get(index)
            if (handler.preedit.length === 0) {
                decoder.recordPrediction(item.text)
                commit(item.text)
                return
            }
            var snapshot = decoder.selectComposition(item.candidate, pageSize, revision)
            if (!populateCandidates(snapshot))
                return
            var text = snapshot.commit
            if (text.length > 0) {
                commit(text)
            } else {
                MInputMethodQuick.sendPreedit(compositionPreedit)
            }
        }
    }

    topItem: Component {
        Column{
            id: topItem
            width: parent  ? parent.width : 0
            KeyboardComponents.TopItem {
                visible: false
                width: parent.width
                Rectangle {
                    id: background
                    anchors.fill: parent
                    color: Theme.highlightBackgroundColor
                    opacity: .05
                }

                Label {
                    anchors.centerIn: parent
                    horizontalAlignment: Text.AlignHCenter
                    text: preedit
                }

                MouseArea {
                    anchors.fill: parent
                }
            }
            KeyboardComponents.TopItem {
                id: listTopItem
                width: parent.width
                SilicaListView {
                    id: listView
                    model: decoder.candidates
                    orientation: ListView.Horizontal
                    width: parent.width
                    height: parent.height
                    boundsBehavior: ((!keyboard.expandedPaste && Clipboard.hasText) || decoder.hasMore) ? Flickable.DragOverBounds : Flickable.StopAtBounds
                    header: pasteComponent

                    footer: Item {
                        width: decoder.hasMore ? 20 : 0
                        height: listView.height
                        visible: decoder.hasMore

                        Image {
                            source: "image://theme/icon-lock-more"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Image {
                            source: "image://theme/icon-lock-more?" + Theme.highlightColor
                            anchors.verticalCenter: parent.verticalCenter
                            opacity: listView.dragging && listView.atXEnd ? 1.0 : 0.0
                            Behavior on opacity { FadeAnimation {} }
                        }

                    }

                    delegate: BackgroundItem {
                        id: backGround
                        // onClicked:  accept(model.index)
                        onClicked: selectPhrase(model.text, model.index, false)
                        width: candidateText.width + Theme.paddingLarge * 2
                        height: listTopItem.height

                        Text {
                            id: candidateText
                            anchors.centerIn: parent
                            color: (backGround.down || index === 0) ? Theme.highlightColor : Theme.primaryColor
                            font { pixelSize: Theme.fontSizeSmall; family: Theme.fontFamily }
                            text: model.text
                        }
                    }
                    onCountChanged: {
                        positionViewAtBeginning();
                    }
                    onDraggingChanged: {
                        if (!dragging) {
                            if (!keyboard.expandedPaste && contentX < -(headerItem.width + Theme.paddingLarge)) {
                                keyboard.expandedPaste = true
                                positionViewAtBeginning()
                            } else if (atXEnd && decoder.hasMore) {
                                decoder.fetchMany=true
                                decoder.getMoreCandidates()

                            }
                        }
                    }

                    Binding on flickDeceleration {
                        when: phraseDialog.visible
                        value: 1000000
                    }

                    Connections {
                        target: decoder
                        onCandidatesUpdated: listView.positionViewAtBeginning()
                    }

                    Connections {
                        target: Clipboard
                        onTextChanged: {
                            if (Clipboard.hasText) {
                                // need to have updated width before repositioning view
                                positionerTimer.restart()
                            }
                        }
                    }

                    Timer {
                        id: positionerTimer
                        interval: 10
                        onTriggered: listView.positionViewAtBeginning()
                    }

                }
            }
        }
    }

    Component {
        id: pasteComponent
        Loader {
            id: horizontalPaste
            visible: Clipboard.hasText && item !== null
            height: parent ? parent.height : 0
            sourceComponent: handler.loadPasteButton("PasteButton.qml", horizontalPaste)

            Connections {
                target: horizontalPaste.item
                onClicked: {
                    if (preedit.length > 0)
                        commitPending(true)
                    commit(Clipboard.text)
                    keyboard.expandedPaste = false
                }
            }
        }
    }

    function loadPasteButton(fileName, pasteLoader) {
        // Sailfish 5.2 exports these components through the keyboard module.
        // Older releases keep the QML files in the keyboard directories.
        try {
            return Qt.createQmlObject("import QtQuick 2.0\nimport com.jolla.keyboard 1.0\n"
                                      + "Component { " + fileName.slice(0, -4) + " {} }",
                                      pasteLoader, "Moyu" + fileName)
        } catch (error) {
            // Resolve the legacy files below when the module lacks this type.
        }
        var component = Qt.createComponent("file:///usr/share/maliit/plugins/com/jolla/handlers/" + fileName,
                                           Component.PreferSynchronous, pasteLoader)
        if (component.status === Component.Error)
            component = Qt.createComponent("file:///usr/share/maliit/plugins/com/jolla/" + fileName,
                                           Component.PreferSynchronous, pasteLoader)
        return component.status === Component.Ready ? component : null
    }




    onComposingEnabledChanged: {
        if (preedit.length > 0)
            commitPending(true)
        getPredictions()
    }


    Rectangle {
        id: phraseDialog

        parent: keyboard
        z: 1
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: keyboard.currentLayoutHeight

        visible: decoder.fetchMany
        color: Theme.highlightDimmerColor
        opacity: 0.9
        clip: true

        MultiPointTouchArea {
            // prevent events leaking below
            anchors.fill: parent
            z: -1
        }

        SilicaFlickable {
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: closeButton.left

            contentHeight: gridView.height

            Flow {
                id: gridView

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.paddingMedium

                property real cellWidth: width / (keyboard.portraitMode ? 5 : 8)

                Repeater {
                    model: phraseDialog.visible ? decoder.moreCandidates : 0
                    delegate: BackgroundItem {
                        id: gridItemBackground
                        height: Theme.itemSizeSmall
                        width: Math.ceil((gridText.contentWidth + 2*Theme.paddingMedium) / gridView.cellWidth)* gridView.cellWidth
                        onClicked: selectPhrase(model.text, model.index, true)

                        Text {
                            id: gridText
                            anchors.verticalCenter: parent.verticalCenter
                            x: Theme.paddingMedium
                            color: gridItemBackground.down ? Theme.highlightColor : Theme.primaryColor
                            font { pixelSize: Theme.fontSizeSmall; family: Theme.fontFamily }
                            text: model.text
                        }
                    }
                }
            }
        }

        IconButton {
            id: closeButton

            anchors {
                right: parent.right
                top: parent.top
                topMargin: Theme.paddingSmall
            }
            opacity: 0.6
            icon.source: "image://theme/icon-close-vkb"
            onClicked: decoder.fetchMany = false
        }
    }


    verticalItem: Component {
        Item {
            id: verticalContainer

            property int inactivePadding: Theme.paddingMedium

            SilicaListView {
                id: verticalList

                model: composingEnabled ? decoder.candidates : 0
                anchors.fill: parent
                clip: true
                boundsBehavior: handler.hasMore ? Flickable.DragOverBounds : Flickable.StopAtBounds
                header: Component {
                    Loader {
                        id: verticalPaste
                        visible: Clipboard.hasText && item !== null
                        width: verticalList.width
                        height: visible ? geometry.keyHeightLandscape : 0
                        sourceComponent: handler.loadPasteButton("PasteButtonVertical.qml", verticalPaste)

                        onLoaded: {
                            item.width = Qt.binding(function() { return verticalPaste.width })
                            item.popupParent = verticalContainer
                            item.popupAnchor = 2
                        }

                        Connections {
                            target: verticalPaste.item
                            onClicked: {
                                if (preedit.length > 0)
                                    commitPending(true)
                                commit(Clipboard.text)
                            }
                        }
                    }
                }

                footer: Item {
                    width:  verticalList.width
                    height: geometry.keyHeightLandscape / 2
                    visible: handler.hasMore

                    Image {
                        id: moreIcon
                        source: "image://theme/icon-lock-more"
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.bottom
                        anchors.verticalCenterOffset: -Theme.paddingSmall
                    }
                    Image {
                        source: "image://theme/icon-lock-more?" + Theme.highlightColor
                        anchors.centerIn: moreIcon
                        opacity: verticalList.dragging && verticalList.atYEnd ? 1.0 : 0.0
                        Behavior on opacity { FadeAnimation {} }
                    }
                }



                delegate: BackgroundItem {
                    id: background
                    onClicked: selectPhrase(model.text, model.index, false)
                    width: parent.width
                    // height: geometry.keyHeightLandscape // assuming landscape!
                    height: geometry.keyHeightLandscape * candidateText.lineCount

                    Text {
                        id: candidateText
                        width: background.width
                        horizontalAlignment: Text.AlignHCenter
                        anchors.verticalCenter: parent.verticalCenter
                        // color: index === 0 ? Theme.highlightColor : Theme.primaryColor
                        // font.pixelSize: Theme.fontSizeSmall
                        // fontSizeMode: Text.HorizontalFit
                        color: (background.down || (index === 0 && preedit.length > 0))
                               ? Theme.highlightColor : Theme.primaryColor
                        font { pixelSize: Theme.fontSizeSmall; family: Theme.fontFamily }
                        text: model.text
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                    }
                }

                onDraggingChanged: {
                    if (!dragging && atYEnd && decoder.hasMore) {
                        decoder.getMoreCandidates()
                    }
                }

                Binding on flickDeceleration {
                    when: phraseDialog.visible
                    value: 1000000
                }

                Connections {
                    target: decoder
                    onCandidatesUpdated: {
                        if (!clipboardChange.running) {
                            verticalList.positionViewAtIndex(0, ListView.Beginning)
                        }
                    }
                }
                Connections {
                    target: Clipboard
                    onTextChanged: {
                        verticalList.positionViewAtBeginning()
                        clipboardChange.restart()
                    }
                }
                Timer {
                    id: clipboardChange
                    interval: 1000
                }

                MouseArea {
                    height: parent.height
                    width: verticalContainer.inactivePadding
                }

                MouseArea {
                    height: parent.height
                    width: verticalContainer.inactivePadding
                    anchors.right: parent.right
                }
            }
        }
    }







    function handleKeyClick() {
        var handled = false
        keyboard.expandedPaste = false
        if (!composingEnabled && pressedKey.text.length > 0)
            return false
        if (pressedKey.key === Qt.Key_Space) {
            if (preedit !== "") {
                if (decoder.candidates.count > 0)
                    accept(0)
                else
                    commitPending(true)

                if (keyboard.shiftState !== ShiftState.LockedShift) {
                    keyboard.shiftState = ShiftState.NoShift
                }


                handled = true
            } else {
                commit(" ")
                handled = true
            }
        } else if (pressedKey.key === Qt.Key_Return) {
            if (preedit !== "") {
                commitPending(true)

                if (keyboard.shiftState !== ShiftState.LockedShift) {
                    keyboard.shiftState = ShiftState.NoShift
                }

                handled = true
            }
        } else if (pressedKey.key === Qt.Key_Backspace && preedit !== "") {

            decoder.populateCandidates(decoder.backspaceComposition(decoder.pageSize))
            MInputMethodQuick.sendPreedit(decoder.compositionPreedit)
            if (preedit.length === 0)
                schedulePredictions()

            if (keyboard.shiftState !== ShiftState.LockedShift) {
                keyboard.shiftState = ShiftState.NoShift
            }

            handled = true
        } else if (pressedKey.key === Qt.Key_Backspace && preedit == "") {
            awaitingCommitContext = false
            schedulePredictions()
        } else if (pressedKey.key === Qt.Key_Home) {
            MInputMethodQuick.sendKey(Qt.Key_Home, 0, "", Maliit.KeyClick)
        } else if (pressedKey.key === Qt.Key_End) {
            MInputMethodQuick.sendKey(Qt.Key_End, 0, "", Maliit.KeyClick)
        } else if (pressedKey.key === Qt.Key_Up) {
            MInputMethodQuick.sendKey(Qt.Key_Up, 0, "", Maliit.KeyClick)
        } else if (pressedKey.key === Qt.Key_Down) {
            MInputMethodQuick.sendKey(Qt.Key_Down, 0, "", Maliit.KeyClick)
        } else if (pressedKey.key === Qt.Key_Left) {
            MInputMethodQuick.sendKey(Qt.Key_Left, 0, "", Maliit.KeyClick)
        } else if (pressedKey.key === Qt.Key_Right) {
            MInputMethodQuick.sendKey(Qt.Key_Right, 0, "", Maliit.KeyClick)
        } else if (pressedKey.text.length !== 0) {
            if (!regLetter.test(pressedKey.text) &&
                    !(pressedKey.text === "'" && preedit.length > 0)) {
                if (preedit.length > 0)
                    commitPending(false)
                commit(pressedKey.text)
            }else{

                preedit = preedit + pressedKey.text
                decoder.update_candidates(preedit)

                if (keyboard.shiftState !== ShiftState.LockedShift) {
                    keyboard.shiftState = ShiftState.NoShift
                }

                MInputMethodQuick.sendPreedit(decoder.compositionPreedit || preedit)
            }
            handled = true
        }
        

        return handled
    }

    function accept(index) {
        decoder.acceptPhrase(index, false)
    }

    function selectPhrase(phrase, index, expanded) {
        decoder.acceptPhrase(index, expanded === undefined ? decoder.fetchMany : expanded)
    }


    function schedulePredictions() {
        if (active && preedit.length === 0)
            predictionTimer.restart()
    }

    function predictionHistory() {
        var text = MInputMethodQuick.surroundingText
        var cursor = MInputMethodQuick.cursorPosition
        if (awaitingCommitContext) {
            // Clients can report the text and cursor in separate notifications.
            var unchanged = text === commitBaseText && cursor === commitBaseCursor
            var textOnly = text === commitExpectedText && cursor === commitBaseCursor
            var cursorOnly = text === commitBaseText && cursor === commitExpectedCursor
            if (!MInputMethodQuick.surroundingTextValid || unchanged || textOnly || cursorOnly)
                return predictionContext
            awaitingCommitContext = false
        }
        return MInputMethodQuick.surroundingTextValid
                ? text.substring(0, Math.max(0, Math.min(cursor, text.length))) : ""
    }

    function getPredictions() {
        if (preedit.length > 0)
            return
        var enabled = active && composingEnabled && !MInputMethodQuick.hiddenText
        if (!enabled)
            awaitingCommitContext = false
        var history = enabled ? predictionHistory() : ""
        var fetchSize = history.length === 0 ? 16 : decoder.fetchSize
        var predictions = enabled ? decoder.predictionList(history, fetchSize) : []
        predictionContext = history
        decoder.candidates.clear()
        decoder.moreCandidates.clear()
        decoder.fetchMany = false
        for (var i = 0; i < predictions.length; ++i) {
            decoder.candidates.append({text: predictions[i], type: "prediction",
                                       segment: 0, candidate: i})
        }
        if (predictions.length > 0) {
            decoder.candidates.append({text: " ", type: "prediction", segment: 0,
                                       candidate: predictions.length})
        }
        decoder.hasMore = false
        decoder.candidatesUpdated()
    }

    function isInputCharacter(character) {
        return "\'-".indexOf(character) >= 0
    }
    function applyPrediction(replacement, index) {
        handler.preedit = "";
        if(pressedKey.text && pressedKey.text.length > 0){
            pressedKey.text = "";
        }

        replacement = replacement + " "
        candidateSpaceIndex = MInputMethodQuick.surroundingTextValid
                ? MInputMethodQuick.cursorPosition + replacement.length : -1
        commit(replacement);
    }

    function selectPhraseAndShrink(phrase, index) {
        selectPhrase(phrase, index)
        decoder.fetchMany = false
    }

    function clearPreedit(){
        if (preedit.length > 0)
            commitPending(true)
        reset()
    }
    function reset() {
        predictionTimer.stop()
        awaitingCommitContext = false
        predictionContext = ""
        commitBaseText = ""
        commitExpectedText = ""
        decoder.candidates.clear()
        preedit = ""
        decoder.resetSearch()
        decoder.candidatesUpdated()
        decoder.moreCandidates.clear()
        decoder.fetchMany = false
        decoder.hasMore = false
        if (keyboard.shiftState !== ShiftState.LockedShift) {
            keyboard.shiftState = ShiftState.NoShift
        }

        schedulePredictions()
    }

    function commit(text) {
        var history = predictionHistory()
        var baseText = MInputMethodQuick.surroundingText
        var baseCursor = MInputMethodQuick.cursorPosition
        var context = awaitingCommitContext ? commitExpectedText : baseText
        var cursor = awaitingCommitContext ? commitExpectedCursor : baseCursor
        cursor = Math.max(0, Math.min(cursor, context.length))
        reset()
        predictionContext = history + text
        commitBaseText = baseText
        commitBaseCursor = baseCursor
        commitExpectedText = context.substring(0, cursor) + text + context.substring(cursor)
        commitExpectedCursor = cursor + text.length
        awaitingCommitContext = true
        MInputMethodQuick.sendCommit(text)
        getPredictions()
        schedulePredictions()
    }

    function commitPending(raw) {
        var text = decoder.commitComposition(raw)
        commit(text.length > 0 ? text : preedit)
    }
}

