import QtQuick
import QtQuick.Controls

Rectangle {
    id: mainRoot
    anchors.fill: parent
    color: "#f5f6fa"

    signal logout()
    signal openChat()

    ListModel {
        id: contactsModel
        ListElement { name: "Иван" }
        ListElement { name: "Марья" }
        ListElement { name: "Петр" }
        ListElement { name: "Анфиса" }
    }

    Column {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        Button {
            text: "Сбросить регистрацию гаджета"
            anchors.right: parent.right
            onClicked: {
                _networkEngine.resetRegistration()
                mainRoot.logout()
            }
        }

        ListView {
            width: parent.width
            height: parent.height - 180
            model: contactsModel
            spacing: 12

            delegate: Rectangle {
                width: parent.width
                visible: model.name !== _networkEngine.myName
                height: model.name !== _networkEngine.myName ? 80 : 0
                radius: 12
                color: "white"
                border.color: "#dcdde1"
                border.width: 1

                // ИСПРАВЛЕНО: Свободный контейнер Item вместо капризного Row!
                Item {
                    anchors.fill: parent
                    anchors.margins: 15

                    Text {
                        text: model.name
                        font.pixelSize: 24
                        font.bold: true
                        anchors.verticalCenter: parent.verticalCenter
                        color: "#2f3640"
                    }

                    Row {
                        anchors.right: parent.right // Кнопки теперь легально прижаты вправо!
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10

                        Button {
                            text: "📝"
                            contentItem: Text { text: parent.text; font.pixelSize: 26; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { implicitWidth: 60; implicitHeight: 55; color: "#eccc68"; radius: 8 }
                            onClicked: {
                                _networkEngine.activeChatPeer = model.name
                                mainRoot.openChat()
                            }
                        }

                        Button {
                            text: "📞"
                            contentItem: Text { text: parent.text; font.pixelSize: 26; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { implicitWidth: 60; implicitHeight: 55; color: "#4cd137"; radius: 8 }
                            onClicked: {
                                _networkEngine.startCall(model.name)
                            }
                        }
                    }
                }
            }
        }
    }
}
