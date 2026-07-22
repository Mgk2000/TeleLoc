#include "networkengine.h"
#include <QNetworkInterface>

NetworkEngine::NetworkEngine(QObject *parent)
    : QObject(parent)
{
    m_udpSocket = new QUdpSocket(this);

    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &NetworkEngine::sendHeartbeat);

    // Подключаем чтение входящих сетевых пакетов
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &NetworkEngine::readPendingDatagrams);

    // ИСПРАВЛЕНО: Оригинальное имя сигнала - audioDataReady
    connect(&m_audioEngine, &AudioEngine::audioDataReady, this, &NetworkEngine::handleAudioInputReady);

    // Настраиваем рингтон для входящих вызовов
    m_ringtonePlayer = new QSoundEffect(this);
    m_ringtonePlayer->setSource(QUrl(QStringLiteral("qrc:/TeleLoc/ringtone.wav")));
    m_ringtonePlayer->setLoopCount(QSoundEffect::Infinite);
    m_ringtonePlayer->setVolume(0.7f);
}

NetworkEngine::~NetworkEngine()
{
    stopAudioCall();
}

void NetworkEngine::start(const QString &name)
{
    m_username = name;
    emit usernameChanged();

    // Биндим сокет на порт с возможностью совместного использования адреса в локальной сети
    m_udpSocket->bind(QHostAddress::AnyIPv4, m_port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    m_heartbeatTimer->start(3000);
    sendHeartbeat();
}

void NetworkEngine::saveNameToFile(const QString &name)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "TeleLoc", "config");
    settings.setValue("user/username", name);
}

QString NetworkEngine::getSavedName() const
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "TeleLoc", "config");
    return settings.value("user/username", "").toString();
}

bool NetworkEngine::isRegistered() const
{
    return !getSavedName().isEmpty();
}

void NetworkEngine::setUsername(const QString &name)
{
    if (m_username != name && !name.isEmpty()) {
        m_username = name;
        saveNameToFile(name);
        emit usernameChanged();
        sendHeartbeat();
    }
}

void NetworkEngine::startChatSession(const QString &targetPeer)
{
    m_currentActiveChatPeer = targetPeer;
}

void NetworkEngine::sendChatMessage(const QString &text)
{
    if (m_currentActiveChatPeer.isEmpty()) return;

    QJsonObject json;
    json["type"] = "chat";
    json["sender"] = m_username;
    json["target"] = m_currentActiveChatPeer;
    json["text"] = text;

    sendJsonMessage(json);
    emit messageReceived(m_username, text);
}

void NetworkEngine::startAudioCall(const QString &targetPeer)
{
    if (targetPeer.isEmpty()) return;
    m_currentActiveCallPeer = targetPeer;

    QJsonObject json;
    json["type"] = "call_start";
    json["sender"] = m_username;
    json["target"] = targetPeer;

    sendJsonMessage(json);
}

void NetworkEngine::acceptAudioCall()
{
    if (m_currentActiveCallPeer.isEmpty()) return;

    // Выключаем рингтон, если он играл
    if (m_ringtonePlayer->isPlaying()) {
        m_ringtonePlayer->stop();
    }

    QJsonObject json;
    json["type"] = "call_accept";
    json["sender"] = m_username;
    json["target"] = m_currentActiveCallPeer;

    sendJsonMessage(json);

    m_isAudioCallActive = true;
    // ИСПРАВЛЕНО: Оригинальное название метода - startCapture
    m_audioEngine.startCapture();
    emit callAccepted();
}

void NetworkEngine::stopAudioCall()
{
    // Гарантированно выключаем звук вызова
    if (m_ringtonePlayer->isPlaying()) {
        m_ringtonePlayer->stop();
    }

    if (m_currentActiveCallPeer.isEmpty()) return;

    QJsonObject json;
    json["type"] = "call_end";
    json["sender"] = m_username;
    json["target"] = m_currentActiveCallPeer;

    sendJsonMessage(json);

    // ИСПРАВЛЕНО: Оригинальное название метода - stopCapture
    m_audioEngine.stopCapture();
    m_isAudioCallActive = false;
    m_currentActiveCallPeer = "";
    m_micRms = 0.0;
    emit micRmsChanged();
    emit callEnded();
}

void NetworkEngine::sendHeartbeat()
{
    QJsonObject json;
    json["type"] = "heartbeat";
    json["sender"] = m_username;
    sendJsonMessage(json);
}

void NetworkEngine::handleAudioInputReady(const QByteArray &data)
{
    if (!m_isAudioCallActive || m_currentActiveCallPeer.isEmpty()) return;

    // Считаем уровень громкости (RMS) микрофона для анимации в UI
    if (data.size() >= 2) {
        const qint16 *samples = reinterpret_cast<const qint16*>(data.constData());
        int count = data.size() / 2;
        double sum = 0;
        for (int i = 0; i < count; ++i) {
            double s = samples[i] / 32768.0;
            sum += s * s;
        }
        m_micRms = qSqrt(sum / count);
        emit micRmsChanged();
    }

    QJsonObject json;
    json["type"] = "audio_payload";
    json["sender"] = m_username;
    json["target"] = m_currentActiveCallPeer;
    json["raw_base64"] = QString::fromLatin1(data.toBase64());

    sendJsonMessage(json);
}

void NetworkEngine::readPendingDatagrams()
{
    while (m_udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_udpSocket->pendingDatagramSize()));
        m_udpSocket->readDatagram(datagram.data(), datagram.size());

        QJsonDocument doc = QJsonDocument::fromJson(datagram);
        if (!doc.isNull() && doc.isObject()) {
            processJsonMessage(doc.object());
        }
    }
}

void NetworkEngine::processJsonMessage(const QJsonObject &json)
{
    QString type = json["type"].toString();
    QString senderName = json["sender"].toString();
    QString target = json["target"].toString();

    if (senderName == m_username) return;

    if (type == "chat") {
        if (target == m_username) {
            emit messageReceived(senderName, json["text"].toString());
        }
    }
    else if (type == "call_start") {
        if (target == m_username) {
            // Если мы уже заняты другим звонком
            if (!m_currentActiveCallPeer.isEmpty() && m_currentActiveCallPeer != senderName) {
                QJsonObject busyJson;
                busyJson["type"] = "call_busy";
                busyJson["sender"] = m_username;
                busyJson["target"] = senderName;
                sendJsonMessage(busyJson);
                return;
            }
            m_currentActiveCallPeer = senderName;

            // Включаем рингтон
            m_ringtonePlayer->play();
            emit incomingCall(senderName);
        }
    }
    else if (type == "call_accept") {
        if (target == m_username && m_currentActiveCallPeer == senderName) {
            if (m_ringtonePlayer->isPlaying()) {
                m_ringtonePlayer->stop();
            }
            m_isAudioCallActive = true;
            // ИСПРАВЛЕНО: startCapture
            m_audioEngine.startCapture();
            emit callAccepted();
        }
    }
    else if (type == "call_end") {
        if (m_currentActiveCallPeer == senderName) {
            if (m_ringtonePlayer->isPlaying()) {
                m_ringtonePlayer->stop();
            }
            // ИСПРАВЛЕНО: stopCapture
            m_audioEngine.stopCapture();
            m_isAudioCallActive = false;
            m_currentActiveCallPeer = "";
            m_micRms = 0.0;
            emit micRmsChanged();
            emit callEnded();
        }
    }
    else if (type == "call_busy") {
        if (target == m_username && m_currentActiveCallPeer == senderName) {
            if (m_ringtonePlayer->isPlaying()) {
                m_ringtonePlayer->stop();
            }
            m_currentActiveCallPeer = "";
            emit callEnded();
        }
    }
    else if (type == "audio_payload") {
        if (target == m_username && m_isAudioCallActive && m_currentActiveCallPeer == senderName) {
            QByteArray audioData = QByteArray::fromBase64(json["raw_base64"].toString().toLatin1());
            // ИСПРАВЛЕНО: Оригинальное название метода - writeAudioData
            m_audioEngine.writeAudioData(audioData);
        }
    }
}

void NetworkEngine::sendJsonMessage(const QJsonObject &json)
{
    QJsonDocument doc(json);
    QByteArray datagram = doc.toJson(QJsonDocument::Compact);
    m_udpSocket->writeDatagram(datagram, QHostAddress::Broadcast, m_port);
}
