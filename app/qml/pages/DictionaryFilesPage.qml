import QtQuick 2.0
import Sailfish.Silica 1.0
import xyz.birdzhang.moyu.ime 1.0

Page {
    id: page
    property bool busy: false
    property bool initialized: false
    property string statusMessage: ""
    property string builtInVersion: ""
    property string fileDirectory: ""
    property string importPath: ""

    function refreshFiles() {
        try {
            var info = JSON.parse(pinyin.dictionaryFiles())
            if (info.error.length > 0) {
                statusMessage = info.error
                return
            }
            fileModel.clear()
            for (var i = 0; i < info.files.length; ++i) {
                fileModel.append({
                    "fileName": info.files[i].name,
                    "fileEnabled": info.files[i].enabled,
                    "entryCount": info.files[i].entries
                })
            }
            builtInVersion = info.builtInVersion
            fileDirectory = info.directory
            initialized = true
        } catch (error) {
            statusMessage = "读取词典文件清单失败"
        }
    }

    function operate(operation, value, enabled) {
        if (busy)
            return
        busy = true
        statusMessage = "正在处理词典文件…"
        pinyin.manageDictionaryFile(operation, value || "", enabled === true)
    }

    function chooseFile() {
        var picker = pageStack.push("Sailfish.Pickers.FilePickerPage", {
            "nameFilters": ["*.dict.yaml", "*.txt", "*.tsv"]
        })
        picker.selectedContentPropertiesChanged.connect(function() {
            var selected = picker.selectedContentProperties
            if (selected && selected.filePath) {
                importPath = selected.filePath
                operate("importDictionaryFile", importPath, true)
            }
        })
    }

    onStatusChanged: {
        if (status === PageStatus.Active && !busy)
            initializeTimer.start()
    }
    Component.onCompleted: initializeTimer.start()

    Timer {
        id: initializeTimer
        interval: 0
        onTriggered: page.refreshFiles()
    }

    ListModel { id: fileModel }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: fileModel

        PullDownMenu {
            MenuItem {
                text: "导入词典文件"
                enabled: !page.busy
                onClicked: page.chooseFile()
            }
            MenuItem {
                text: "导出用户词典"
                enabled: !page.busy
                onClicked: page.operate("exportUserDictionary", "", false)
            }
        }

        header: Column {
            width: list.width
            spacing: Theme.paddingMedium

            PageHeader { title: "Rime 词典文件" }
            SectionHeader { text: "内置词库" }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: "雾凇拼音 · " + page.builtInVersion + "\n包含字表、基础、扩展、腾讯和杂项词库。"
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
            }
            SectionHeader { text: "导入扩展词典" }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: "支持 UTF-8 的 .dict.yaml、.txt 和 .tsv 文件，每行用 Tab 分隔词条、完整拼音和可选词频。Rime 主词典请分别导入它引用的子词典。"
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "选择词典文件"
                enabled: !page.busy
                onClicked: page.chooseFile()
            }
            TextField {
                width: parent.width
                text: page.importPath
                onTextChanged: page.importPath = text
                label: "文件路径"
                placeholderText: "/home/defaultuser/Documents/example.dict.yaml"
                enabled: !page.busy
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                EnterKey.text: "导入"
                EnterKey.onClicked: page.operate("importDictionaryFile", page.importPath, true)
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "从路径导入"
                enabled: !page.busy && page.importPath.length > 0
                onClicked: page.operate("importDictionaryFile", page.importPath, true)
            }
            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                size: BusyIndicatorSize.Medium
                running: page.busy
                visible: running
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.statusMessage.length > 0
                text: page.statusMessage
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
            }
            SectionHeader { text: "已导入词典（" + fileModel.count + "）" }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: fileModel.count === 0
                text: "暂无扩展词典。导入后自动编译并启用。"
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
            }
        }

        delegate: ListItem {
            id: item
            property string dictionaryName: fileName
            property bool dictionaryEnabled: fileEnabled
            width: list.width
            contentHeight: Theme.itemSizeLarge
            enabled: !page.busy

            Column {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                anchors.verticalCenter: parent.verticalCenter
                Label {
                    width: parent.width
                    text: item.dictionaryName
                    truncationMode: TruncationMode.Fade
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    text: (item.dictionaryEnabled ? "已启用" : "已停用") + " · " + entryCount + " 个词条"
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryColor
                }
            }
            menu: ContextMenu {
                MenuItem {
                    text: item.dictionaryEnabled ? "停用" : "启用"
                    onClicked: page.operate("setDictionaryFileEnabled", item.dictionaryName, !item.dictionaryEnabled)
                }
                MenuItem {
                    text: "导出"
                    onClicked: page.operate("exportDictionaryFile", item.dictionaryName, false)
                }
                MenuItem {
                    text: "删除"
                    onClicked: item.remorseAction("删除词典文件", function() {
                        page.operate("removeDictionaryFile", item.dictionaryName, false)
                    })
                }
            }
        }
        footer: Label {
            x: Theme.horizontalPageMargin
            width: list.width - 2 * x
            text: "文件目录\n" + page.fileDirectory
            wrapMode: Text.WrapAnywhere
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeExtraSmall
        }
        VerticalScrollDecorator {}
    }

    MoyuPinyin {
        id: pinyin
        onDictionaryOperationFinished: {
            page.busy = false
            try {
                var response = JSON.parse(result)
                if (response.error.length > 0) {
                    page.statusMessage = response.error
                } else {
                    page.refreshFiles()
                    page.statusMessage = response.path.length > 0
                            ? "已导出到\n" + response.path : "词典文件已更新并生效"
                }
            } catch (error) {
                page.statusMessage = "词典文件处理结果读取失败"
            }
        }
    }
}
