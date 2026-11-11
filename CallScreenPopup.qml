// qmllint disable import-unresolved
import QtQuick
// qmllint disable import-unresolved
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: callScreenPopup
    width: parent ? parent.width : 360
    height: parent ? parent.height : 640 //parent ? (developer ? parent.height - 150 : parent.height) : 640
    modal: !developer
    focus: true
    closePolicy: Popup.NoAutoClose

    x: 0
    y: 0 //developer ? 150 : 0

    parent: Overlay.overlay

    // Локальные свойства для динамического сглаживания индикаторов громкости
    property int liveMicVolume: 0
    property int liveSpeakerVolume: 0

    property int micLevel
    property int speakerLevel
    property string callerName: ""
    property int callNetType: -1
    property bool isIncoming: true

    function setSpekVal(val) {
        speakerSpinBox.value = val
        speakerLevel = val
    }
    function setMicVal(val) {
        micSpinBox.value = val
        micLevel = val
    }

    // СВЯЗЫВАЕМ СИГНАЛЫ ЖИВОЙ ГРОМКОСТИ С ИНДИКАТОРАМИ VU-METER
    Connections {
        target: audioEngine

        // Сигнал микрофона из C++ воркера
        function onMicVolumeUpdated(volume) {
            callScreenPopup.liveMicVolume = volume
        }

        // Сигнал динамика из C++ воркера
        function onNetVolumeUpdated(volume) {
            callScreenPopup.liveSpeakerVolume = volume
        }

        function onRemoteSetMicLevel(level){
            console.log("@@@sound QML RemoteSetMicLevel ", level)
            micLevel = level
        }

        function onRemoteSetSpeakerLevel(level){
            console.log("@@@sound QML RemoteSpeakerLevel ", level)
            speakerLevel = level
        }
    }

    background: Rectangle {
        color: "#291444" // Глубокий графитовый фон вместо старого ядовито-зеленого
        anchors.fill: parent
    }

    contentItem: Item {
        anchors.fill: parent

        // --- ВЕРХНЯЯ ЧАСТЬ: Иконка телефона ---
        Rectangle {
            id: iconContainer
            width: 120
            height: 120
            radius: 60
            color: callingColor
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: developer ? parent.height * 0.52 : parent.height * 0.2
            Text {
                text: "📞"
                font.pixelSize: 50
                anchors.centerIn: parent

                SequentialAnimation on scale {
                    running: callScreenPopup.isIncoming && callScreenPopup.visible
                    loops: Animation.Infinite
                    PropertyAnimation { to: 1.1; duration: 600; easing.type: Easing.InOutQuad }
                    PropertyAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutQuad }
                }
            }
        }

        // --- РЕГУЛЯТОР МИКРОФОНА + РАБОЧИЙ ИНДИКАТОР ---
        Row {
            id: micController
            visible: developer
            spacing: 10
            anchors.right: iconContainer.left
            anchors.rightMargin: 20
            anchors.verticalCenter: iconContainer.verticalCenter

            // Настоящий работающий VU-Meter микрофона
            Rectangle {
                width: 6
                height: 64
                color: "#252525" // Тёмная подложка
                radius: 3
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    width: parent.width
                    // Привязываем высоту строго к нашему локальному свойству liveMicVolume
                    height: parent.height * (callScreenPopup.liveMicVolume / 100.0)
                    color: callScreenPopup.liveMicVolume > 85 ? "#ef5350" : "#0fa958" // Краснеет при перегрузке
                    radius: parent.radius
                    anchors.bottom: parent.bottom

                    Behavior on height {
                        NumberAnimation { duration: 100; easing.type: Easing.OutQuad } // Плавный спад пиков
                    }
                }
            }

            Column {
                spacing: 5
                Text {
                    text: "🎙 Мик: " + callScreenPopup.micLevel + "%"
                    color: "#aaaaaa"
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                GreenSpinBox {
                    id: micSpinBox
                    from: 0; to: 100; value: callScreenPopup.micLevel
                    editable: true; stepSize: 5

                    onValueModified: {
                        micLevel = value
                        audioEngine.setMicLevel(value, true)
                    }
                }
            }
        }

        // --- РЕГУЛЯТОР ДИНАМИКОВ + РАБОЧИЙ ИНДИКАТОР ---
        Row {
            id: speakerController
            visible: developer
            spacing: 10
            anchors.left: iconContainer.right
            anchors.leftMargin: 20
            anchors.verticalCenter: iconContainer.verticalCenter

            Column {
                spacing: 5
                Text {
                    text: "🔊 Звк: " + callScreenPopup.speakerLevel + "%"
                    color: "#aaaaaa"
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                GreenSpinBox {
                    id: speakerSpinBox
                    from: 0; to: 100;
                    value: callScreenPopup.speakerLevel // ИСПРАВЛЕНО: Убрана опечатка micLevel!
                    editable: true; stepSize: 5

                    onValueModified: {
                        callScreenPopup.speakerLevel = value
                        audioEngine.setSpeakerLevel(value, true)
                    }
                }
            }

            // Настоящий работающий VU-Meter динамика
            Rectangle {
                width: 6
                height: 64
                color: "#252525"
                radius: 3
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    width: parent.width
                    // Привязываем высоту строго к liveSpeakerVolume
                    height: parent.height * (callScreenPopup.liveSpeakerVolume / 100.0)
                    color: callScreenPopup.liveSpeakerVolume > 85 ? "#ef5350" : "#2f8ccb" // Синеет/краснеет при воспроизведении
                    radius: parent.radius
                    anchors.bottom: parent.bottom

                    Behavior on height {
                        NumberAnimation { duration: 100; easing.type: Easing.OutQuad }
                    }
                }
            }
        }
        // --- СРЕДНЯЯ ЧАСТЬ: Имя абонента и статус ---
        Column {
            id: infoColumn
            anchors.top: iconContainer.bottom
            anchors.topMargin: 40
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            width: parent.width * 0.8

            Text {
                text: callScreenPopup.callerName
                color: "#ffffff"
                font.pixelSize: 28
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignHCenter
                anchors.horizontalCenter: parent.horizontalCenter
                elide: Text.ElideRight
            }

            Text {
                text: callScreenPopup.isIncoming ? "Входящий вызов..." : "Исходящий вызов..."
                color: callScreenPopup.isIncoming ? "#2f8ccb" : "#0fa958" // Приятный синий или мягкий зеленый HyperOS
                font.pixelSize: 16
                font.weight: Font.Medium
                horizontalAlignment: Text.AlignHCenter
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }

        // --- НИЖНЯЯ ЧАСТЬ: ТЁМНЫЙ ТРЁХПОЗИЦИОННЫЙ СЛАЙДЕР ---
        Item {
            id: sliderContainer
            width: parent.width * 0.85
            height: 66
            anchors.bottom: parent.bottom
            anchors.bottomMargin: parent.height * 0.08
            anchors.horizontalCenter: parent.horizontalCenter

            Rectangle {
                anchors.fill: parent
                color: "#1e1e1e" // Стильная глубокая подложка мессенджера вместо старого зеленого
                radius: height / 2
                border.color: "#2d2d2d"
                border.width: 1.5

                Text {
                    text: callScreenPopup.isIncoming ? "◀ Отклонить" : "◀ Отмена"
                    color: "#888888"
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    anchors.left: parent.left
                    anchors.leftMargin: 24
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: callScreenPopup.isIncoming ? "Принять ▶" : "Вызов ▶"
                    color: "#888888"
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    anchors.right: parent.right
                    anchors.rightMargin: 24
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // Ползунок свайпа вызова
            Rectangle {
                id: handle
                width: 62
                height: 62
                radius: 31
                color: "#252525" // Стилизованный тёмный круг ползунка вместо белого
                border.color: "#444444"
                border.width: 1
                anchors.verticalCenter: parent.verticalCenter

                x: (sliderContainer.width - width) / 2

                Text {
                    text: "↔"
                    font.pixelSize: 22
                    font.bold: true
                    // Динамическая смена цвета стрелок при перетаскивании
                    color: handle.x < ((sliderContainer.width - handle.width) / 2 - 20) ? "#ef5350" : // Мягкий красный
                           handle.x > ((sliderContainer.width - handle.width) / 2 + 20) ? "#0fa958" : "#888888" // Мягкий зеленый / нейтральный
                    anchors.centerIn: parent
                }

                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    drag.target: handle
                    drag.axis: Drag.XAxis
                    drag.minimumX: 2
                    drag.maximumX: sliderContainer.width - handle.width - 2

                    onReleased: {
                        var midX = (sliderContainer.width - handle.width) / 2;
                        var threshold = sliderContainer.width * 0.25;

                        if (handle.x > midX + threshold) {
                            handle.x = sliderContainer.width - handle.width - 2;

                            if (callScreenPopup.isIncoming) {
                                console.log("$$$ incoming accept 1");
                                window.activeConferencePeers = callScreenPopup.callerName;
                                window.activeCallNetType = callScreenPopup.callNetType;
                                console.log("$$$ incoming accept 2", callScreenPopup.callerName, callScreenPopup.callNetType );
                                netEngine.acceptAudioCall(callScreenPopup.callerName, callScreenPopup.callNetType);
                                console.log("$$$ incoming accept 3");
                            } else if (!netEngine.isCallSpeaking()){
                                netEngine.startAudioCall(callScreenPopup.callerName, callScreenPopup.callNetType)
                            }
                        }
                        else if (handle.x < midX - threshold) {
                            handle.x = 2;

                            if (callScreenPopup.isIncoming) {
                                netEngine.stopAudioCall();
                                netEngine.masterStopRecording()
                            } else {
                                netEngine.stopAudioCall();
                                netEngine.masterStopRecording()
                            }
                            callScreenPopup.close();
                        }
                        else {
                            returnToCenterAnim.start();
                        }
                    }
                }

                NumberAnimation on x {
                    id: returnToCenterAnim
                    to: (sliderContainer.width - handle.width) / 2
                    duration: 200
                    easing.type: Easing.OutQuad
                    running: false
                }
            }
        }
    }
    RowLayout {
        id: developerFooter
        y: parent.height - 60
        x:100
        visible: developer
        // Чекбокс изменения настроек удаленного абонента
        CheckBox {
            id: remoteCheckbox
            checked: false
            text: "R"
            //Layout.fillWidth: true
            Layout.preferredWidth: 60
            Layout.preferredHeight: 60
            Layout.alignment: Qt.AlignVCenter

            contentItem: Text {
                text: remoteCheckbox.text
                font.pixelSize: 30
                font.weight: Font.Medium
                color: remoteCheckbox.checked ? "#2f8ccb" : "#aaaaaa"
                leftPadding: remoteCheckbox.indicator.width + remoteCheckbox.spacing
                verticalAlignment: Text.AlignVCenter
            }

            indicator: Rectangle {
                implicitWidth: 20
                implicitHeight: 20
                x: remoteCheckbox.leftPadding
                y: parent.height / 2 - height / 2
                radius: 5
                color: remoteCheckbox.checked ? "#2f8ccb" : "transparent"
                border.color: remoteCheckbox.checked ? "#2f8ccb" : "#555555"
                border.width: 1.5

                Rectangle {
                    width: 10; height: 5
                    border.color: "white"; border.width: 1.5; color: "transparent"
                    visible: remoteCheckbox.checked
                    rotation: -45
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: -1
                }
            }

            onCheckedChanged: {
                netEngine.setRemoteControl(checked)
                var m = audioEngine.getMicLevel()
                var s = audioEngine.getSpeakerLevel()
                var rm = audioEngine.getRemoteMicLevel()
                var rs = audioEngine.getRemoteSpeakerLevel()
                if (checked) {
                    customCallScreen.setSpekVal(rs)
                    customCallScreen.setMicVal(rm)
                } else {
                    customCallScreen.setSpekVal(s)
                    customCallScreen.setMicVal(m)
                }
            }
        }
        // Кнопка 2: Запись аудио разговора (btnRecord) — Сдержанная красная
        Button {
            id: btnRecord1
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
                color: btnRecord1.checked ? "#331111" : "#252525"
                border.color: btnRecord1.checked ? "#ef5350" : "#444444"
                border.width: 1
                radius: 8
            }

            contentItem: Item {
                anchors.fill: parent
                Rectangle {
                    anchors.centerIn: parent
                    width: btnRecord1.checked ? 12 : 10
                    height: btnRecord1.checked ? 12 : 10
                    radius: btnRecord1.checked ? 2 : 5
                    color: "#ef5350"
                }
            }
        }
        Button {
            id : noisesuppreessbutton
            Layout.preferredWidth: 60
            Layout.preferredHeight: 60
            checkable: true
            checked: true
            background: Rectangle {
                color: noisesuppreessbutton.checked ? "#770000" : "#cccccc"
                border.color: noisesuppreessbutton.checked ? "#bb0000" : "#224466"
                border.width: 6
                radius: 8
            }
            contentItem: Item {
                anchors.fill: parent
                Text {
                    text : "N"
                font.pixelSize: 40; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter;}
            }
            onCheckedChanged: {
                console.log("@@@sound QML NoiseSuppress=", checked)
                //audioEngine.setNoiseSuppress(checked)
                audioEngine.setNoiseSuppress(checked)
            }

        }
        Button {
            id : preventwhistlebutton
            Layout.preferredWidth: 60
            Layout.preferredHeight: 60
            checkable: true
            checked: true
            background: Rectangle {
                color: preventwhistlebutton.checked ? "#770000" : "#cccccc"
                border.color: preventwhistlebutton.checked ? "#bb0000" : "#224466"
                border.width: 6
                radius: 8
            }
            contentItem: Item {
                anchors.fill: parent
                Text {
                    text : "PW"
                font.pixelSize: 28; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter;}
            }
            onCheckedChanged: {
                console.log("@@@sound QML PrevenWhistlet=", checked)
                audioEngine.setPreventWhistle(checked)
            }

        }
    }

    onOpened: {
        handle.x = (sliderContainer.width - handle.width) / 2;
        console.log("@@@sound Open call diialog")
        audioEngine.initConfigLevels()

        micSpinBox.value = callScreenPopup.micLevel
        speakerSpinBox.value = callScreenPopup.speakerLevel
        remoteCheckbox.checked = false
    }
    onClosed: {
        remoteCheckbox.checked = false
        btnRecord1.checked = false
    }
}
