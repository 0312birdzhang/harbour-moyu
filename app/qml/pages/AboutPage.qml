import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: "关于"
            }

            Label {
                text: "墨羽输入法 0.5.9"
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeLarge
            }

            SectionHeader {
                text: "说明"
                font.pixelSize: Theme.fontSizeMedium
            }

            Label {
                text: "墨羽输入法使用 Rime 1.13.1 和雾凇拼音词库，提供简体中文拼音输入。键盘前端基于搜鸟输入法的 Maliit/QML 布局。" +
                      "<br/><br/>在系统设置的【文本输入】中勾选【墨羽拼音输入法】，然后在输入界面切换键盘。" +
                      "<br/><br/>本应用提供 Rime 用户词典管理、词典文件导入导出和自定义键盘布局。手动添加的词条立即收录，自动学习的词条在第 5 次输入时收录。" +
                      "<br/><br/>早期键盘前端参考木木大大的【山寨谷歌输入法】和 gexc 大大的【拼音键盘】。"
                textFormat: Text.RichText
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.primaryColor
                width: parent.width
                anchors {
                    left: parent.left
                    right: parent.right
                    rightMargin: Theme.paddingMedium
                    leftMargin: Theme.paddingMedium
                }
            }

            SectionHeader {
                text: "作者"
                font.pixelSize: Theme.fontSizeMedium
            }

            Label {
                text: "旗鱼俱乐部 @0312birdzhang 出品"
                wrapMode: Text.WordWrap
                textFormat: Text.RichText
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.primaryColor
                width: parent.width - Theme.paddingLarge
                horizontalAlignment: Text.AlignRight
            }

            Label {
                text: "霓虹羽墨输入法图标"
                wrapMode: Text.WordWrap
                textFormat: Text.RichText
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.primaryColor
                width: parent.width - Theme.paddingLarge
                horizontalAlignment: Text.AlignRight
            }

            Label {
                text: "简繁转换插件由topiasv支持 -- 已废弃"
                wrapMode: Text.WordWrap
                textFormat: Text.RichText
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.primaryColor
                width: parent.width - Theme.paddingLarge
                horizontalAlignment: Text.AlignRight
            }
        }
    }
}
