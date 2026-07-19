#include "networkengine.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QDebug>

// 1. Изменяем конструктор (убираем мультикаст-адрес)
NetworkEngine::NetworkEngine(QObject *parent) : QObject(parent)
{
    m_udpSocket = new QUdpSocket(this);
    m_audioSocket = new QUdpSocket(this);
    m_udpSocket->bind(QHostAddress::AnyIPv4, m_port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    m_audioSocket->bind(QHostAddress::AnyIPv4, m_audioPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &NetworkEngine::readPendingDatagrams);
    connect(m_audioSocket, &QUdpSocket::readyRead, this, [this]() {
        while (m_audioSocket->hasPendingDatagrams()) {
            QByteArray datagram;
            datagram.resize(m_audioSocket->pendingDatagramSize());
            QHostAddress senderIp;
            m_audioSocket->readDatagram(datagram.data(), datagram.size(), &senderIp);

            bool isParticipant = false;
            auto it = m_discoveredPeers.begin();
            while (it != m_discoveredPeers.end()) {
                if (it.value().address.toIPv4Address() == senderIp.toIPv4Address() && m_activeCallPeers.contains(it.key())) {
                    isParticipant = true;
                    break;
                }
                ++it;
            }

            if (m_inCall && isParticipant && m_audioEngine) {
                long long sumNet = 0;
                const qint16 *samplesNet = reinterpret_cast<const qint16*>(datagram.constData());
                int sampleCountNet = datagram.size() / 2;
                for (int i = 0; i < sampleCountNet; ++i) {
                    sumNet += samplesNet[i] * samplesNet[i];
                }
                int rmsNet = (sampleCountNet > 0) ? qSqrt(sumNet / sampleCountNet) : 0;
                int newNetLevel = qMin(100, (rmsNet * 100) / 32767);
                if (m_netLevel != newNetLevel) {
                    m_netLevel = newNetLevel;
                    emit netLevelChanged();
                }
                m_audioEngine->playFrame(datagram);
            }
        }
    });
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &NetworkEngine::sendHeartbeat);
    m_expiryTimer = new QTimer(this);
    connect(m_expiryTimer, &QTimer::timeout, this, &NetworkEngine::checkDeadPeers);
    m_audioEngine = new AudioEngine(this);
    connect(m_audioEngine, &AudioEngine::frameReady, this, &NetworkEngine::handleAudioFrameReady);
    m_filePlayTimer = new QTimer(this);
    connect(m_filePlayTimer, &QTimer::timeout, this, &NetworkEngine::streamAudioFileChunk);

}

void NetworkEngine::startAudioCall(const QString &targetPeerName)
{
    if (targetPeerName.isEmpty()) return;

    if (m_activeCallPeers.contains(targetPeerName)) {
        m_inCall = true;
        if (m_audioEngine) m_audioEngine->startRecording();
        emit callAccepted();

        QJsonObject json;
        json["type"] = "call_accept";
        json["sender"] = m_username;
        json["target_peer"] = targetPeerName;
        broadcastDatagram(json);
        return;
    }

    if (!m_activeCallPeers.contains(targetPeerName)) {
        m_activeCallPeers.append(targetPeerName);
    }

    QJsonObject json;
    json["type"] = "call_start";
    json["sender"] = m_username;
    json["target_peer"] = targetPeerName;
    broadcastDatagram(json);
}
void NetworkEngine::handleAudioFrameReady(const QByteArray &frame)
{
    if (frame.isEmpty() || !m_inCall || m_activeCallPeers.isEmpty()) return;
    long long sum = 0;
    const qint16 *samples = reinterpret_cast<const qint16*>(frame.constData());
    int sampleCount = frame.size() / 2;
    for (int i = 0; i < sampleCount; ++i) {
        sum += samples[i] * samples[i];
    }
    int rms = (sampleCount > 0) ? qSqrt(sum / sampleCount) : 0;
    int newMicLevel = qMin(100, (rms * 100) / 32767);
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
}
void NetworkEngine::configureNetworkInterfaces()
{
    m_broadcastAddresses.clear();
    qDebug() << "=== СКАНИРОВАНИЕ СЕТЕВЫХ ИНТЕРФЕЙСОВ ДЛЯ ОБХОДА VPN ===";

    QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        // Пропускаем выключенные, петлевые (loopback) карты
        if (!iface.isValid() || !(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        QString name = iface.name().toLower();
        QString desc = iface.humanReadableName().toLower();

        // Жестко отсекаем виртуальные адаптеры VPN
        if (name.contains("tun") || name.contains("tap") || name.contains("vpn") || name.contains("ppp") || name.contains("p2p") ||
            desc.contains("tun") || desc.contains("tap") || desc.contains("vpn") || desc.contains("virtual")) {
            qDebug() << "Игнорируем VPN интерфейс:" << iface.humanReadableName();
            continue;
        }

        // Ищем реальный IPv4 бродкаст-адрес подсети (например, 192.168.1.255)
        QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            QHostAddress bcast = entry.broadcast();
            if (!bcast.isNull() && bcast.protocol() == QAbstractSocket::IPv4Protocol) {
                // Сохраняем этот адрес в наш список
                m_broadcastAddresses.append(bcast);
                qDebug() << "Найден физический адрес бродкаста:" << bcast.toString() << "на карте:" << iface.humanReadableName();
            }
        }
    }

    // Если вдруг роутер не отдал бродкаст подсети, добавляем общий резервный бродкаст
    if (m_broadcastAddresses.isEmpty()) {
        m_broadcastAddresses.append(QHostAddress::Broadcast);
        qDebug() << "Физические подсети не найдены. Используем общий бродкаст 255.255.255.255";
    }
}

// 2. ОПТИМИЗИРОВАННЫЙ МЕТОД ОТПРАВКИ СООБЩЕНИЙ
void NetworkEngine::broadcastDatagram(const QJsonObject &json)
{
    QJsonDocument doc(json);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &interface : interfaces) {
        if (interface.flags().testFlag(QNetworkInterface::IsUp) && !interface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            QList<QNetworkAddressEntry> entries = interface.addressEntries();
            for (const QNetworkAddressEntry &entry : entries) {
                QHostAddress broadcastAddress = entry.broadcast();
                if (!broadcastAddress.isNull() && broadcastAddress.protocol() == QAbstractSocket::IPv4Protocol) {
                    m_udpSocket->writeDatagram(data, broadcastAddress, m_port);
                }
            }
        }
    }
}

// 3. ОПТИМИЗИРОВАННЫЙ МЕТОД ОТПРАВКИ ЗВУКА РАЦИИ

NetworkEngine::~NetworkEngine()
{
    stopAudioCall();
}


void NetworkEngine::start(const QString &username)
{
    m_username = username;
    // Запускаем сканирование бродкаст-адресов подсетей
    configureNetworkInterfaces();

    // Запускаем фоновый TCP сервер на том же Wi-Fi адресе
    initTcpServer();

    m_heartbeatTimer->start(2000);
    m_expiryTimer->start(5000);
    sendHeartbeat();
}

void NetworkEngine::sendFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;
    QFileInfo fileInfo(filePath);
    QJsonObject json;
    json["type"] = "file";
    json["sender"] = m_username;
    json["fileName"] = fileInfo.fileName();
    json["fileData"] = QString::fromLatin1(file.readAll().toBase64());
    broadcastDatagram(json);
}

void NetworkEngine::checkDeadPeers()
{
    QDateTime now = QDateTime::currentDateTime();
    bool changed = false;
    auto it = m_discoveredPeers.begin();
    while (it != m_discoveredPeers.end()) {
        if (it.value().lastSeen.secsTo(now) > 6) {
            it = m_discoveredPeers.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) emit peerListChanged();
}

QStringList NetworkEngine::peerList() const
{
    return m_discoveredPeers.keys();
}

bool NetworkEngine::isRegistered() const
{
    QSettings settings("TeleLocCompany", "TeleLocApp");
    return settings.contains("username") && !settings.value("username").toString().isEmpty();
}

QString NetworkEngine::getSavedName() const
{
    QSettings settings("TeleLocCompany", "TeleLocApp");
    return settings.value("username", "").toString();
}

void NetworkEngine::saveNameToFile(const QString &username)
{
    QSettings settings("TeleLocCompany", "TeleLocApp");
    settings.setValue("username", username);
    m_username = username;
}

void NetworkEngine::resetRegistration()
{
    QSettings settings("TeleLocCompany", "TeleLocApp");
    settings.remove("username");
    m_username.clear();
    if (m_heartbeatTimer) m_heartbeatTimer->stop();
    if (m_expiryTimer) m_expiryTimer->stop();
    m_discoveredPeers.clear();
    emit peerListChanged();
}

void NetworkEngine::readPendingDatagrams()
{
    while (m_udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_udpSocket->pendingDatagramSize());
        //qDebug() << "dtagram="  << datagram;
        QHostAddress senderAddress;
        quint16 senderPort;

        m_udpSocket->readDatagram(datagram.data(), datagram.size(), &senderAddress, &senderPort);

        QJsonDocument doc = QJsonDocument::fromJson(datagram);
        if (!doc.isNull() && doc.isObject()) {
            processJsonMessage(doc.object(), senderAddress);
        }
    }
}

void NetworkEngine::processJsonMessage(const QJsonObject &json, const QHostAddress &senderAddress)
{
    QString type = json["type"].toString();
    QString senderName = json["sender"].toString();
    if (senderName == m_username || senderName.isEmpty()) return;
    if (type == "heartbeat") {
        m_discoveredPeers[senderName] = PeerInfo{senderAddress, QDateTime::currentDateTime()};
        emit peerListChanged();
    } else if (type == "request_open_chat") {
        QString target = json["target_peer"].toString();
        if (target == m_username || target == "Все") {
            emit requestOpenChat(senderName);
        }
    } else if (type == "call_start") {
        QString target = json["target_peer"].toString();
        if (target == m_username || target == "Все") {
            if (!m_activeCallPeers.contains(senderName)) {
                m_activeCallPeers.append(senderName);
            }
            emit incomingCall(senderName);
        }
    } else if (type == "call_accept") {
        QString target = json["target_peer"].toString();
        if (target == m_username || target == "Все") {
            m_inCall = true;
            if (!m_activeCallPeers.contains(senderName)) {
                m_activeCallPeers.append(senderName);
            }
            if (m_audioEngine) m_audioEngine->startRecording();
            emit callAccepted();
        }
    } else if (type == "call_end") {
        QString target = json["target_peer"].toString();
        if (target == m_username || target == "Все" || m_activeCallPeers.contains(senderName)) {
            m_activeCallPeers.removeOne(senderName);
            if (m_activeCallPeers.isEmpty()) {
                m_inCall = false;
                if (m_audioEngine) m_audioEngine->stop();
                emit callEnded();
            }
        }
    }
}
void NetworkEngine::stopAudioCall()
{
    if (m_activeCallPeers.isEmpty()) {
        m_inCall = false;
        if (m_audioEngine) m_audioEngine->stop();
        emit callEnded();
        return;
    }

    QString lastPeer = m_activeCallPeers.last();
    m_activeCallPeers.removeOne(lastPeer);

    QJsonObject json;
    json["type"] = "call_end";
    json["sender"] = m_username;
    json["target_peer"] = lastPeer;
    broadcastDatagram(json);

    if (m_activeCallPeers.isEmpty()) {
        m_inCall = false;
        if (m_audioEngine) m_audioEngine->stop();
        emit callEnded();
    }
}
void NetworkEngine::sendMessage(const QString &targetPeer, const QString &text)
{
    if (text.isEmpty()) return;
    QJsonObject json;
    json["type"] = "message";
    json["sender"] = m_username;
    json["text"] = text;
    json["target_peer"] = targetPeer;
    QJsonDocument doc(json);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
#ifdef _WIN32
    if (m_activeTunnel && m_activeTunnel->state() == QAbstractSocket::ConnectedState) {
        m_activeTunnel->write(data);
        m_activeTunnel->flush();
    } else {
        broadcastDatagram(json);
    }
#else
    broadcastDatagram(json);
#endif
    emit messageReceived(m_username, text);
}
// 2. Метод фонового поддержания туннеля (Иван постоянно стучится к Анфисе)
void NetworkEngine::sendHeartbeat()
{
    QJsonObject json;
    json["type"] = "heartbeat";
    json["sender"] = m_username;
    broadcastDatagram(json);

#ifndef _WIN32
    // Автоматически ищем пиров в таблице вместо жесткого IP
    auto it = m_discoveredPeers.begin();
    while (it != m_discoveredPeers.end()) {
        QHostAddress peerIp = it.value().address;

        // Пропускаем петлю обратной связи (себя)
        if (peerIp == QHostAddress::LocalHost || peerIp == QHostAddress::LocalHostIPv6) {
            ++it;
            continue;
        }

        if (!m_activeTunnel) {
            m_activeTunnel = new QTcpSocket(this);
            connect(m_activeTunnel, &QTcpSocket::readyRead, this, &NetworkEngine::handleTcpReadyRead);
            connect(m_activeTunnel, &QTcpSocket::disconnected, m_activeTunnel, &QTcpSocket::deleteLater);
        }

        if (m_activeTunnel->state() == QAbstractSocket::UnconnectedState) {
            m_activeTunnel->connectToHost(peerIp, m_tcpPort);
        }
        break; // Удерживаем одно активное локальное подключение
    }
#endif
}
void NetworkEngine::initTcpServer()
{
    m_tcpServer = new QTcpServer(this);

    // Слушаем абсолютно все интерфейсы без привязки к конкретному Wi-Fi IP
    if (!m_tcpServer->listen(QHostAddress::AnyIPv4, m_tcpPort)) {
        qWarning() << "Не удалось запустить TCP-сервер:" << m_tcpServer->errorString();
    } else {
        qDebug() << "=== TCP СЕРВЕР TeleLoc ЗАПУЩЕН ===";
        qDebug() << "Слушаем адрес: AnyIPv4 порт:" << m_tcpPort;
        connect(m_tcpServer, &QTcpServer::newConnection, this, &NetworkEngine::handleNewTcpConnection);
    }
}

void NetworkEngine::handleNewTcpConnection()
{
    QTcpSocket *clientSocket = m_tcpServer->nextPendingConnection();
    if (clientSocket) {
        m_activeTunnel = clientSocket;
        connect(m_activeTunnel, &QTcpSocket::readyRead, this, &NetworkEngine::handleTcpReadyRead);
        connect(m_activeTunnel, &QTcpSocket::disconnected, m_activeTunnel, &QTcpSocket::deleteLater);
    }
}
void NetworkEngine::handleTcpReadyRead()
{
    if (!m_activeTunnel) return;
    QByteArray data = m_activeTunnel->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isNull() && doc.isObject()) {
        QJsonObject json = doc.object();
        QString type = json["type"].toString();
        QString peerName = json["sender"].toString();
        if (type == "message") {
            emit messageReceived(peerName, json["text"].toString());
        } else if (type == "request_open_chat") {
            emit requestOpenChat(peerName);
        } else if (type == "call_start") {
            if (!m_activeCallPeers.contains(peerName)) {
                m_activeCallPeers.append(peerName);
            }
            emit incomingCall(peerName);
        } else if (type == "call_accept") {
            m_inCall = true;
            if (!m_activeCallPeers.contains(peerName)) {
                m_activeCallPeers.append(peerName);
            }
            if (m_audioEngine) m_audioEngine->startRecording();
            emit callAccepted();
        } else if (type == "call_end") {
            m_activeCallPeers.removeOne(peerName);
            if (m_activeCallPeers.isEmpty()) {
                m_inCall = false;
                if (m_audioEngine) m_audioEngine->stop();
                emit callEnded();
            }
        }
    }
}
void NetworkEngine::startChatSession(const QString &targetPeer)
{
    QJsonObject json;
    json["type"] = "request_open_chat";
    json["sender"] = m_username;
    json["target_peer"] = targetPeer;

    QJsonDocument doc(json);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

// Шлём пакет через туннель или бродкаст
#ifdef _WIN32
    if (m_activeTunnel && m_activeTunnel->state() == QAbstractSocket::ConnectedState) {
        m_activeTunnel->write(data);
        m_activeTunnel->flush();
    } else {
        broadcastDatagram(json);
    }
#else
    broadcastDatagram(json);
#endif
}
QStringList NetworkEngine::getPeerNames() const
{
    return m_discoveredPeers.keys();
}
void NetworkEngine::saveDebugAudioPath(const QString &path)
{
    QSettings settings("TeleLocCompany", "TeleLocApp");
    settings.setValue("debugAudioPath", path);
}

QString NetworkEngine::getSavedDebugAudioPath() const
{
    QSettings settings("TeleLocCompany", "TeleLocApp");
    return settings.value("debugAudioPath", "").toString();
}
void NetworkEngine::setPlayFileMode(bool enabled)
{
    m_playFileMode = enabled;
    if (m_playFileMode) {
        if (m_audioFile.isOpen()) {
            m_audioFile.close();
        }
        m_audioFile.setFileName(getSavedDebugAudioPath());
        if (m_audioFile.open(QIODevice::ReadOnly)) {
            m_audioFile.seek(44);
            m_filePlayTimer->start(20);
        } else {
            m_playFileMode = false;
        }
    } else {
        m_filePlayTimer->stop();
        if (m_audioFile.isOpen()) {
            m_audioFile.close();
        }
    }
}

void NetworkEngine::streamAudioFileChunk()
{
    if (!m_playFileMode) return;

    // Жёстко задаём параметры, которые ожидает AudioEngine
    int sampleRate = 8000;
    int intervalMs = 20;
    int targetSamples = (sampleRate * intervalMs) / 1000; // 160 семплов
    int chunkSize = targetSamples * 2; // 320 байт

    QByteArray frame;
    frame.resize(chunkSize);
    qint16 *targetSamplesPtr = reinterpret_cast<qint16*>(frame.data());

    // Математический синтез чистой синусоиды (нота Ля 440 Гц)
    static double phase = 0.0;
    double frequency = 440.0;
    double twoPi = 2.0 * 3.14159265358979323846;

    for (int i = 0; i < targetSamples; ++i) {
        // Генерируем чистую волну с комфортной громкостью (амплитуда 10000 из 32767)
        targetSamplesPtr[i] = static_cast<qint16>(10000.0 * qSin(phase));

        phase += (twoPi * frequency) / sampleRate;
        if (phase >= twoPi) {
            phase -= twoPi;
        }
    }

    // Обновляем VU-метр микрофона в QML, чтобы полоска ожила
    int newMicLevel = 30;
    if (m_micLevel != newMicLevel) {
        m_micLevel = newMicLevel;
        emit micLevelChanged();
    }

    // Отправляем готовый чистый кадр
    if (m_localLoopbackMode) {
        if (m_audioEngine) {
            m_audioEngine->playFrame(frame);
        }
    } else {
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
    }
}

#include <QDir>
#include <QStandardPaths>

QStringList NetworkEngine::getAvailableWavFiles() const
{
    QStringList filters;
    filters << "*.wav";
    QString path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
#ifdef Q_OS_ANDROID
    QDir dir(path);
    return dir.entryList(filters, QDir::Files);
#else
    return QStringList();
#endif
}
