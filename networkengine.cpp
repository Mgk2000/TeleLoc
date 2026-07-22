#include "networkengine.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QDateTime>
#include <QtMath>
#include <QDebug>

NetworkEngine::NetworkEngine(QObject *parent)
    : QObject(parent)
{
    m_udpSocket = new QUdpSocket(this);
    m_sendUdpSocket = new QUdpSocket(this);
    m_audioSocket = new QUdpSocket(this);

    m_udpSocket->bind(QHostAddress::AnyIPv4, m_port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &NetworkEngine::readPendingDatagrams);

    m_audioSocket->bind(QHostAddress::AnyIPv4, m_audioPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
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
    if (targetPeerName == "Все") return; // Запрещаем масс-звонки

    m_currentActiveCallPeer = targetPeerName;

    QJsonObject json;
    json["type"] = "call_start";
    json["sender"] = m_username;
    json["target_peer"] = targetPeerName;
    broadcastDatagram(json);

    m_inCall = true;
    if (m_audioEngine) {
        m_audioEngine->startRecording();
    }
}
void NetworkEngine::acceptAudioCall(const QString &targetPeerName)
{
    m_currentActiveCallPeer = targetPeerName;

    QJsonObject json;
    json["type"] = "call_accept";
    json["sender"] = m_username;
    json["target_peer"] = targetPeerName;
    broadcastDatagram(json);

    m_inCall = true;
    if (m_audioEngine) {
        m_audioEngine->startRecording();
    }

    emit callAccepted();
}
void NetworkEngine::stopAudioCall()
{
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
    if (senderName == m_username || senderName.isEmpty()) return;

    if (type == "heartbeat") {
        QHostAddress cleanIp = senderAddress;
        bool ok;
        quint32 ipv4 = senderAddress.toIPv4Address(&ok);
        if (ok) {
            cleanIp = QHostAddress(ipv4);
        } else {
            QString ipStr = senderAddress.toString();
            if (ipStr.startsWith("::ffff:")) {
                cleanIp = QHostAddress(ipStr.mid(7));
            }
        }
        m_discoveredPeers[senderName] = PeerInfo{cleanIp, QDateTime::currentDateTime()};
        emit peerListChanged();
    }
    else if (type == "message") {
        double msgId = json["msg_id"].toDouble();
        if (m_processedMessageIds.contains(msgId)) return;

        m_processedMessageIds.append(msgId);
        if (m_processedMessageIds.size() > 100) {
            m_processedMessageIds.removeFirst();
        }

        emit messageReceived(senderName, json["text"].toString());
    }
    else if (type == "request_open_chat") {
        emit requestOpenChat(senderName);
    }
    else if (type == "call_start") {
        QString target = json["target_peer"].toString();
        if (target == m_username) {
            // Если мы уже с кем-то говорим, шлём в сеть пакет "Занято"
            if (!m_currentActiveCallPeer.isEmpty() && m_currentActiveCallPeer != senderName) {
                QJsonObject busyJson;
                busyJson["type"] = "call_busy";
                busyJson["sender"] = m_username;
                busyJson["target_peer"] = senderName;
                broadcastDatagram(busyJson);
                return;
            }

            m_currentActiveCallPeer = senderName;
            emit incomingCall(senderName);
        }
    }
    else if (type == "call_accept") {
        QString target = json["target_peer"].toString();
        if (target == m_username) {
            m_inCall = true;
            m_currentActiveCallPeer = senderName;
            if (m_audioEngine) m_audioEngine->startRecording();
            emit callAccepted();
        }
    }
    else if (type == "call_busy") {
        QString target = json["target_peer"].toString();
        if (target == m_username) {
            // Если нам ответили "Занято", сбрасываем свой вызов
            m_inCall = false;
            m_currentActiveCallPeer.clear();
            if (m_audioEngine) m_audioEngine->stop();
            emit callEnded();
            // Сюда можно будет повесить текстовый нотис "Абонент занят"
        }
    }
    else if (type == "call_end") {
        if (m_currentActiveCallPeer == senderName) {
            m_inCall = false;
            m_currentActiveCallPeer.clear();
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

    if (conferenceChanged) {
        if (m_activeCallPeers.isEmpty()) {
            m_inCall = false;
            if (m_audioEngine) {
                m_audioEngine->stop();
            }
            emit callEnded();
        } else {
            emit callAccepted();
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
    }
}
