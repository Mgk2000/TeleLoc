import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    visibility: Qt.platform.os === "windows" ? Window.Windowed : Window.FullScreen

    width: 360
    height: 640
    title: "TeleLoc Рация"

    background: Rectangle {
        color: "#121212" // Глубокий графитовый фон AMOLED
    }

    // Полноэкранный вызов (остается Попапом в глобальном контексте)
    CallScreenPopup {
        id: customCallScreen
    }

    // Глобальные свойства сохранены для полной совместимости
    property string activeChatPeer: ""
    property string savedName: "Полосарь"
    property string activeConferencePeers: ""
    property int activeCallNetType: -1
    property int micVolume: 0
    property string callingColor: "#008800"
    property bool chatVisible: false
    property bool developer: netEngine.isDeveloper()

    ListModel {
        id: chatLogModel
    }

    Connections {
        target: netEngine
        function onPeerListChanged() {
            // Оставляем вашу логику обновления меню чата, если она используется
        }
        function onMessageReceived(fromIp, message) {
            var currentTime = new Date().toLocaleTimeString(Qt.locale(), "hh:mm")
            chatLogModel.append({
                "sender": fromIp,
                "text": message,
                "time": currentTime
            })
        }
        function onIncomingCall(peerName, netType) {
            customCallScreen.callerName = peerName
            customCallScreen.callNetType = netType
            customCallScreen.isIncoming = true
            customCallScreen.micLevel = audioEngine.getMicLevel()
            customCallScreen.speakerLevel = audioEngine.getSpeakerLevel()
            customCallScreen.open()
        }
        function onCallAccepted() {
            if (window.activeCallNetType !== -1) statusText.text = "Разговор"
        }
        function onCallStopped() {
            //micIndicatorText.text = "🎤 0%"
            //netIndicatorText.text = "🔊 0%"
            customCallScreen.close()
            window.activeConferencePeers = ""
            window.activeCallNetType = -1
            console.log("@@@sound QML onCallStopped")
        }
        function onSetActiveNetType(_netType) {
            window.activeCallNetType = _netType
        }
        function onSetCallState(st) {
            if (st === 0) callingColor = "#0fa958"
            else if (st === 1 || st === 2) callingColor = "#a3e2ab"
            else callingColor = "#ffb3b3"
        }
        function onInitMicLevel(ml) { customCallScreen.micLevel = ml }
        function onInitSpeakerLevel(sl) { customCallScreen.speakerLevel = sl }
        function onAutoAcceptCall() { customCallScreen.open() }
    }

    Connections {
        target: audioEngine
        function onMicVolumeUpdated(volume) {
            if (typeof micIndicatorText !== "undefined" && micIndicatorText)
                micIndicatorText.text = "🎤 " + volume + "%"
        }
        function onNetVolumeUpdated(volume) {
            if (typeof netIndicatorText !== "undefined" && netIndicatorText)
                netIndicatorText.text = "🔊 " + volume + "%"
        }
        function onStopWritingSound() {
            if (typeof btnRecord !== "undefined" && btnRecord)
                btnRecord.checked = false
        }
        function onShowAttenuator(val) {customCallScreen.setAttVal(val)}
    }

    // Главный контейнер страниц мессенджера
    StackView {
        id: mainStack
        anchors.fill: parent
        initialItem: mainPage // Стартуем со списка пиров
    }
    // ============================================================================
    // СТРАНИЦА 1: ГЛАВНЫЙ ЭКРАН (СПИСОК АБОНЕНТОВ И НИЖНИЙ ТУЛБАР)
    // ============================================================================
    Component {
        id: mainPage
        Page {
            id: mainPageRoot
            background: Rectangle { color: "#121212" }

            // Верхняя аккуратная панель заголовка в стиле Telegram
            header: Rectangle {
                width: parent.width
                height: 56
                color: "#1e1e1e"

                Rectangle {
                    width: parent.width; height: 1; color: "#2d2d2d"
                    anchors.bottom: parent.bottom
                }

                Text {
                    text: "Чаты и абоненты"
                    color: "#ffffff"
                    font.pixelSize: 18
                    font.weight: Font.Bold
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // Чистый вертикальный список пиров (без кнопок вызовов внутри)
            ListView {
                id: usersList
                anchors.fill: parent
                anchors.topMargin: 6
                model: myUsersModel // Ваша оригинальная C++ модель пиров
                spacing: 2
                clip: true

                delegate: Rectangle {
                    id: peerDelegate
                    width: usersList.width
                    height: 70
                    color: itemMouseArea.pressed ? "#252525" : (itemMouseArea.containsMouse ? "#1c1a1a" : "#121212")

                    // Тонкий разделитель между карточками пиров
                    Rectangle {
                        width: parent.width - 32
                        height: 1
                        color: "#1f1f1f"
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 12

                        // Иконка-аватар по первой букве имени
                        Rectangle {
                            width: 44; height: 44
                            radius: 22
                            color: isAlive ? "#143d28" : "#252525"
                            border.color: isAlive ? "#0fa958" : "#444444"
                            border.width: 1

                            Text {
                                text: userName.substring(0, 1).toUpperCase()
                                color: isAlive ? "#0fa958" : "#888888"
                                font.pixelSize: 16
                                font.weight: Font.Bold
                                anchors.centerIn: parent
                            }
                        }

                        // Информация об абоненте (Имя и статус)
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: userName // Роль NameRole из C++
                                font.pixelSize: 16
                                font.weight: Font.Bold
                                color: isAlive ? "#e0e0e0" : "#666666"
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }

                            Text {
                                text: isAlive ? "В сети (Доступен p2p)" : "Не в сети"
                                font.pixelSize: 13
                                color: isAlive ? "#0fa958" : "#555555"
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }

                        // Стрелочка перехода вправо (как в традиционных списках)
                        Text {
                            text: "❯"
                            font.pixelSize: 14
                            color: "#333333"
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }

                    MouseArea {
                        id: itemMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            // При клике фиксируем активного абонента и плавно переходим на его персональную страницу
                            window.activeChatPeer = userName
                            mainStack.push(peerPage, { "peerName": userName, "peerIpLocal": ipLocal, "peerIpSpot": ipSpot, "peerIpDirect": ipDirect })
                        }
                    }
                }
            }

            // ТРАДИЦИОННЫЙ НИЖНИЙ ТУЛБАР УПРАВЛЕНИЯ
            footer: Rectangle {
                id: footerToolbar
                width: parent.width
                height: 64
                color: "#1e1e1e" // Окрашен в строгий темно-серый цвет мессенджера

                Rectangle {
                    width: parent.width; height: 1; color: "#2d2d2d"
                    anchors.top: parent.top
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 16

                    // Круглая кнопка системных настроек рации
                    Button {
                        id: settingsButton
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                        Layout.alignment: Qt.AlignVCenter

                        background: Rectangle {
                            color: settingsButton.pressed ? "#2d2d2d" : (settingsButton.hovered ? "#252525" : "transparent")
                            radius: 22
                        }
                        contentItem: Text {
                            text: "⚙️"
                            font.pixelSize: 22
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: {
                            savedName = netEngine.getSavedName()
                            settingsDialog.open()
                        }
                    }

                }
            }
        }
    }
    // ============================================================================
    // СТРАНИЦА 2: ПЕРСОНАЛЬНЫЙ ЭКРАН АБОНЕНТА (ШАПКА И ЗВОНКИ)
    // ============================================================================
    Component {
        id: peerPage
        Page {
            id: peerPageRoot
            background: Rectangle { color: "#121212" }

            // Переменные страницы, получаемые при переходе (push)
            property string peerName: ""
            property string peerIpLocal: ""
            property string peerIpSpot: ""
            property string peerIpDirect: ""

            // ВЕРХНИЙ ТУЛБАР АБОНЕНТА: Кнопка «Назад», Звонки и Запись WAV
            header: Rectangle {
                width: parent.width
                height: 56
                color: "#1e1e1e"

                Rectangle {
                    width: parent.width; height: 1; color: "#2d2d2d"
                    anchors.bottom: parent.bottom
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 12
                    spacing: 6

                    // Кнопка возврата к списку чатов
                    Button {
                        id: backButton
                        Layout.preferredWidth: 60
                        Layout.preferredHeight: 60
                        background: Rectangle {
                            color: backButton.pressed ? "#2d2d2d" : "transparent"
                            radius: 20
                        }
                        contentItem: Text {
                            text: "⬅️"
                            color: "#2f8ccb"
                            font.pixelSize: 40
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: {
                            window.activeChatPeer = ""
                            mainStack.pop() // Возврат на главный экран
                        }
                    }

                    // Имя абонента, с которым ведется работа
                    Text {
                        text: peerPageRoot.peerName
                        color: "#ffffff"
                        font.pixelSize: 16
                        font.weight: Font.Bold
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }

                    // --- БЛОК КНОПОК ЗВОНКОВ ПО СЕТЯМ (ИЗ СТАРОГО USERSLIST) ---

                    // Кнопка 1: Локальная сеть (Роутер) — Зеленая
                    Button {
                        enabled: peerPageRoot.peerIpLocal !== ""
                        Layout.preferredWidth: 60
                        Layout.preferredHeight: 60
                        background: Rectangle {
                            color: parent.enabled ? (parent.pressed ? "#1e5a3c" : "#2e7d32") : "#252525"
                            radius: 8
                            border.color: parent.enabled ? "#4caf50" : "#333333"
                            border.width: 1
                        }
                        contentItem: Text { text: "🏠"; font.pixelSize: 40; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; opacity: parent.enabled ? 1.0 : 0.2 }
                        onClicked: {
                            customCallScreen.callerName = peerPageRoot.peerName
                            customCallScreen.callNetType = 0
                            customCallScreen.isIncoming = false
                            customCallScreen.micLevel = audioEngine.getMicLevel()
                            customCallScreen.speakerLevel = audioEngine.getSpeakerLevel()
                            customCallScreen.open()
                            console.log("Звонок по LAN на " + peerPageRoot.peerIpLocal)
                        }
                    }

                    // Кнопка 2: Хот-спот — Голубая
                    Button {
                        enabled: peerPageRoot.peerIpSpot !== ""
                        Layout.preferredWidth: 60
                        Layout.preferredHeight: 60
                        background: Rectangle {
                            color: parent.enabled ? (parent.pressed ? "#0f547a" : "#0277bd") : "#252525"
                            radius: 8
                            border.color: parent.enabled ? "#03a9f4" : "#333333"
                            border.width: 1
                        }
                        contentItem: Text { text: "📱"; font.pixelSize: 40; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; opacity: parent.enabled ? 1.0 : 0.2 }
                        onClicked: {
                            customCallScreen.callerName = peerPageRoot.peerName
                            customCallScreen.callNetType = 1
                            customCallScreen.isIncoming = false
                            customCallScreen.micLevel = audioEngine.getMicLevel()
                            customCallScreen.speakerLevel = audioEngine.getSpeakerLevel()
                            customCallScreen.open()
                            console.log("Звонок по AP на " + peerPageRoot.peerIpSpot)
                        }
                    }

                    // Кнопка 3: Wi-Fi Direct — Фиолетовая
                    Button {
                        enabled: peerPageRoot.peerIpDirect !== ""
                        Layout.preferredWidth: 60
                        Layout.preferredHeight: 60
                        background: Rectangle {
                            color: parent.enabled ? (parent.pressed ? "#4a148c" : "#6a1b9a") : "#252525"
                            radius: 8
                            border.color: parent.enabled ? "#9c27b0" : "#333333"
                            border.width: 1
                        }
                        contentItem: Text { text: "⚡"; font.pixelSize: 40; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; opacity: parent.enabled ? 1.0 : 0.2 }
                        onClicked: {
                            customCallScreen.callerName = peerPageRoot.peerName
                            customCallScreen.callNetType = 2
                            customCallScreen.isIncoming = false
                            customCallScreen.micLevel = audioEngine.getMicLevel()
                            customCallScreen.speakerLevel = audioEngine.getSpeakerLevel()
                            customCallScreen.open()
                            console.log("Звонок по Direct на " + peerPageRoot.peerIpDirect)
                        }
                    }

                    // Кнопка 4: Запись аудио разговора (btnRecord) — Сдержанная красная
                /*    Button {
                        id: btnRecord
                        Layout.preferredWidth: 60
                        Layout.preferredHeight: 60
                        checkable: true
                        checked: false
                        visible : developer
                        onCheckedChanged: {
                            if (checked) {
                                netEngine.masterStartRecording()
                            } else {
                                netEngine.masterStopRecording()
                            }
                        }

                        background: Rectangle {
                            color: btnRecord.checked ? "#331111" : "#252525"
                            border.color: btnRecord.checked ? "#ef5350" : "#444444"
                            border.width: 1
                            radius: 8
                        }

                        contentItem: Item {
                            anchors.fill: parent
                            Rectangle {
                                anchors.centerIn: parent
                                width: btnRecord.checked ? 12 : 10
                                height: btnRecord.checked ? 12 : 10
                                radius: btnRecord.checked ? 2 : 5
                                color: "#ef5350"
                            }
                        }
                    }*/

                }
            }
            // История переписки строго с данным абонентом
            ListView {
                id: chatListView
                width: parent.width
                anchors.top: parent.top
                anchors.bottom: messageInputRow.top
                anchors.margins: 10
                model: chatLogModel
                clip: true

                delegate: Item {
                    width: chatListView.width
                    height: chatText.implicitHeight + 34
                    // Фильтруем: показываем сообщения только этого абонента или ваши ответы ему
                    visible: model.sender === peerPageRoot.peerName || model.sender === netEngine.getSavedName()

                    Rectangle {
                        width: parent.width * 0.75
                        height: chatText.implicitHeight + 24
                        color: model.sender === netEngine.getSavedName() ? "#143d28" : "#212121"
                        radius: 12
                        border.color: model.sender === netEngine.getSavedName() ? "#1e5a3c" : "#2d2d2d"
                        border.width: 1
                        anchors.right: model.sender === netEngine.getSavedName() ? parent.right : undefined
                        anchors.left: model.sender === netEngine.getSavedName() ? undefined : parent.left

                        Column {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 4

                            Text {
                                text: model.sender
                                font.bold: true
                                font.pixelSize: 11
                                color: model.sender === netEngine.getSavedName() ? "#4caf50" : "#2f8ccb"
                            }

                            TextEdit {
                                id: chatText
                                text: model.text
                                width: parent.width
                                wrapMode: Text.Wrap
                                font.pixelSize: 14
                                color: "#e0e0e0"
                                readOnly: true
                                selectByMouse: true
                                persistentSelection: true
                            }

                            Text {
                                text: model.time
                                font.pixelSize: 10
                                color: "#777777"
                                anchors.right: parent.right
                            }
                        }
                    }
                }
            }

            // НИЖНЯЯ ПАНЕЛЬ ВВОДА И ОТПРАВКИ СООБЩЕНИЙ (messageInputRow)
            Row {
                id: messageInputRow
                width: parent.width
                height: 50
                spacing: 8
                anchors.bottom: parent.bottom
                anchors.margins: 6

                TextField {
                    id: messageField
                    width: parent.width - 64
                    height: 44
                    placeholderText: "Сообщение..."
                    placeholderTextColor: "#666666"
                    font.pixelSize: 14
                    color: "#e0e0e0"
                    verticalAlignment: TextInput.AlignVCenter
                    leftPadding: 14

                    background: Rectangle {
                        color: "#1e1e1e"
                        border.color: messageField.activeFocus ? "#2f8ccb" : "#2d2d2d"
                        border.width: 1.5
                        radius: 22
                    }
                }

                Button {
                    width: 44
                    height: 44
                    background: Rectangle {
                        color: messageField.text.trim() !== "" ? "#2f8ccb" : "#252525"
                        radius: 22
                        border.color: messageField.text.trim() !== "" ? "#2f8ccb" : "#333333"
                        border.width: 1
                    }
                    contentItem: Text {
                        text: "➔"
                        color: messageField.text.trim() !== "" ? "white" : "#555555"
                        font.bold: true
                        font.pixelSize: 16
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        if (messageField.text.trim() !== "") {
                            netEngine.sendMessage(peerPageRoot.peerName, messageField.text)
                            var currentTime = new Date().toLocaleTimeString(Qt.locale(), "hh:mm")
                            chatLogModel.append({
                                "sender": netEngine.getSavedName(),
                                "text": messageField.text,
                                "time": currentTime
                            })
                            messageField.text = ""
                            chatListView.positionViewAtEnd()
                        }
                    }
                }
            }
        }
    }
    // ============================================================================
    // ГЛОБАЛЬНЫЕ ДИАЛОГИ (ДОЛЖНЫ ЛЕЖАТЬ В КОРНЕ ApplicationWindow)
    // ============================================================================
    Dialog {
        id: settingsDialog
        title: "Настройки"
        anchors.centerIn: parent
        width: 280
        height: 240
        modal: true

        background: Rectangle {
            color: "#1e1e1e"
            radius: 14
            border.color: "#2d2d2d"
            border.width: 1.5
        }

        header: Text {
            text: settingsDialog.title
            color: "#ffffff"
            font.pixelSize: 16
            font.bold: true
            padding: 14
            horizontalAlignment: Text.AlignHCenter
        }

        contentItem: Column {
            anchors.centerIn: parent
            spacing: 16
            width: parent.width * 0.9

            TextField {
                id: settingsNameInput
                width: parent.width
                height: 42
                placeholderText: "Ваше имя"
                placeholderTextColor: "#555555"
                text: savedName
                color: "#e0e0e0"
                verticalAlignment: TextInput.AlignVCenter
                leftPadding: 10

                background: Rectangle {
                    color: "#141414"
                    border.color: settingsNameInput.activeFocus ? "#2f8ccb" : "#333333"
                    border.width: 1
                    radius: 8
                }
            }

            Switch {
                id: developerSwitch
                text: "Для разработчиков"
                width: parent.width
                checked: developer

                contentItem: Text {
                    text: developerSwitch.text
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: developerSwitch.checked ? "#2f8ccb" : "#aaaaaa"
                    leftPadding: developerSwitch.indicator.width + developerSwitch.spacing
                    verticalAlignment: Text.AlignVCenter
                }

                indicator: Rectangle {
                    implicitWidth: 44
                    implicitHeight: 22
                    x: developerSwitch.leftPadding
                    y: parent.height / 2 - height / 2
                    radius: 11
                    color: developerSwitch.checked ? "#2f8ccb" : "#2d2d2d"
                    border.color: developerSwitch.checked ? "#2f8ccb" : "#444444"
                    border.width: 1

                    Rectangle {
                        x: developerSwitch.checked ? parent.width - width - 2 : 2
                        y: parent.height / 2 - height / 2
                        width: 18
                        height: 18
                        radius: 9
                        color: "white"

                        Behavior on x {
                            NumberAnimation { duration: 120 }
                        }
                    }
                }

                onCheckedChanged: {
                    developer = checked
                    netEngine.setDeveloper(checked)
                    console.log("@@@QML developer=", developer)
                }
            }

            Button {
                width: parent.width
                height: 42
                background: Rectangle {
                    color: parent.pressed ? "#1b5e20" : "#2e7d32"
                    radius: 8
                    border.color: "#4caf50"
                    border.width: 1
                }
                contentItem: Text { text: "Сохранить"; color: "white"; font.bold: true; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: {
                    if (settingsNameInput.text.trim() !== "") {
                        netEngine.saveName(settingsNameInput.text)
                        netEngine.saveSettings()
                    }
                    settingsDialog.close()
                }
            }
        }
    }

}
