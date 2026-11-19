import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0
import xyz.birdzhang.moyu.ime 1.0

Page {
    id: page
    property string statusMessage: ""

    ConfigurationGroup {
        id: config
        path: "/app/xyz.birdzhang.moyu.ime"
        property string keyboardLayout: ""
    }

    Component.onCompleted: {
        jsonField.text = config.keyboardLayout.length > 0
                ? config.keyboardLayout : JSON.stringify(MoyuLayout.defaultLayout(), null, 2)
    }

    function saveLayout() {
        try {
            var definition = MoyuLayout.decode(jsonField.text)
            if (!definition)
                throw new Error("请输入布局 JSON，或使用恢复默认按钮")
            jsonField.focus = false
            config.keyboardLayout = JSON.stringify(definition)
            statusMessage = "布局已保存，墨羽拼音键盘会立即使用新布局"
        } catch (error) {
            statusMessage = error.toString()
        }
    }

    function generateLetterLayout() {
        try {
            var layout = MoyuLayout.withLetterRows(firstRow.text, secondRow.text, thirdRow.text)
            jsonField.text = JSON.stringify(layout, null, 2)
            statusMessage = "已生成布局，点击保存即可应用"
        } catch (error) {
            statusMessage = error.toString()
        }
    }

    function restoreDefault() {
        config.keyboardLayout = ""
        jsonField.text = JSON.stringify(MoyuLayout.defaultLayout(), null, 2)
        firstRow.text = "qwertyuiop"
        secondRow.text = "asdfghjkl"
        thirdRow.text = "zxcvbnm"
        statusMessage = "已恢复默认墨羽拼音键盘"
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        PullDownMenu {
            MenuItem {
                text: "恢复默认布局"
                onClicked: page.restoreDefault()
            }
            MenuItem {
                text: "复制布局 JSON"
                onClicked: {
                    Clipboard.text = jsonField.text
                    page.statusMessage = "布局 JSON 已复制"
                }
            }
            MenuItem {
                text: "从剪贴板读取 JSON"
                onClicked: {
                    jsonField.text = Clipboard.text
                    page.statusMessage = "点击保存前会校验布局"
                }
            }
        }

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader { title: "自定义键盘布局" }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: "可调整墨羽拼音的按键顺序、符号、长按字符和行数。空配置使用默认键盘，格式错误时键盘会恢复默认布局。"
                wrapMode: Text.WordWrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
            }

            SectionHeader { text: "快捷调整字母顺序" }

            TextField {
                id: firstRow
                width: parent.width
                text: "qwertyuiop"
                label: "第一行 · 10 个字母"
                inputMethodHints: Qt.ImhLatinOnly | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            }
            TextField {
                id: secondRow
                width: parent.width
                text: "asdfghjkl"
                label: "第二行 · 9 个字母"
                inputMethodHints: Qt.ImhLatinOnly | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            }
            TextField {
                id: thirdRow
                width: parent.width
                text: "zxcvbnm"
                label: "第三行 · 7 个字母"
                inputMethodHints: Qt.ImhLatinOnly | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "生成布局 JSON"
                onClicked: page.generateLetterLayout()
            }

            SectionHeader { text: "完整布局 JSON" }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: "rows 定义 1—6 行，每行的 keys 定义按键。字符键使用 character，caption 是字母，symView 和 symView2 是两层符号，accents 是长按字符。功能键可使用 shift、backspace、symbol、comma、period、space、splitSpace 和 enter。splitIndex 定义横屏分割位置。字符键设置 fixedWidth 为 true 后，可用 implicitWidth 指定固定宽度，功能键可直接设置 implicitWidth。"
                wrapMode: Text.WordWrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
            }

            TextArea {
                id: jsonField
                width: parent.width
                label: "布局 JSON"
                wrapMode: TextEdit.NoWrap
                inputMethodHints: Qt.ImhLatinOnly | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "校验并保存布局"
                onClicked: page.saveLayout()
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: page.statusMessage
                visible: text.length > 0
                wrapMode: Text.WordWrap
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
            }

            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
}
