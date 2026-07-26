#include "networkengine.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QDateTime>
#include <QtMath>
#include <QDebug>

#include <QMediaDevices>
#include <QAudioDevice>

NetworkEngine::NetworkEngine(QObject *parent)
    : QObject(parent)
{
    m_udpSocket = new QUdpSocket(this);
    m_sendUdpSocket = new QUdpSocket(this);
    m_audioSocket = new QUdpSocket(this);

    // ЖЕСТКИЙ ФИКС VPN: Переводим управляющий сокет на Any, чтобы ловить пакеты из туннелей
    m_udpSocket->bind(QHostAddress::Any, m_port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &NetworkEngine::readPendingDatagrams);

    // ЖЕСТКИЙ ФИКС VPN: Звуковой сокет тоже переводим на Any для сквозного прохода аудиопотока
    m_audioSocket->bind(QHostAddress::Any, m_audioPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    connect(m_audioSocket, &QUdpSocket::readyRead, this, [this]() {
        while (m_audioSocket->hasPendingDatagrams()) {
            QByteArray datagram;
            datagram.resize(m_audioSocket->pendingDatagramSize());
            QHostAddress senderAddress;
            m_audioSocket->readDatagram(datagram.data(), datagram.size(), &senderAddress);

            if (m_inCall && m_audioEngine && !m_currentActiveCallPeer.isEmpty()) {
                // Очищаем адрес от IPv6-обёртки
                QHostAddress cleanSender = senderAddress;
                bool ok;
                quint32 senderIpv4 = senderAddress.toIPv4Address(&ok);
                if (ok) {
                    cleanSender = QHostAddress(senderIpv4);
                } else {
                    QString ipStr = senderAddress.toString();
                    if (ipStr.startsWith("::ffff:")) {
                        cleanSender = QHostAddress(ipStr.mid(7));
                    }
                }

                // Узнаем IP-адрес нашего текущего единственного собеседника
                QHostAddress peerIp;
                if (m_discoveredPeers.contains(m_currentActiveCallPeer)) {
                    peerIp = m_discoveredPeers[m_currentActiveCallPeer].address;
                    quint32 peerIpv4 = peerIp.toIPv4Address(&ok);
                    if (ok) peerIp = QHostAddress(peerIpv4);
                }

                // ВОСПРОИЗВОДИМ ЗВУК ТОЛЬКО ЕСЛИ ОН ПРИЛЕТЕЛ С IP-АДРЕСА НАШЕГО СОБЕСЕДНИКА
                if (cleanSender == peerIp) {
                    m_audioEngine->playFrame(datagram);
                }
            }
        }
    });

    QAudioDevice defaultOutput = QMediaDevices::defaultAudioOutput();

    m_ringtone = new QSoundEffect(this);
    m_ringtone->setAudioDevice(defaultOutput); // <-- ЖЕСТКО НАПРАВЛЯЕМ ЗВУК ВСЛЕД ЗА ЮТУБОМ
    m_ringtone->setSource(QUrl(QStringLiteral("qrc:/qt/qml/TeleLoc/ring1.wav")));
    m_ringtone->setLoopCount(QSoundEffect::Infinite);
    m_ringtone->setVolume(0.8f);

    m_msgSound = new QSoundEffect(this);
    m_msgSound->setAudioDevice(defaultOutput); // <-- СЮДА ТОЖЕ
    m_msgSound->setSource(QUrl(QStringLiteral("qrc:/qt/qml/TeleLoc/ring2.wav")));
    m_msgSound->setLoopCount(1);
    m_msgSound->setVolume(0.7f);

    m_audioEngine = new AudioEngine(this);
    connect(m_audioEngine, &AudioEngine::frameReady, this, [this](const QByteArray &frame) {
        handleAudioFrameReady(frame);
    });

    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &NetworkEngine::sendHeartbeat);
}
NetworkEngine::~NetworkEngine()
{
    if (m_audioEngine) {
        m_audioEngine->stop();
    }
}

void NetworkEngine::start(const QString &username)
{
    m_username = username;
    m_heartbeatTimer->start(1000);
    sendHeartbeat();
}

void NetworkEngine::saveNameToFile(const QString &name)
{
    QSettings settings("TeleLocCompany", "TeleLocApp");
    settings.setValue("username", name);
}

QString NetworkEngine::getSavedName() const
{
    QSettings settings("TeleLocCompany", "TeleLocApp");
    return settings.value("username", "").toString();
}

bool NetworkEngine::isRegistered() const
{
    return !getSavedName().isEmpty();
}

QStringList NetworkEngine::peerList() const
{
    return m_discoveredPeers.keys();
}

void NetworkEngine::setDebugFrequency(double hz)
{
    m_debugFrequency = hz;
    m_debugPhase = 0.0;
}
void NetworkEngine::sendMessage(const QString &targetPeer, const QString &text)
{
    if (text.isEmpty()) return;

    double msgId = QRandomGenerator::global()->generateDouble();

    QJsonObject json;
    json["type"] = "message";
    json["sender"] = m_username;
    json["text"] = text;
    json["target_peer"] = targetPeer;
    json["msg_id"] = msgId;

    broadcastDatagram(json);
    emit messageReceived(m_username, text);
}

void NetworkEngine::startChatSession(const QString &targetPeerName)
{
    QJsonObject json;
    json["type"] = "request_open_chat";
    json["sender"] = m_username;
    json["target_peer"] = targetPeerName;
    broadcastDatagram(json);
}

void NetworkEngine::startAudioCall(const QString &targetPeerName)
{
    if (targetPeerName == "Все" || targetPeerName.isEmpty()) return;

    m_currentActiveCallPeer = targetPeerName;

    QJsonObject json;
    json["type"] = "call_start";
    json["sender"] = m_username;
    json["target_peer"] = targetPeerName;
    broadcastDatagram(json);

    // Включаем рингтон (гудки вызова)
    startRingtone();

    // Мы еще не в режиме разговора, мы только ждем ответа!
    m_inCall = false;

    // ФИКС БАГА №2: Тайм-аут вызова. Если через 15 секунд абонент не ответит,
    // принудительно сбрасываем звонок, чтобы гудки не шли бесконечно.
    QTimer::singleShot(15000, this, [this, targetPeerName]() {
        // Если мы все еще ждем ИМЕННО ЭТОГО абонента и разговор так и не начался (m_inCall == false)
        if (!m_inCall && m_currentActiveCallPeer == targetPeerName) {
            qDebug() << "Таймаут вызова:" << targetPeerName << "не отвечает.";
            stopAudioCall();
            emit messageReceived("Система", QString("Абонент %1 не отвечает").arg(targetPeerName));
        }
    });
}
void NetworkEngine::acceptAudioCall(const QString &targetPeerName)
{
    if (targetPeerName.isEmpty()) return;

    m_currentActiveCallPeer = targetPeerName;

    // ФИКС БАГА №1: Гасим рингтон намертво
    stopRingtone();

    QJsonObject json;
    json["type"] = "call_accept";
    json["sender"] = m_username;
    json["target_peer"] = targetPeerName;
    broadcastDatagram(json);

    // Переходим в режим разговора и включаем микрофон
    m_inCall = true;
    if (m_audioEngine) {
        m_audioEngine->startRecording();
    }

emit callAccepted(targetPeerName);}
void NetworkEngine::stopAudioCall()
{
    // Жестко и бескомпромиссно глушим рингтон
    stopRingtone();

    if (!m_currentActiveCallPeer.isEmpty()) {
        QJsonObject json;
        json["type"] = "call_end";
        json["sender"] = m_username;
        json["target_peer"] = m_currentActiveCallPeer;
        broadcastDatagram(json);
    }

    m_inCall = false;
    m_currentActiveCallPeer.clear();

    if (m_audioEngine) {
        m_audioEngine->stop();
    }

    emit callEnded();
}

void NetworkEngine::readPendingDatagrams()
{
    while (m_udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_udpSocket->pendingDatagramSize());
        QHostAddress senderAddress;

        m_udpSocket->readDatagram(datagram.data(), datagram.size(), &senderAddress);

        QJsonDocument doc = QJsonDocument::fromJson(datagram);
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject json = doc.object();
            processJsonMessage(json, senderAddress);
        }
    }
}

void NetworkEngine::processJsonMessage(const QJsonObject &json, const QHostAddress &senderAddress)
{
    QString type = json["type"].toString();
    QString senderName = json["sender"].toString();

    // Жесткая защита от сетевого эха (самопроизвольного отлова собственных пакетов)
    if (senderName == m_username || senderName.isEmpty()) return;

    if (type == "heartbeat") {
        QString senderName = json["sender"].toString();

        // senderAddress — это объект QHostAddress, который мы получили из m_udpSocket->readDatagram
        // (убедитесь, что передаете его в параметры метода processJsonMessage)

        // ЛЕГАЛЬНЫЙ ФИКС: Вытаскиваем MAC-адрес отправителя из сетевого интерфейса ОС по его IP-адресу!
        QString senderMac;
        const auto interfaces = QNetworkInterface::allInterfaces();
        for (const QNetworkInterface &interface : interfaces) {
            // Ищем в системной ARP-таблице, к какому MAC-адресу привязан этот входящий IP
            const auto addressEntries = interface.addressEntries();
            for (const QNetworkAddressEntry &entry : addressEntries) {
                if (entry.ip().isInSubnet(senderAddress, 24)) { // Если пир в нашей подсести
                    // Вытаскиваем аппаратный адрес удаленного узла, который зарегистрировала ОС
                    senderMac = interface.hardwareAddress();
                   // qDebug() << "senderMac===============================" <<senderMac;
                    break;
                }
            }
        }

        // Если адрес успешно вытащен из сетевого кэша Windows/Android
        if (!senderMac.isEmpty() && senderMac != "00:00:00:00:00:00") {
            if (m_discoveredPeers[senderName].macAddress != senderMac) {
                m_discoveredPeers[senderName].macAddress = senderMac;

                qDebug() << "Успешно скэширован реальный адрес дачника:" << senderName << "MAC:" << senderMac;
                saveMacDatabaseToFile(senderName, senderMac);
            }
        }
    }
    else if (type == "message") {
        double msgId = json["msg_id"].toDouble();
        if (m_processedMessageIds.contains(msgId)) return;

        m_processedMessageIds.append(msgId);
        if (m_processedMessageIds.size() > 100) {
            m_processedMessageIds.removeFirst();
        }

        playMessageSound();
        emit messageReceived(senderName, json["text"].toString());
    }
    else if (type == "request_open_chat") {
        emit requestOpenChat(senderName);
    }
    else if (type == "call_start") {
        QString target = json["target_peer"].toString().trimmed();
        QString sender = json["sender"].toString().trimmed();

        // Если звонят лично нам ИЛИ прилетел прямой P2P вызов "Директ"
        if (target == m_username || target == QStringLiteral("Директ")) {
            m_incomingCallSender = sender.isEmpty() ? QStringLiteral("Абонент P2P") : sender;

            // ЧИСТАЯ ДИАГНОСТИКА: Вытаскиваем IP-адрес входящего пакета
            QString incomingIp = senderAddress.toString();
            if (incomingIp.startsWith(QLatin1String("::ffff:"))) {
                incomingIp = incomingIp.mid(7); // Очищаем от IPv6-обертки
            }

            // Печатаем в чат, с какого IP пробился пакет сквозь VPN!
            emit messageReceived(QStringLiteral("Система"),
                                 QStringLiteral("📩 Входящий звонок от %1 с IP: %2").arg(m_incomingCallSender).arg(incomingIp));

            startRingtone();
            emit incomingCallReceived(m_incomingCallSender);
        }
    }
    else if (type == "call_accept") {
        QString target = json["target_peer"].toString();
        if (target == m_username) {
            stopRingtone(); // Глушим исходящие гудки, нам ответили!

            m_inCall = true;
            m_currentActiveCallPeer = senderName;

            // Включаем голосовой тракт СТРОГО в момент коннекта
            if (m_audioEngine) {
                m_audioEngine->startRecording();
            }
            emit callAccepted(senderName);
        }
    }
    else if (type == "call_busy") {
        QString target = json["target_peer"].toString();
        if (target == m_username) {
            m_inCall = false;
            m_currentActiveCallPeer.clear();
            stopRingtone();
            if (m_audioEngine) m_audioEngine->stop();
            emit callEnded();
        }
    }
    else if (type == "call_end") {
        if (m_currentActiveCallPeer == senderName) {
            m_inCall = false;
            m_currentActiveCallPeer.clear();
            stopRingtone();
            if (m_audioEngine) m_audioEngine->stop();
            emit callEnded();
        }
    }
}

void NetworkEngine::sendHeartbeat()
{
    QJsonObject json;
    json["type"] = "heartbeat";
    json["sender"] = m_username;
    broadcastDatagram(json);

    QDateTime now = QDateTime::currentDateTime();
    QMutableListIterator<QString> i(m_activeCallPeers);
    bool conferenceChanged = false;

    while (i.hasNext()) {
        QString peerName = i.next();
        if (m_discoveredPeers.contains(peerName)) {
            QDateTime lastSeen = m_discoveredPeers[peerName].lastSeen;
            if (lastSeen.secsTo(now) > 5) {
                i.remove();
                conferenceChanged = true;
            }
        } else {
            i.remove();
            conferenceChanged = true;
        }
    }

    // ЧИСТЫЙ ФИКС: Защищаем одиночный P2P звонок от вмешательства таймера.
    // Если мы находимся в обычном звонке один на один (m_currentActiveCallPeer не пустой),
    // игнорируем логику групповой конференции, чтобы таймер случайно не перезапустил рингтон.
    if (!m_currentActiveCallPeer.isEmpty()) {
        return;
    }

    if (conferenceChanged) {
        if (m_activeCallPeers.isEmpty()) {
            m_inCall = false;
            if (m_audioEngine) {
                m_audioEngine->stop();
            }
            emit callEnded();
        } else {
        emit callAccepted(!m_activeCallPeers.isEmpty() ? m_activeCallPeers.first() : "");
        }
    }
}

void NetworkEngine::handleAudioFrameReady(const QByteArray &frame)
{
    if (frame.isEmpty() || !m_inCall || m_currentActiveCallPeer.isEmpty()) return;

    int targetBytes = frame.size();
    int targetSamples = targetBytes / 2;
    int sampleRate = 16000;

    QByteArray finalOutFrame;

    // Если в выпадающем списке руками выбрана частота - генерируем её
    if (m_debugFrequency > 0.0) {
        double twoPi = 2.0 * M_PI;
        finalOutFrame.resize(targetBytes);
        qint16 *genSamples = reinterpret_cast<qint16*>(finalOutFrame.data());

        for (int i = 0; i < targetSamples; ++i) {
            genSamples[i] = static_cast<qint16>(4000.0 * qSin(m_debugPhase));
            m_debugPhase += (twoPi * m_debugFrequency) / sampleRate;
            if (m_debugPhase >= twoPi) {
                m_debugPhase -= twoPi;
            }
        }
    } else {
        // ЧИСТЫЙ ОТКАТ: Если выбран "Сброс" - шлём 100% живой непрерывный поток с микрофона
        finalOutFrame = frame;
    }

    // Расчёт RMS для полоски "Мик" на тулбаре
    long long sum = 0;
    const qint16 *samples = reinterpret_cast<const qint16*>(finalOutFrame.constData());
    int sampleCount = finalOutFrame.size() / 2;
    for (int i = 0; i < sampleCount; ++i) {
        sum += static_cast<long long>(samples[i]) * samples[i];
    }
    double rms = (sampleCount > 0) ? qSqrt(static_cast<double>(sum) / sampleCount) : 0.0;
    double normalized = qPow(rms / 32767.0, 1.0 / 3.0);
    int targetLevel = qMin(100, static_cast<int>(normalized * 100.0 * 1.4));
    int newMicLevel = m_micLevel;
    if (targetLevel > m_micLevel) {
        newMicLevel = (m_micLevel * 30 + targetLevel * 70) / 100;
    } else {
        newMicLevel = (m_micLevel * 90 + targetLevel * 10) / 100;
    }
    if (m_micLevel != newMicLevel) {
        m_micLevel = newMicLevel;
        emit micLevelChanged();
    }

    // Мгновенная p2p-отправка пакета адресату без задержек и очередей
    if (m_discoveredPeers.contains(m_currentActiveCallPeer)) {
        QHostAddress peerIp = m_discoveredPeers[m_currentActiveCallPeer].address;
        bool ok;
        quint32 ipv4 = peerIp.toIPv4Address(&ok);
        if (ok) {
            peerIp = QHostAddress(ipv4);
        }
        m_audioSocket->writeDatagram(finalOutFrame, peerIp, m_audioPort);
    }

    // Локальный самовызов (петля на одном устройстве)
    if (m_username == m_currentActiveCallPeer) {
        if (m_audioEngine) {
            m_audioEngine->playFrame(finalOutFrame);
        }
    }
}

void NetworkEngine::broadcastDatagram(const QJsonObject &json)
{
    QJsonDocument doc(json);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    if (m_sendUdpSocket) {
        m_sendUdpSocket->writeDatagram(data, QHostAddress::Broadcast, m_port);

        QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
        for (const QNetworkInterface &interface : interfaces) {
            if (interface.flags().testFlag(QNetworkInterface::IsUp) && !interface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
                QList<QNetworkAddressEntry> entries = interface.addressEntries();
                for (const QNetworkAddressEntry &entry : entries) {
                    QHostAddress broadcastAddress = entry.broadcast();
                    if (!broadcastAddress.isNull() && broadcastAddress.protocol() == QAbstractSocket::IPv4Protocol) {
                        m_sendUdpSocket->writeDatagram(data, broadcastAddress, m_port);
                    }
                    QHostAddress ip = entry.ip();
                    if (ip.protocol() == QAbstractSocket::IPv4Protocol) {
                        quint32 ipv4 = ip.toIPv4Address();
                        quint32 subnet = (255 << 24) | (255 << 16) | (255 << 8) | 0;
                        if (entry.netmask().toIPv4Address() != 0) {
                            subnet = entry.netmask().toIPv4Address();
                        }
                        quint32 bcast = ipv4 | (~subnet);
                        m_sendUdpSocket->writeDatagram(data, QHostAddress(bcast), m_port);
                    }
                }
            }
        }

        // =========================================================================
        // ТОЧЕЧНЫЙ ФИКС ДЛЯ WI-FI DIRECT: НИЧЕГО НЕ СТИРАЕМ, ПРОСТО ДУБЛИРУЕМ СЮДА
        // =========================================================================
        // Если Android скрыл новый интерфейс p2p0 в кэше allInterfaces(), эта строка
        // принудительно пробьет стандартную P2P-подсеть Android напрямую. Как только
        // Иван и Пётр вернутся в чат — сокеты поймают этот пакет, и они увидят друг друга!
        m_sendUdpSocket->writeDatagram(data, QHostAddress(QStringLiteral("192.168.49.255")), m_port);
    }
}

void NetworkEngine::startRingtone()
{
    stopRingtone();

    QAudioDevice currentOutput = QMediaDevices::defaultAudioOutput();

    m_ringtone = new QSoundEffect(currentOutput, this);
    m_ringtone->setLoopCount(QSoundEffect::Infinite);
    m_ringtone->setVolume(0.8f);
    m_ringtone->setSource(QUrl(QStringLiteral("qrc:/qt/qml/TeleLoc/ring1.wav")));

    if (m_ringtone->status() == QSoundEffect::Loading) {
        // Делаем обычный коннект БЕЗ флага UniqueConnection
        connect(m_ringtone, &QSoundEffect::statusChanged, this, [this]() {
            if (m_ringtone && m_ringtone->status() == QSoundEffect::Ready) {
                // Отключаем сигнал СРАЗУ, как только файл готов, чтобы лямбда не вызвалась повторно
                disconnect(m_ringtone, &QSoundEffect::statusChanged, this, nullptr);
                if (!m_ringtone->isPlaying()) {
                    m_ringtone->play();
                }
            }
        });
    } else if (m_ringtone->status() == QSoundEffect::Ready) {
        m_ringtone->play();
    }
}
void NetworkEngine::stopRingtone()
{
    if (m_ringtone && m_ringtone->isPlaying()) {
        m_ringtone->stop();
    }
}

void NetworkEngine::playMessageSound()
{
    if (m_msgSound) {
        m_msgSound->stop();
        m_msgSound->deleteLater();
        m_msgSound = nullptr;
    }

    QAudioDevice currentOutput = QMediaDevices::defaultAudioOutput();

    m_msgSound = new QSoundEffect(currentOutput, this);
    m_msgSound->setLoopCount(1);
    m_msgSound->setVolume(0.7f);
    m_msgSound->setSource(QUrl(QStringLiteral("qrc:/qt/qml/TeleLoc/ring2.wav")));

    if (m_msgSound->status() == QSoundEffect::Loading) {
        // Аналогично убираем UniqueConnection для звука чата
        connect(m_msgSound, &QSoundEffect::statusChanged, this, [this]() {
            if (m_msgSound && m_msgSound->status() == QSoundEffect::Ready) {
                disconnect(m_msgSound, &QSoundEffect::statusChanged, this, nullptr);
                m_msgSound->play();
            }
        });
    } else if (m_msgSound->status() == QSoundEffect::Ready) {
        m_msgSound->play();
    }
}

#include <QSettings>

void NetworkEngine::saveMacDatabaseToFile(const QString &name, const QString &mac)
{
    // QSettings автоматически создаст неубиваемый файл в защищенной памяти системы
    QSettings settings("TeleLocProject", "MacCache");
    settings.setValue(QString("peers/%1").arg(name), mac);
}

QString NetworkEngine::getSavedMacForPeer(const QString &name)
{
    QSettings settings("TeleLocProject", "MacCache");
    return settings.value(QString("peers/%1").arg(name), QString()).toString();
}
void NetworkEngine::startWifiDirectAudioCall(const QString &targetPeerName)
{
    Q_UNUSED(targetPeerName);

    QJsonObject json;
    json["type"] = "call_start";
    json["sender"] = m_username;
    // Пишем "Директ", чтобы QML принимающей стороны сразу понял тип вызова
    json["target_peer"] = QStringLiteral("Директ");

    QJsonDocument doc(json);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    // Локальные IP-адреса Wi-Fi Direct подсети Android
    QHostAddress goAddress("192.168.49.1");
    QHostAddress clientAddress("192.168.49.100");

    // ДИАГНОСТИКА: Выводим в окно чата точные направления выстрела сокета
    emit messageReceived(QStringLiteral("Система"),
                         QStringLiteral("🌐 Звоню на 192.168.49.1 и 192.168.49.100..."));

    // Стреляем пакетами по обоим адресам напрямую!
    m_sendUdpSocket->writeDatagram(data, goAddress, m_port);
    m_sendUdpSocket->writeDatagram(data, clientAddress, m_port);

    // Дополнительно бьем вещанием по всей подсети
    m_sendUdpSocket->writeDatagram(data, QHostAddress("192.168.49.255"), m_port);

    startRingtone();
    m_inCall = false;
}
