#include "networkengine.h"
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QNetworkInterface>
#include <QDebug>

NetworkEngine::NetworkEngine(QObject *parent)
    : QObject(parent), m_port(45454), m_isRegistered(false), m_activeChatPeer(""), m_callStatus("IDLE"), m_audioEngine(nullptr)
{
    m_socket = new QUdpSocket(this);
    m_socket->bind(QHostAddress::AnyIPv4, m_port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    m_subnetBroadcast = getRealHardwareAddress();

    connect(m_socket, &QUdpSocket::readyRead, this, &NetworkEngine::readPendingDatagrams);
    loadNameFromFile();
    loadQueueFromFile();

    m_pingTimer = new QTimer(this);
    connect(m_pingTimer, &QTimer::timeout, this, &NetworkEngine::onPingTimer);
    m_pingTimer->start(10000);
}

QHostAddress NetworkEngine::getRealHardwareAddress() const {
    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        if (!(interface.flags() & QNetworkInterface::IsUp) || !(interface.flags() & QNetworkInterface::IsRunning)) continue;
        if (interface.flags() & QNetworkInterface::IsLoopBack) continue;

        QString name = interface.name().toLower();
        QString desc = interface.humanReadableName().toLower();

        if (name.contains("tun") || name.contains("tap") || name.contains("vpn") || name.contains("ppp") || desc.contains("vpn") || desc.contains("wireguard")) continue;

        if (name.contains("wlan") || name.contains("wifi") || name.contains("usb") || name.contains("rndis") || name.contains("eth") || desc.contains("wireless") || desc.contains("wi-fi")) {
            for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    QHostAddress bcast = entry.broadcast();
                    if (!bcast.isNull() && bcast != QHostAddress::Null) return bcast;
                }
            }
        }
    }
    return QHostAddress::Broadcast;
}

QString NetworkEngine::getConfigPath(const QString &fileName) const {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    return path + "/" + fileName;
}

void NetworkEngine::loadNameFromFile() {
    QFile file(getConfigPath("teleloc.conf"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file); m_myName = in.readAll().trimmed(); file.close();
        if (!m_myName.isEmpty()) { m_isRegistered = true; emit myNameChanged(); emit isRegisteredChanged(); }
    }
}

void NetworkEngine::saveNameToFile(const QString &name) {
    if (name.trimmed().isEmpty()) return;
    QFile file(getConfigPath("teleloc.conf"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file); out << name.trimmed(); file.close();
        m_myName = name.trimmed(); m_isRegistered = true;
        emit myNameChanged(); emit isRegisteredChanged();
    }
}

void NetworkEngine::setMyName(const QString &name) {
    if (m_myName != name) { m_myName = name; emit myNameChanged(); }
}

void NetworkEngine::resetRegistration() {
    rejectOrEndCall();
    QFile file(getConfigPath("teleloc.conf")); file.remove();
    m_myName = ""; m_isRegistered = false; m_chatLog = ""; m_activeChatPeer = "";
    emit myNameChanged(); emit isRegisteredChanged(); emit chatLogChanged(); emit activeChatPeerChanged();
}

void NetworkEngine::cleanOldMessages() {
    QDateTime now = QDateTime::currentDateTime(); auto it = m_offlineQueue.begin();
    while (it != m_offlineQueue.end()) {
        if (it->timestamp.secsTo(now) > 86400) { it = m_offlineQueue.erase(it); } else { ++it; }
    }
    if (m_offlineQueue.size() > 5) { while (m_offlineQueue.size() > 5) { m_offlineQueue.removeFirst(); } }
}

void NetworkEngine::setActiveChatPeer(const QString &peer) {
    if (m_activeChatPeer != peer) {
        m_activeChatPeer = peer; emit activeChatPeerChanged(); m_chatLog = "";
        QFile file(getConfigPath("chat_" + peer + ".log"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) { QTextStream in(&file); m_chatLog = in.readAll(); file.close(); }
        if (m_chatLog.isEmpty()) m_chatLog = QString("--- Начало чата с %1 ---\n").arg(peer);
        emit chatLogChanged();
    }
}
void NetworkEngine::clearChatHistory(const QString &peer) {
    if (peer.isEmpty()) return;
    QFile file(getConfigPath("chat_" + peer + ".log"));
    file.remove();
    m_chatLog = QString("--- Начало чата с %1 ---\n").arg(peer);
    emit chatLogChanged();
}

void NetworkEngine::sendAudioPacket(const QByteArray &audioData) {
    if (m_myName.isEmpty() || m_activeChatPeer.isEmpty() || m_callStatus != "CONNECTED") return;
    // Формат пакета: AUDIO:ОТ_КОГО:КОМУ:СЫРЫЕ_БАЙТЫ
    QByteArray datagram = "AUDIO:" + m_myName.toUtf8() + ":" + m_activeChatPeer.toUtf8() + ":" + audioData;
    m_socket->writeDatagram(datagram, m_subnetBroadcast, m_port);
}

void NetworkEngine::startCall(const QString &targetName) {
    if (m_myName.isEmpty() || targetName.isEmpty() || m_callStatus != "IDLE") return;
    m_activeChatPeer = targetName; m_callStatus = "OUTGOING"; emit activeChatPeerChanged(); emit callStatusChanged();
    QByteArray datagram = "CALL_REQ:" + m_myName.toUtf8() + ":" + targetName.toUtf8();
    m_socket->writeDatagram(datagram, m_subnetBroadcast, m_port);
}

void NetworkEngine::acceptCall() {
    if (m_callStatus != "INCOMING" || m_activeChatPeer.isEmpty()) return;
    m_callStatus = "CONNECTED"; emit callStatusChanged(); emit callStarted();
    QByteArray datagram = "CALL_ACCEPT:" + m_myName.toUtf8() + ":" + m_activeChatPeer.toUtf8();
    m_socket->writeDatagram(datagram, m_subnetBroadcast, m_port);
}

void NetworkEngine::rejectOrEndCall() {
    if (m_callStatus == "IDLE") return;
    if (m_audioEngine) m_audioEngine->stop();
    QByteArray datagram = "CALL_REJECT:" + m_myName.toUtf8() + ":" + m_activeChatPeer.toUtf8();
    m_socket->writeDatagram(datagram, m_subnetBroadcast, m_port);
    m_callStatus = "IDLE"; m_activeChatPeer = ""; emit callStatusChanged(); emit activeChatPeerChanged(); emit callEnded();
}
void NetworkEngine::sendTextMessage(const QString &text) {
    if (m_myName.isEmpty() || m_activeChatPeer.isEmpty() || text.trimmed().isEmpty()) return;
    QString msgText = text.trimmed(); QString formattedLine = QString("[Вы]: %1\n").arg(msgText);
    m_chatLog += formattedLine; emit chatLogChanged();
    QFile file(getConfigPath("chat_" + m_activeChatPeer + ".log"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) { QTextStream out(&file); out << formattedLine; file.close(); }

    QByteArray datagram = "TEXT_MSG:" + m_myName.toUtf8() + ":" + m_activeChatPeer.toUtf8() + ":" + msgText.toUtf8();
    m_socket->writeDatagram(datagram, m_subnetBroadcast, m_port);

    OfflineMessage newMsg = { m_myName, m_activeChatPeer, msgText, QDateTime::currentDateTime() };
    m_offlineQueue.append(newMsg); cleanOldMessages(); saveQueueToFile();
}

void NetworkEngine::onPingTimer() {
    if (m_myName.isEmpty() || m_offlineQueue.isEmpty()) return;
    cleanOldMessages();
    for (const auto &msg : m_offlineQueue) {
        QByteArray datagram = "PING_REQ:" + m_myName.toUtf8() + ":" + msg.to.toUtf8();
        m_socket->writeDatagram(datagram, m_subnetBroadcast, m_port);
    }
}

void NetworkEngine::saveQueueToFile() {
    QFile file(getConfigPath("pending_messages.conf"));
    if (file.open(QIODevice::WriteOnly)) {
        QDataStream out(&file); out.setVersion(QDataStream::Qt_6_5); out << m_offlineQueue.size();
        for (const auto &msg : m_offlineQueue) out << msg.from << msg.to << msg.text << msg.timestamp;
        file.close();
    }
}

void NetworkEngine::loadQueueFromFile() {
    QFile file(getConfigPath("pending_messages.conf"));
    if (file.open(QIODevice::ReadOnly)) {
        QDataStream in(&file); in.setVersion(QDataStream::Qt_6_5); int size = 0; in >> size; m_offlineQueue.clear();
        for (int i = 0; i < size; ++i) { OfflineMessage msg; in >> msg.from >> msg.to >> msg.text >> msg.timestamp; m_offlineQueue.append(msg); }
        file.close(); cleanOldMessages();
    }
}

void NetworkEngine::readPendingDatagrams() {
    while (m_socket->hasPendingDatagrams()) {
        QByteArray datagram; datagram.resize(m_socket->pendingDatagramSize());
        m_socket->readDatagram(datagram.data(), datagram.size());
        if (datagram.isEmpty()) continue;

        // ВЫСОКОСКОРОСТНОЙ АНАЛИЗ СЫРЫХ БАЙТ (Индексы на месте, защищены от сдвигов Qt)
        if (datagram.startsWith("AUDIO:")) {
            if (m_callStatus == "CONNECTED") {
                int firstColon = 6;
                int secondColon = datagram.indexOf(':', firstColon);
                int thirdColon = datagram.indexOf(':', secondColon + 1);
                if (secondColon > 0 && thirdColon > 0) {
                    QString from = QString::fromUtf8(datagram.mid(firstColon, secondColon - firstColon));
                    if (m_activeChatPeer == from) {
                        QByteArray audioBytes = datagram.mid(thirdColon + 1);
                        if (m_audioEngine) m_audioEngine->playAudioBlock(audioBytes);
                    }
                }
            }
            continue;
        }

        // РАЗБОР СИГНАЛЬНЫХ СТРОК АТС
        QString rawStr = QString::fromUtf8(datagram);
        QStringList parts = rawStr.split(":");
        if (parts.size() < 3) continue;

        QString type = parts[0];
        QString from = parts[1];
        QString to = parts[2];
        if (from == m_myName) continue;

        if (type == "CALL_REQ") {
            if (to != m_myName) continue;
            if (m_callStatus == "IDLE") {
                m_activeChatPeer = from; m_callStatus = "INCOMING";
                emit activeChatPeerChanged(); emit callStatusChanged();
            } else {
                QByteArray dReject = "CALL_REJECT:" + m_myName.toUtf8() + ":" + from.toUtf8();
                m_socket->writeDatagram(dReject, m_subnetBroadcast, m_port);
            }
        }
        else if (type == "CALL_ACCEPT") {
            if (to != m_myName) continue;
            if (m_callStatus == "OUTGOING" && m_activeChatPeer == from) {
                m_callStatus = "CONNECTED"; emit callStatusChanged(); emit callStarted();
            }
        }
        else if (type == "CALL_REJECT") {
            if (to != m_myName) continue;
            if (m_activeChatPeer == from) {
                if (m_audioEngine) m_audioEngine->stop();
                m_callStatus = "IDLE"; m_activeChatPeer = "";
                emit callStatusChanged(); emit activeChatPeerChanged(); emit callEnded();
            }
        }
        else if (type == "TEXT_MSG" && parts.size() >= 4) {
            if (to != m_myName) continue;
            QString text = parts[3];
            QString formattedLine = QString("[%1]: %2\n").arg(from, text);
            QFile file(getConfigPath("chat_" + from + ".log"));
            if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) { QTextStream out(&file); out << formattedLine; file.close(); }

            if (m_activeChatPeer != from) {
                m_activeChatPeer = from; emit activeChatPeerChanged();
                m_chatLog = formattedLine; emit chatLogChanged();
            } else {
                m_chatLog += formattedLine; emit chatLogChanged();
            }
        }
        else if (type == "PING_REQ") {
            if (to != m_myName) continue;
            QByteArray dPong = "PING_PONG:" + m_myName.toUtf8() + ":" + from.toUtf8();
            m_socket->writeDatagram(dPong, m_subnetBroadcast, m_port);
        }
        else if (type == "PING_PONG") {
            if (to != m_myName) continue;
            for (const auto &msg : m_offlineQueue) {
                if (msg.to == from) {
                    QByteArray dMsg = "TEXT_MSG:" + m_myName.toUtf8() + ":" + from.toUtf8() + ":" + msg.text.toUtf8();
                    m_socket->writeDatagram(dMsg, m_subnetBroadcast, m_port);
                }
            }
        }
    }
}
