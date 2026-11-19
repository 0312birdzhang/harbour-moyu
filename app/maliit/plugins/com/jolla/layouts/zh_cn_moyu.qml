/*
 * Copyright (C) 2012-2013 Jolla ltd and/or its subsidiary(-ies). All rights reserved.
 *
 * Contact: Pekka Vuorela <pekka.vuorela@jollamobile.com>
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 * Neither the name of Jolla Ltd nor the names of its contributors may be
 * used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

import QtQuick 2.0
import Nemo.Configuration 1.0
import xyz.birdzhang.moyu.ime 1.0
import ".."

KeyboardLayout {
    id: pinyinLayout
    type: "china_pinyin"
    inputMode: "simplified"
    property bool pinyinMode: true
    splitSupported: true

    property var customRows: []
    property bool customRowsActive: customRows.length > 0

    ConfigurationGroup {
        id: layoutConfig
        path: "/app/xyz.birdzhang.moyu.ime"
        property string keyboardLayout: ""
        onKeyboardLayoutChanged: applyLayoutTimer.restart()
    }

    Component.onCompleted: applyLayoutTimer.start()

    Timer {
        id: applyLayoutTimer
        interval: 0
        onTriggered: pinyinLayout.applyCustomLayout()
    }

    Timer {
        id: relayoutTimer
        interval: 0
        onTriggered: pinyinLayout.updateSizes()
    }

    Component { id: customRowComponent; KeyboardRow {} }
    Component {
        id: customCharacterComponent
        CharacterKey {
            captionShifted: caption.toUpperCase()
            accents: MoyuLayout.accentsFor(caption, false)
            accentsShifted: MoyuLayout.accentsFor(caption, true)
        }
    }
    Component { id: customShiftComponent; ShiftKey {} }
    Component { id: customBackspaceComponent; BackspaceKey {} }
    Component {
        id: customSymbolComponent
        SymbolKey { caption: keyboard.inSymView ? "拼音" : "符号" }
    }
    Component { id: customCommaComponent; ChineseContextAwareCommaKey {} }
    Component { id: customPeriodComponent; ChineseContextAwarePeriodKey {} }
    Component { id: customSpaceComponent; SpacebarKey {} }
    Component {
        id: customSplitSpaceComponent
        SpacebarKey {
            active: pinyinLayout.splitActive
            languageLabel: ""
        }
    }
    Component { id: customEnterComponent; EnterKey {} }

    function discardRows(rows) {
        for (var index = 0; index < rows.length; ++index) {
            rows[index].visible = false
            rows[index].separateButtonSizes = true
            rows[index].destroy()
        }
    }

    function applyCustomLayout() {
        var newRows = []
        try {
            var definition = MoyuLayout.decode(layoutConfig.keyboardLayout)
            if (definition) {
                var factories = {character: customCharacterComponent, shift: customShiftComponent,
                                 backspace: customBackspaceComponent, symbol: customSymbolComponent,
                                 comma: customCommaComponent, period: customPeriodComponent,
                                 space: customSpaceComponent, splitSpace: customSplitSpaceComponent,
                                 enter: customEnterComponent}
                for (var rowIndex = 0; rowIndex < definition.rows.length; ++rowIndex) {
                    var rowDefinition = definition.rows[rowIndex]
                    var row = customRowComponent.createObject(pinyinLayout, {
                        visible: false,
                        splitIndex: rowDefinition.splitIndex === undefined ? -1 : rowDefinition.splitIndex,
                        separateButtonSizes: rowDefinition.separateButtonSizes || false
                    })
                    if (!row)
                        throw new Error("无法创建键盘行")
                    newRows.push(row)
                    for (var keyIndex = 0; keyIndex < rowDefinition.keys.length; ++keyIndex) {
                        var keyDefinition = rowDefinition.keys[keyIndex]
                        var key = factories[keyDefinition.type].createObject(row,
                                                MoyuLayout.keyProperties(keyDefinition))
                        if (!key)
                            throw new Error("无法创建按键")
                    }
                }
            }
        } catch (error) {
            console.warn("Moyu custom layout", error.toString())
            discardRows(newRows)
            newRows = []
        }
        discardRows(customRows)
        customRows = newRows
        for (var index = 0; index < customRows.length; ++index)
            customRows[index].visible = true
        relayoutTimer.restart()
    }

    KeyboardRow {
        visible: !pinyinLayout.customRowsActive
        separateButtonSizes: pinyinLayout.customRowsActive
        CharacterKey { caption: "q"; captionShifted: "Q"; symView: "1"; symView2: "@" }
        CharacterKey { caption: "w"; captionShifted: "W"; symView: "2"; symView2: "/" }
        CharacterKey { caption: "e"; captionShifted: "E"; symView: "3"; symView2: "\\"; accents: "eēéěè"; accentsShifted: "EĒÉĚÈ" }
        CharacterKey { caption: "r"; captionShifted: "R"; symView: "4"; symView2: "~" }
        CharacterKey { caption: "t"; captionShifted: "T"; symView: "5"; symView2: "^" }
        CharacterKey { caption: "y"; captionShifted: "Y"; symView: "6"; symView2: "_" }
        CharacterKey { caption: "u"; captionShifted: "U"; symView: "7"; symView2: "¥"; accents: "uūúǔù"; accentsShifted: "UŪÚǓÙ" }
        CharacterKey { caption: "i"; captionShifted: "I"; symView: "8"; symView2: "€"; accents: "iīíǐì"; accentsShifted: "IĪÍǏÌ" }
        CharacterKey { caption: "o"; captionShifted: "O"; symView: "9"; symView2: "$"; accents: "oōóǒò"; accentsShifted: "OŌÓǑÒ" }
        CharacterKey { caption: "p"; captionShifted: "P"; symView: "0"; symView2: "£" }
    }

    KeyboardRow {
        visible: !pinyinLayout.customRowsActive
        separateButtonSizes: pinyinLayout.customRowsActive
        splitIndex: 5

        CharacterKey { caption: "a"; captionShifted: "A"; symView: "*"; symView2: "§"; accents: "aāáǎà"; accentsShifted: "AĀÁǍÀ" }
        CharacterKey { caption: "s"; captionShifted: "S"; symView: "#"; symView2: "=" }
        CharacterKey { caption: "d"; captionShifted: "D"; symView: "+"; symView2: "〈" }
        CharacterKey { caption: "f"; captionShifted: "F"; symView: "-"; symView2: "〉" }
        CharacterKey { caption: "g"; captionShifted: "G"; symView: "（"; symView2: "(" }
        CharacterKey { caption: "h"; captionShifted: "H"; symView: "）"; symView2: ")" }
        CharacterKey { caption: "j"; captionShifted: "J"; symView: "—"; symView2: "《" }
        CharacterKey { caption: "k"; captionShifted: "K"; symView: "…"; symView2: "》" }
        CharacterKey { caption: "l"; captionShifted: "L"; symView: "%"; symView2: "&" }
    }

    KeyboardRow {
        visible: !pinyinLayout.customRowsActive
        separateButtonSizes: pinyinLayout.customRowsActive
        ShiftKey {}

        CharacterKey { caption: "z"; captionShifted: "Z"; symView: "“"; symView2: "'" }
        CharacterKey { caption: "x"; captionShifted: "X"; symView: "”"; symView2: "\"" }
        CharacterKey { caption: "c"; captionShifted: "C"; symView: "；"; symView2: ";" }
        CharacterKey { caption: "v"; captionShifted: "V"; symView: "："; symView2: ":"; accents: "vüǖǘǚǜ"; accentsShifted: "VÜǕǗǙǛ" }
        CharacterKey { caption: "b"; captionShifted: "B"; symView: "、"; symView2: "·" }
        CharacterKey { caption: "n"; captionShifted: "N"; symView: "！"; symView2: "!" }
        CharacterKey { caption: "m"; captionShifted: "M"; symView: "？"; symView2: "?" }

        BackspaceKey {}
    }

    KeyboardRow {
        visible: !pinyinLayout.customRowsActive
        separateButtonSizes: pinyinLayout.customRowsActive
        splitIndex: 3
        SymbolKey {
            caption: keyboard.inSymView ? "拼音" : "符号" // symbols/pinyin
        }

        ChineseContextAwareCommaKey {}

        SpacebarKey {}
        SpacebarKey {
            active: splitActive
            languageLabel: ""
        }

        ChineseContextAwarePeriodKey {}

        EnterKey {}
    }
}
