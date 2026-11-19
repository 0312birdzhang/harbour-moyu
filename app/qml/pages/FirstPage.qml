import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0

Page{


    ConfigurationGroup{
        id: config
        path: "/app/xyz.birdzhang.moyu.ime"
        property int pageSize: 20
        property int fetchSize: 15
    }
    SilicaFlickable{
        anchors.fill: parent
        contentHeight: column.height
        PullDownMenu {
            MenuItem {
                text: "关于"
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
            MenuItem {
                text: "Rime 用户词典"
                onClicked: pageStack.push(Qt.resolvedUrl("UserDictionaryPage.qml"))
            }
            MenuItem {
                text: "Rime 词典文件"
                onClicked: pageStack.push(Qt.resolvedUrl("DictionaryFilesPage.qml"))
            }
            MenuItem {
                text: "自定义键盘布局"
                onClicked: pageStack.push(Qt.resolvedUrl("KeyboardLayoutPage.qml"))
            }
        }
        Column{
            id: column
            width: parent.width
            spacing: Theme.paddingMedium
            PageHeader{
                title: "设置"
            }

            SectionHeader{
                text: "联想词数量"
                font.pixelSize: Theme.fontSizeMedium * 0.8
            }
            Slider {
                minimumValue: 5
                maximumValue: 20
                stepSize: 1
                value: config.fetchSize
                width: parent.width
                valueText:  value
                onValueChanged: {
                    config.fetchSize = value;
                }
            }

            SectionHeader{
                text: "候选词数量"
                font.pixelSize: Theme.fontSizeMedium * 0.8
            }

            Slider {
                minimumValue: 10
                maximumValue: 25
                stepSize: 1
                value: config.pageSize
                width: parent.width
                valueText: value
                onValueChanged: {
                    config.pageSize = value;
                }
            }

        }
    }

}
