import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

Window {
    id: window
    width: 360
    height: 640
    visible: true
    title: "TeleLoc"

    property string activeChatPeer: ""
    property string activeConferencePeers: ""
    property string incomingCallFrom: ""
    property string debugAudioPath: ""
    property bool isPlayingFile: false

    ListModel { id: chatModel }
    ListModel {
        id: contactsModel
        ListElement { name: "Иван" }
        ListElement { name: "Марья" }
        ListElement { name: "Петр" }
        ListElement { name: "Анфиса" }
    }
    ListModel { id: textDropdownModel }
    ListModel { id: audioDropdownModel }
    ListModel { id: localWavFilesModel }

    FileDialog {
        id: audioFileDialog
        title: "Выберите WAV файл"
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            var path = String(audioFileDialog.selectedFile).replace("file:///", "")
            window.debugAudioPath = path
            netEngine.saveDebugAudioPath(path)
        }
    }

    function updateDropdowns() {
        textDropdownModel.clear()
        audioDropdownModel.clear()
        textDropdownModel.append({"name": "Все"})
        audioDropdownModel.append({"name": "Все"})
        var myName = "Пользователь"
        try {
            var saved = netEngine.getSavedName()
            if (saved && saved !== "") {
                myName = saved
            }
        } catch(e) {}
        for (var i = 0; i < contactsModel.count; i++) {
            var contactName = contactsModel.get(i).name
            if (contactName === myName) continue
            if (contactName !== window.activeChatPeer) {
                textDropdownModel.append({"name": contactName})
            }
            if (window.activeConferencePeers.indexOf(contactName) === -1) {
                audioDropdownModel.append({"name": contactName})
            }
        }
    }

    function scanAndroidAudio() {
        localWavFilesModel.clear()
        var files = netEngine.getAvailableWavFiles()

        // Если реальных файлов нет, шьем виртуальный список отладки!
        if (files.length === 0) {
            localWavFilesModel.append({"name": "Рингтон_Анфисы.wav"})
            localWavFilesModel.append({"name": "Сигнал_Вызова_Ивана.wav"})
            localWavFilesModel.append({"name": "Тестовая_Музыка.wav"})
        } else {
            for (var i = 0; i < files.length; i++) {
                localWavFilesModel.append({"name": files[i]})
            }
        }
    }

    Connections {
        target: netEngine
        ignoreUnknownSignals: true
        function onMessageReceived(sender, text) {
            chatModel.append({
                "senderName": sender,
                "messageText": text,
                "isMe": (sender === netEngine.getSavedName())
            })
        }
        function onIncomingCall(peer) {
            window.incomingCallFrom = peer
        }
        function onCallAccepted() {
            if (window.activeConferencePeers === "") {
                window.activeConferencePeers = window.incomingCallFrom
            } else {
                window.activeConferencePeers += ", " + window.incomingCallFrom
            }
            window.incomingCallFrom = ""
            window.updateDropdowns()
        }
        function onCallEnded() {
            window.activeConferencePeers = ""
            window.isPlayingFile = false
            window.updateDropdowns()
        }
    }

    Component.onCompleted: {
        window.debugAudioPath = netEngine.getSavedDebugAudioPath()
        if (!netEngine.isRegistered()) {
            netEngine.saveNameToFile("Пользователь")
        }
        netEngine.start(netEngine.getSavedName())
        window.updateDropdowns()
    }

    Column {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            id: toolbar
            width: parent.width
            height: 60
            color: "#2c3e50"
            z: 10

            Row {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                Button {
                    id: textMenuButton
                    width: 36
                    height: 40
                    contentItem: Text {
                        text: "📝"
                        font.pixelSize: 18
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        window.updateDropdowns()
                        textMenu.popup()
                    }
                    Menu {
                        id: textMenu
                        Instantiator {
                            model: textDropdownModel
                            onObjectAdded: (index, object) => textMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => textMenu.removeItem(object)
                            delegate: MenuItem {
                                text: model.name
                                onTriggered: {
                                    if (model.name !== "Все") {
                                        window.activeChatPeer = model.name
                                    }
                                    netEngine.startChatSession(model.name)
                                    window.updateDropdowns()
                                }
                            }
                        }
                    }
                }

                Button {
                    id: audioMenuButton
                    width: 36
                    height: 40
                    background: Rectangle {
                        color: window.activeConferencePeers === "" ? "#2ecc71" : "#e74c3c"
                        radius: 6
                    }
                    contentItem: Text {
                        text: "📞"
                        font.pixelSize: 18
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        window.updateDropdowns()
                        audioMenu.popup()
                    }
                    Menu {
                        id: audioMenu
                        Instantiator {
                            model: audioDropdownModel
                            onObjectAdded: (index, object) => audioMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => audioMenu.removeItem(object)
                            delegate: MenuItem {
                                text: model.name
                                onTriggered: {
                                    netEngine.startAudioCall(model.name)
                                    if (window.activeConferencePeers === "") {
                                        window.activeConferencePeers = model.name
                                    } else if (model.name === "Все") {
                                        window.activeConferencePeers = "Все"
                                    } else {
                                        window.activeConferencePeers += ", " + model.name
                                    }
                                    window.updateDropdowns()
                                }
                            }
                        }
                    }
                }
                Button {
                    id: selectFileButton
                    width: 36
                    height: 40
                    background: Rectangle {
                        color: "#34495e"
                        radius: 6
                        border.color: window.debugAudioPath !== "" ? "#2ecc71" : "transparent"
                        border.width: 1
                    }
                    contentItem: Text {
                        text: "🎵"
                        font.pixelSize: 18
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        if (Qt.platform.os === "android") {
                            window.scanAndroidAudio()
                            androidFilesMenu.popup()
                        } else {
                            audioFileDialog.open()
                        }
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: window.debugAudioPath === "" ? "Выбрать отладочный WAV" : "Выбран: " + window.debugAudioPath.split('/').pop()

                    Menu {
                        id: androidFilesMenu
                        Instantiator {
                            model: localWavFilesModel
                            onObjectAdded: (index, object) => androidFilesMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => androidFilesMenu.removeItem(object)
                            delegate: MenuItem {
                                text: model.name
                                onTriggered: {
                                    var baseDir = netEngine.getAvailableWavFilesPath()
                                    window.debugAudioPath = baseDir + "/" + model.name
                                    netEngine.saveDebugAudioPath(window.debugAudioPath)
                                }
                            }
                        }
                    }
                }

                Button {
                    id: playFileButton
                    width: 36
                    height: 40
                    visible: window.activeConferencePeers !== "" && window.debugAudioPath !== ""
                    background: Rectangle {
                        color: window.isPlayingFile ? "#f39c12" : "#2c3e50"
                        radius: 6
                        border.color: "#f39c12"
                        border.width: 1
                    }
                    contentItem: Text {
                        text: window.isPlayingFile ? "⏸️" : "▶️"
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        window.isPlayingFile = !window.isPlayingFile
                        netEngine.setPlayFileMode(window.isPlayingFile)
                    }
                }

                Button {
                    id: hangUpButton
                    width: 36
                    height: 40
                    visible: window.activeConferencePeers !== ""
                    background: Rectangle {
                        color: "#d63031"
                        radius: 6
                    }
                    contentItem: Text {
                        text: "❌"
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        netEngine.stopAudioCall()
                        window.activeConferencePeers = ""
                        window.updateDropdowns()
                    }
                }

                Column {
                    width: parent.width - 158 - (playFileButton.visible ? 46 : 0) - (hangUpButton.visible ? 46 : 0)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        text: "Чат: " + window.activeChatPeer
                        color: "white"
                        font.bold: true
                        font.pixelSize: 13
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Text {
                        text: window.activeConferencePeers === "" ? "🎙 Звонок: нет" : "🎙 В сети: " + window.activeConferencePeers
                        color: "#2ecc71"
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        width: parent.width
                    }

                    Row {
                        width: parent.width
                        spacing: 6
                        visible: window.activeConferencePeers !== ""

                        Column {
                            width: (parent.width - 6) / 2
                            spacing: 1
                            Text { text: "Мик: " + netEngine.micLevel + "%"; color: "#a4b0be"; font.pixelSize: 8 }
                            ProgressBar {
                                id: micBar
                                width: parent.width
                                height: 4
                                value: netEngine.micLevel / 100.0
                                background: Rectangle { color: "#1a252f"; radius: 2 }
                                contentItem: Item {
                                    Rectangle { width: micBar.width * micBar.value; height: parent.height; color: "#3498db"; radius: 2 }
                                }
                            }
                        }

                        Column {
                            width: (parent.width - 6) / 2
                            spacing: 1
                            Text { text: "Сет: " + netEngine.netLevel + "%"; color: "#a4b0be"; font.pixelSize: 8 }
                            ProgressBar {
                                id: netBar
                                width: parent.width
                                height: 4
                                value: netEngine.netLevel / 100.0
                                background: Rectangle { color: "#1a252f"; radius: 2 }
                                contentItem: Item {
                                    Rectangle { width: netBar.width * netBar.value; height: parent.height; color: "#2ecc71"; radius: 2 }
                                }
                            }
                        }
                    }
                }
            }
        }
        Item {
            width: parent.width
            height: parent.height - 60

            Column {
                anchors.fill: parent
                anchors.margins: 15
                spacing: 10

                ScrollView {
                    id: chatScroll
                    width: parent.width
                    height: parent.height - 60
                    clip: true

                    ListView {
                        id: chatListView
                        model: chatModel
                        width: chatScroll.width
                        spacing: 8
                        onCountChanged: chatListView.positionViewAtEnd()

                        delegate: Item {
                            width: chatListView.width
                            height: messageBubble.height + 4

                            Rectangle {
                                id: messageBubble
                                color: model.isMe ? "#e8f5e9" : "#ffffff"
                                border.color: model.isMe ? "#c8e6c9" : "#e0e0e0"
                                border.width: 1
                                radius: 8
                                width: parent.width - 20
                                anchors.horizontalCenter: parent.horizontalCenter
                                height: msgColumn.height + 12

                                Column {
                                    id: msgColumn
                                    x: 10
                                    y: 6
                                    width: parent.width - 20
                                    spacing: 4

                                    Text {
                                        text: model.isMe ? "Вы:" : model.senderName + ":"
                                        font.bold: true
                                        font.pixelSize: 14
                                        color: model.isMe ? "#2e7d32" : "#6a1b9a"
                                    }

                                    Text {
                                        text: model.messageText
                                        color: "black"
                                        font.pixelSize: 15
                                        wrapMode: Text.Wrap
                                        width: parent.width
                                    }
                                }
                            }
                        }
                    }
                }

                Row {
                    width: parent.width
                    height: 40
                    spacing: 10

                    TextField {
                        id: messageInput
                        width: parent.width - 65
                        placeholderText: "Введите сообщение..."
                        font.pixelSize: 16
                        onAccepted: okButton.clicked()
                    }

                    Button {
                        id: okButton
                        text: "▶"
                        width: 55
                        height: parent.height
                        font.pixelSize: 22
                        font.bold: true
                        background: Rectangle {
                            color: "#4caf50"
                            radius: 6
                        }
                        contentItem: Text {
                            text: okButton.text
                            font: okButton.font
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: {
                            if (messageInput.text.trim() !== "") {
                                netEngine.sendMessage(window.activeChatPeer, messageInput.text.trim())
                                messageInput.text = ""
                            }
                        }
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                color: "#aa000000"
                visible: window.incomingCallFrom !== ""
                z: 20
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        netEngine.stopAudioCall()
                        window.incomingCallFrom = ""
                    }
                }
                Rectangle {
                    width: 280
                    height: 160
                    color: "white"
                    radius: 12
                    anchors.centerIn: parent
                    MouseArea { anchors.fill: parent }
                    Column {
                        anchors.centerIn: parent
                        spacing: 20
                        width: parent.width * 0.85
                        Text {
                            text: "Входящий вызов от:\n" + window.incomingCallFrom
                            font.pixelSize: 18
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            width: parent.width
                        }
                        Button {
                            text: "📞 Ответить"
                            width: parent.width
                            height: 45
                            anchors.horizontalCenter: parent.horizontalCenter
                            background: Rectangle {
                                color: "#2ecc71"
                                radius: 6
                            }
                            contentItem: Text {
                                text: parent.text
                                color: "white"
                                font.pixelSize: 16
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: {
                                netEngine.startAudioCall(window.incomingCallFrom)
                            }
                        }
                    }
                }
            }
        }
    }
}
