import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window
    visible: true
    width: 360
    height: 640
    title: "TeleLoc Radio"

    property string activeScreen: netEngine.isRegistered() ? "MAIN" : "LOGIN"
    property int currentTransportMode: 0
    property string activeChatPeer: ""
    property string activeConferencePeers: ""
    property int activeCallNetType: -1
    property var textDropdownArray: []
    property var audioDropdownLanArray: []
    property var audioDropdownApArray: []
    property var audioDropdownDirectArray: []
    Connections {
        target: netEngine
        function onActiveUsersChanged() {
            window.updateDropdowns()
        }
        function onMessageReceived(fromIp, message) {
            var currentTime = new Date().toLocaleTimeString(Qt.locale(), "hh:mm")
            chatLogModel.append({
                "sender": fromIp,
                "text": message,
                "time": currentTime
            })
        }
    }

    function updateDropdowns() {
        var textTmp = []
        var lanTmp = []
        var apTmp = []
        var directTmp = []
        var myName = "Пользователь"
        try {
            var saved = netEngine.getSavedName()
            if (saved && saved !== "") {
                myName = saved
            }
        } catch(e) {}
        var lanUsers = netEngine.getUsers(0)
        for (var i = 0; i < lanUsers.length; i++) {
            if (lanUsers[i].name === myName) continue
            if (lanUsers[i].name !== window.activeChatPeer) textTmp.push(lanUsers[i].name)
            if (window.activeConferencePeers.indexOf(lanUsers[i].name) === -1) lanTmp.push(lanUsers[i].name)
        }
        var apUsers = netEngine.getUsers(1)
        for (var j = 0; j < apUsers.length; j++) {
            if (apUsers[j].name === myName) continue
            if (window.activeConferencePeers.indexOf(apUsers[j].name) === -1) apTmp.push(apUsers[j].name)
        }
        var directUsers = netEngine.getUsers(2)
        for (var k = 0; k < directUsers.length; k++) {
            if (directUsers[k].name === myName) continue
            if (window.activeConferencePeers.indexOf(directUsers[k].name) === -1) directTmp.push(directUsers[k].name)
        }
        if (window.activeChatPeer !== "") textTmp.push("❌ Выйти")
        if (window.activeConferencePeers !== "") {
            if (window.activeChatPeer !== "") textTmp.push("❌ Выйти")
            if (window.activeCallNetType === 0) lanTmp.push("❌ Выход")
            if (window.activeCallNetType === 1) apTmp.push("❌ Выход")
            if (window.activeCallNetType === 2) directTmp.push("❌ Выход")

        }
        window.textDropdownArray = textTmp
        window.audioDropdownLanArray = lanTmp
        window.audioDropdownApArray = apTmp
        window.audioDropdownDirectArray = directTmp
    }

    Component.onCompleted: {
        window.updateDropdowns()
    }

    Item {
        id: container
        anchors.fill: parent

        Rectangle {
            id: loginScreen
            anchors.fill: parent
            color: "#2c3e50"
            visible: window.activeScreen === "LOGIN"

            Column {
                anchors.centerIn: parent
                spacing: 20
                width: parent.width * 0.8

                Text {
                    text: "Регистрация"
                    color: "white"
                    font.pixelSize: 24
                    font.bold: true
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                TextField {
                    id: regNameField
                    width: parent.width
                    placeholderText: "Введите ваше имя..."
                    color: "black"
                    background: Rectangle {
                        color: "white"
                        radius: 6
                    }
                }

                Button {
                    width: parent.width
                    height: 45
                    background: Rectangle {
                        color: "#2ecc71"
                        radius: 6
                    }
                    contentItem: Text {
                        text: "Войти"
                        color: "white"
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        if (regNameField.text.trimmed() !== "") {
                            window.activeScreen = "MAIN"
                            window.updateDropdowns()
                        }
                    }
                }
            }
        }
        Rectangle {
            id: mainScreen
            anchors.fill: parent
            color: "#34495e"
            visible: window.activeScreen === "MAIN"
//-------------------------------------------------------
            Rectangle {
                id: toolbar
                width: parent.width
                height: 115
                color: "#2c3e50"
                anchors.top: parent.top

                Column {
                    anchors.fill: parent
                    anchors.margins: 5
                    spacing: 5

                    Row {
                        width: parent.width
                        height: 50
                        spacing: 8

                        Button {
                            id: chatMenuButton
                            width: 50
                            height: 50
                            background: Rectangle {
                                color: window.activeChatPeer !== "" ? "#e74c3c" : "#3498db"
                                radius: 8
                            }
                            contentItem: Text {
                                text: "📝"
                                font.pixelSize: 20
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: chatMenu.open()

                            Menu {
                                id: chatMenu
                                title: "Чат с..."
                                Instantiator {
                                    model: window.textDropdownArray
                                    onObjectAdded: (index, object) => chatMenu.insertItem(index, object)
                                    onObjectRemoved: (index, object) => chatMenu.removeItem(object)
                                    delegate: MenuItem {
                                        text: modelData
                                        onTriggered: {
                                            if (modelData === "❌ Выйти") {
                                                window.activeChatPeer = ""
                                            } else {
                                                window.activeChatPeer = modelData
                                            }
                                            window.updateDropdowns()
                                        }
                                    }
                                }
                            }
                        }
                        Button {
                            id: settingsButton
                            width: 50
                            height: 50
                            background: Rectangle {
                                color: "#7f8c8d"
                                radius: 8
                            }
                            contentItem: Text {
                                text: "⚙️"
                                font.pixelSize: 20
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: settingsDialog.open()
                        }

                        Button {
                            id: debugToneButton
                            width: 50
                            height: 50
                            background: Rectangle {
                                color: "#f1c40f"
                                radius: 8
                            }
                            contentItem: Text {
                                text: "🎵"
                                font.pixelSize: 20
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: {
                                netEngine.debugUsers()
                            }
                        }

                        Rectangle {
                            id: volumeIndicator
                            width: parent.width - 174
                            height: 50
                            color: "#34495e"
                            radius: 8
                            border.color: "#bdc3c7"
                            border.width: 1

                            Text {
                                text: "🎤 0%"
                                color: "white"
                                font.pixelSize: 12
                                font.bold: true
                                anchors.centerIn: parent
                            }
                        }
                    }
                    Row {
                        width: parent.width
                        height: 50
                        spacing: 8

                        Button {
                            id: callLanButton
                            width: 50
                            height: 50
                            visible: window.activeCallNetType === -1 ? (window.audioDropdownLanArray.length > 0) : (window.activeCallNetType === 0)
                            background: Rectangle {
                                color: window.activeCallNetType === 0 ? "#e74c3c" : "#2ecc71"
                                radius: 8
                            }
                            contentItem: Text {
                                text: "🏠📞"
                                font.pixelSize: 14
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: callLanMenu.open()

                            Menu {
                                id: callLanMenu
                                title: "LAN Вызов..."
                                Instantiator {
                                    model: window.audioDropdownLanArray
                                    onObjectAdded: (index, object) => callLanMenu.insertItem(index, object)
                                    onObjectRemoved: (index, object) => callLanMenu.removeItem(object)
                                    delegate: MenuItem {
                                        text: modelData
                                        onTriggered: {
                                            callLanMenu.close()
                                            if (modelData === "❌ Выход") {
                                                window.activeConferencePeers = ""
                                                window.activeCallNetType = -1
                                            } else {
                                                window.activeConferencePeers = modelData
                                                window.activeCallNetType = 0
                                                netEngine.startAudioCall(modelData)
                                            }
                                            window.updateDropdowns()
                                        }
                                    }
                                }
                            }
                        }

                        Button {
                            id: callApButton
                            width: 50
                            height: 50
                            visible: window.activeCallNetType === -1 ? (window.audioDropdownApArray.length > 0) : (window.activeCallNetType === 1)
                            background: Rectangle {
                                color: window.activeCallNetType === 1 ? "#e74c3c" : "#e67e22"
                                radius: 8
                            }
                            contentItem: Text {
                                text: "📱📞"
                                font.pixelSize: 14
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: callApMenu.open()

                            Menu {
                                id: callApMenu
                                title: "AP Вызов..."
                                Instantiator {
                                    model: window.audioDropdownApArray
                                    onObjectAdded: (index, object) => callApMenu.insertItem(index, object)
                                    onObjectRemoved: (index, object) => callApMenu.removeItem(object)
                                    delegate: MenuItem {
                                        text: modelData
                                        onTriggered: {
                                            callApMenu.close()
                                            if (modelData === "❌ Выход") {
                                                window.activeConferencePeers = ""
                                                window.activeCallNetType = -1
                                            } else {
                                                window.activeConferencePeers = modelData
                                                window.activeCallNetType = 1
                                                netEngine.startAudioCall(modelData)
                                            }
                                            window.updateDropdowns()
                                        }
                                    }
                                }
                            }
                        }

                        Button {
                            id: callDirectButton
                            width: 50
                            height: 50
                            visible: window.activeCallNetType === -1 ? (window.audioDropdownDirectArray.length > 0) : (window.activeCallNetType === 2)
                            background: Rectangle {
                                color: window.activeCallNetType === 2 ? "#e74c3c" : "#2980b9"
                                radius: 8
                            }
                            contentItem: Text {
                                text: "⚡📞"
                                font.pixelSize: 14
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: callDirectMenu.open()

                            Menu {
                                id: callDirectMenu
                                title: "Direct Вызов..."
                                Instantiator {
                                    model: window.audioDropdownDirectArray
                                    onObjectAdded: (index, object) => callDirectMenu.insertItem(index, object)
                                    onObjectRemoved: (index, object) => callDirectMenu.removeItem(object)
                                    delegate: MenuItem {
                                        text: modelData
                                        onTriggered: {
                                            callDirectMenu.close()
                                            if (modelData === "❌ Выход") {
                                                window.activeConferencePeers = ""
                                                window.activeCallNetType = -1
                                            } else {
                                                window.activeConferencePeers = modelData
                                                window.activeCallNetType = 2
                                                netEngine.startAudioCall(modelData)
                                            }
                                            window.updateDropdowns()
                                        }
                                    }
                                }
                            }
                        }

                        Button {
                            id: callBluetoothButton
                            width: 50
                            height: 50
                            background: Rectangle {
                                color: "#8e44ad"
                                radius: 8
                            }
                            contentItem: Text {
                                text: "🌐"
                                font.pixelSize: 20
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Text {
                            id: statusText
                            width: parent.width - (callLanButton.visible ? 58 : 0) - (callApButton.visible ? 58 : 0) - (callDirectButton.visible ? 58 : 0) - 58 - 10
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: Text.AlignRight
                            text: window.activeChatPeer !== "" ? window.activeChatPeer : (window.activeConferencePeers !== "" ? "Звонок" : "Ждём")
                            color: "white"
                            font.bold: true
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
            }

//----------------------------------------------------------
            ListView {
                id: chatListView
                width: parent.width
                anchors.top: toolbar.bottom
                anchors.bottom: inputArea.top
                model: chatLogModel
                clip: true
                delegate: Item {
                    width: chatListView.width
                    height: textBubble.height + 15

                    Rectangle {
                        id: textBubble
                        width: Math.min(parent.width * 0.7, messageText.implicitWidth + 20)
                        height: messageText.implicitHeight + 20
                        color: model.sender === "Я" ? "#2ecc71" : (model.sender === "Система" ? "#7f8c8d" : "#3498db")
                        radius: 10
                        anchors.right: model.sender === "Я" ? parent.right : undefined
                        anchors.left: model.sender !== "Я" ? parent.left : undefined
                        anchors.margins: 10
                        Column {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 2

                            Text {
                                text: model.sender
                                font.bold: true
                                font.pixelSize: 11
                                color: "#ecf0f1"
                            }

                            Text {
                                id: messageText
                                text: model.text
                                font.pixelSize: 14
                                color: "white"
                                width: parent.width
                                wrapMode: Text.Wrap
                            }

                            Text {
                                text: model.time
                                font.pixelSize: 9
                                color: "#bdc3c7"
                                anchors.right: parent.right
                            }
                        }
                    }
                }
            }
            Rectangle {
                id: inputArea
                width: parent.width
                height: 60
                color: "#2c3e50"
                anchors.bottom: parent.bottom

                Row {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    TextField {
                        id: msgField
                        width: parent.width - 70
                        height: 40
                        placeholderText: window.activeChatPeer !== "" ? "Сообщение для " + window.activeChatPeer + "..." : "Выберите абонента в меню..."
                        enabled: window.activeChatPeer !== ""
                        color: "black"
                        background: Rectangle {
                            color: parent.enabled ? "white" : "#95a5a6"
                            radius: 6
                        }
                    }

                    Button {
                        width: 50
                        height: 40
                        enabled: window.activeChatPeer !== "" && msgField.text.trimmed() !== ""
                        background: Rectangle {
                            color: parent.enabled ? "#2ecc71" : "#95a5a6"
                            radius: 6
                        }
                        contentItem: Text {
                            text: "➤"
                            font.pixelSize: 18
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: {
                            var currentTime = new Date().toLocaleTimeString(Qt.locale(), "hh:mm")
                            chatLogModel.append({
                                "sender": "Я",
                                "text": msgField.text,
                                "time": currentTime
                            })
                            netEngine.sendMessage(window.activeChatPeer, msgField.text)
                            msgField.text = ""
                        }
                    }
                }
            }
        }
    }
    Dialog {
        id: settingsDialog
        title: "Настройки"
        anchors.centerIn: parent
        width: 320
        height: 240
        modal: true
        focus: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnOutsidePressed

        background: Rectangle {
            color: "#2c3e50"
            radius: 10
            border.color: "#34495e"
            border.width: 2
        }

        header: Rectangle {
            width: parent.width
            height: 40
            color: "#34495e"
            radius: 10
            Text {
                text: "Настройки рации"
                color: "white"
                font.bold: true
                font.pixelSize: 16
                anchors.centerIn: parent
            }
        }

        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 15

            Row {
                spacing: 10
                width: parent.width

                Text {
                    text: "Имя:"
                    color: "white"
                    font.pixelSize: 16
                    width: 50
                    anchors.verticalCenter: parent.verticalCenter
                }

                TextField {
                    id: nameField
                    text: netEngine.getSavedName()
                    width: 200
                    placeholderText: "Введите имя..."
                    color: "black"
                    anchors.verticalCenter: parent.verticalCenter
                    background: Rectangle {
                        color: "white"
                        radius: 4
                    }
                }
            }

            Button {
                id: createGroupBtn
                width: parent.width
                height: 50
                text: "Запустить группу (:D)"
                font.bold: true
                background: Rectangle {
                    color: "#e67e22"
                    radius: 6
                }
                onClicked: {
                    netEngine.createAndroidP2pGroup()
                }
            }

            Button {
                width: parent.width
                height: 40
                text: "Закрыть"
                background: Rectangle {
                    color: "#7f8c8d"
                    radius: 6
                }
                onClicked: {
                    settingsDialog.close()
                }
            }
        }
    }

    MouseArea {
        id: screenDebugMouseArea
        anchors.fill: parent
        z: -1
        onClicked: {
            netEngine.debugUsers()
        }
    }
}
