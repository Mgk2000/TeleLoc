import QtQuick
import QtQuick.Controls

Rectangle {
    id: chatRoot
    anchors.fill: parent
    color: "#f5f6fa"

    signal backPressed() // Сигнал возврата на главный экран

    Column {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 15

        Row {
            width: parent.width
            spacing: 15

            Button {
                text: "⬅ Назад"
                font.pixelSize: 16
                onClicked: {
                    _networkEngine.activeChatPeer = ""
                    chatRoot.backPressed()
                }
            }

            Text {
                text: "Чат: " + _networkEngine.activeChatPeer
                font.pixelSize: 22
                font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        ScrollView {
            width: parent.width
            height: parent.height - 240
            clip: true
            background: Rectangle { color: "white"; radius: 10; border.color: "#dcdde1" }

            TextArea {
                text: _networkEngine.chatLog
                font.pixelSize: 18
                readOnly: true
                wrapMode: TextArea.Wrap
            }
        }

        Row {
            width: parent.width
            spacing: 10

            TextField {
                id: messageInput
                width: parent.width - 90
                placeholderText: "Сообщение..."
                font.pixelSize: 18
                background: Rectangle { implicitHeight: 50; radius: 8; border.color: "#7f8fa6" }
            }

            Button {
                width: 80
                height: 50
                text: "Послать"
                contentItem: Text { text: parent.text; font.pixelSize: 16; font.bold: true; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { color: "#00a8ff"; radius: 8 }
                onClicked: {
                    if (messageInput.text.trim() !== "") {
                        _networkEngine.sendTextMessage(messageInput.text.trim())
                        messageInput.text = ""
                    }
                }
            }
        }
    }
}
