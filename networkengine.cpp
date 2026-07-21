#include "networkengine.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QDateTime>
#include <QtMath>
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
            m_audioSocket->readDatagram(datagram.data(), datagram.size());

            if (m_inCall && m_audioEngine) {
                long long sum = 0;
                const qint16 *samples = reinterpret_cast<const qint16*>(datagram.constData());
                int sampleCount = datagram.size() / 2;
                for (int i = 0; i < sampleCount; ++i) {
                    sum += static_cast<long long>(samples[i]) * samples[i];
                }
                double rms = (sampleCount > 0) ? qSqrt(static_cast<double>(sum) / sampleCount) : 0.0;

                double normalized = qPow(rms / 32767.0, 1.0 / 3.0);
                // Симметрично снижаем чувствительность для входящего сетевого звука
                int targetLevel = qMin(100, static_cast<int>(normalized * 100.0 * 1.4));

                int newNetLevel = m_netLevel;
                if (targetLevel > m_netLevel) {
                    newNetLevel = (m_netLevel * 30 + targetLevel * 70) / 100;
                } else {
                    newNetLevel = (m_netLevel * 90 + targetLevel * 10) / 100;
                }

                if (m_netLevel != newNetLevel) {
                    m_netLevel = newNetLevel;
                    emit netLevelChanged();
                }

                m_audioEngine->playFrame(datagram);
            }
        }
    });

    m_audioEngine = new AudioEngine(this);
    connect(m_audioEngine, &AudioEngine::frameReady, this, &NetworkEngine::handleAudioFrameReady);
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

    QJsonDocument doc(json);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

#ifdef _WIN32
    broadcastDatagram(json);
#else
    broadcastDatagram(json);
#endif

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
    if (!m_activeCallPeers.contains(targetPeerName) && targetPeerName != "Все") {
        m_activeCallPeers.append(targetPeerName);
    }

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

void NetworkEngine::stopAudioCall()
{
    QJsonObject json;
    json["type"] = "call_end";
    json["sender"] = m_username;
    json["target_peer"] = "Все";
    broadcastDatagram(json);

    m_inCall = false;
    m_activeCallPeers.clear();
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

        QString target = json["target_peer"].toString();
        if (target == m_username || target == "Все" || m_username == "Пользователь") {
            emit messageReceived(senderName, json["text"].toString());
        }
    }
    else if (type == "request_open_chat") {
        emit requestOpenChat(senderName);
    }
    else if (type == "call_start") {
        QString target = json["target_peer"].toString();
        if (target == m_username || target == "Все") {
            if (!m_activeCallPeers.contains(senderName)) {
                m_activeCallPeers.append(senderName);
            }
            emit incomingCall(senderName);
        }
    }
    else if (type == "call_accept") {
        QString target = json["target_peer"].toString();
        if (target == m_username || target == "Все") {
            m_inCall = true;
            if (!m_activeCallPeers.contains(senderName)) {
                m_activeCallPeers.append(senderName);
            }
            if (m_audioEngine) m_audioEngine->startRecording();
            emit callAccepted();
        }
    }
    else if (type == "call_end") {
        if (m_activeCallPeers.contains(senderName)) {
            m_activeCallPeers.removeOne(senderName);
            if (m_activeCallPeers.isEmpty()) {
                m_inCall = false;
                if (m_audioEngine) m_audioEngine->stop();
                emit callEnded();
            }
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
            if (m_audioEngine) m_audioEngine->stop();
            emit callEnded();
        } else {
            emit callAccepted();
        }
    }
}

void NetworkEngine::handleAudioFrameReady(const QByteArray &frame)
{
    if (frame.isEmpty() || !m_inCall || m_activeCallPeers.isEmpty()) return;

    int targetBytes = frame.size();
    int targetSamples = targetBytes / 2;

    long long sum = 0;
    const qint16 *samples = reinterpret_cast<const qint16*>(frame.constData());
    int sampleCount = frame.size() / 2;
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

    for (const QString &peerName : m_activeCallPeers) {
        if (m_discoveredPeers.contains(peerName)) {
            QHostAddress peerIp = m_discoveredPeers[peerName].address;
            bool ok;
            quint32 ipv4 = peerIp.toIPv4Address(&ok);
            if (ok) {
                peerIp = QHostAddress(ipv4);
            }
            m_audioSocket->writeDatagram(frame, peerIp, m_audioPort);
        }
    }

    QByteArray mixedFrame;
    mixedFrame.resize(targetBytes);
    qint16 *mixedSamples = reinterpret_cast<qint16*>(mixedFrame.data());
    std::fill(mixedSamples, mixedSamples + targetSamples, 0);

    QHashIterator<QString, QByteArray> i(m_audioBuffers);
    while (i.hasNext()) {
        i.next();
        QByteArray &buf = m_audioBuffers[i.key()];
        if (buf.size() >= targetBytes) {
            const qint16 *peerSamples = reinterpret_cast<const qint16*>(buf.constData());
            for (int s = 0; s < targetSamples; ++s) {
                int mixed = mixedSamples[s] + peerSamples[s];
                if (mixed > 32710) mixed = 32710;
                if (mixed < -32710) mixed = -32710;
                mixedSamples[s] = static_cast<qint16>(mixed);
            }
            buf.remove(0, targetBytes);
        }
    }

    if (m_audioEngine) {
        m_audioEngine->playFrame(mixedFrame);
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
void NetworkEngine::acceptAudioCall(const QString &targetPeerName)
{
    if (!m_activeCallPeers.contains(targetPeerName) && targetPeerName != "Все") {
        m_activeCallPeers.append(targetPeerName);
    }

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
