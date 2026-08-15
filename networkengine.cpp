#include "networkengine.h"
#include <QNetworkInterface>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QThread>

NetworkEngine::NetworkEngine(QObject *parent)
    : QObject(parent), m_isCallActive(false)
{
    udpSocket = new QUdpSocket(this);
    tcpSocket = new QTcpSocket(this);
    tcpClientSocket = nullptr; // Для входящих TCP-соединений
    tcpServer = new QTcpServer(this);
    udpSocket->bind(QHostAddress::AnyIPv4, 28000, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    // Подключаем ваши оригинальные слоты чтения данных
    connect(tcpServer, &QTcpServer::newConnection, this, &NetworkEngine::onNewConnection);
    connect(udpSocket, &QUdpSocket::readyRead, this, &NetworkEngine::onReadyUdpRead);

    // Ваши оригинальные звуковые эффекты
    m_incomingRing = new QSoundEffect(this);
    m_incomingRing->setSource(QUrl::fromLocalFile(":/audio/ring.wav"));
    m_incomingRing->setLoopCount(QSoundEffect::Infinite);

    m_ringbackTone = new QSoundEffect(this);
    m_ringbackTone->setSource(QUrl::fromLocalFile(":/audio/ringback.wav"));
    m_ringbackTone->setLoopCount(QSoundEffect::Infinite);

    m_busyTone = new QSoundEffect(this);
    m_busyTone->setSource(QUrl::fromLocalFile(":/audio/busy.wav"));

    audioEngine = new AudioEngine(this);

    // Читаем конфигурацию при старте
    readConfig();

    // Запускаем таймер обновления интерфейсов
    QTimer *interfaceTimer = new QTimer(this);
    connect(interfaceTimer, &QTimer::timeout, this, &NetworkEngine::updateInterfaces);
    interfaceTimer->start(5000);
    updateInterfaces();

    // Таймер для рассылки Discovery
    QTimer *discTimer = new QTimer(this);
    connect(discTimer, &QTimer::timeout, this, &NetworkEngine::sendDiscovery);
    discTimer->start(3000);
}

NetworkEngine::~NetworkEngine()
{
    delete audioEngine;
}

void NetworkEngine::handleVoipWakeup(const QString &callerName) {
    emit messageReceived("СИСТЕМА", "Приложение разбужено Java-интентом! Вызов от: " + callerName);
    m_incomingRing->play();
    emit incomingCall(callerName, 0);
}

// === ТОЧНЫЕ РЕАЛИЗАЦИИ ВАШИХ ОТЛАДОЧНЫХ МЕТОДОВ ДЛЯ ТЕСТА ЗВУКА ===
void NetworkEngine::startDebugRecord() {
    if (audioEngine) {
        audioEngine->startRecording(); // Начинает писать входящие PCM-данные в test.wav
    }
}

void NetworkEngine::stopDebugRecord() {
    if (audioEngine) {
        audioEngine->stopRecording(); // Запечатывает WAV-заголовок файла на диске
    }
}

void NetworkEngine::playDebugRecord() {
    if (audioEngine) {
        audioEngine->playRecordedFile(); // Воспроизводит сохранённый test.wav в динамик
    }
}
bool NetworkEngine::isNetTypeAvailable(int netType) {
    if (m_users.isEmpty()) return false;
    UserInfo me = m_users.at(0);
    if (netType == 0) return !me.ip0.isEmpty();
    if (netType == 1) return !me.ip1.isEmpty();
    if (netType == 2) return !me.ip2.isEmpty();
    return false;
}

QStringList NetworkEngine::getUsers(int netType) {
    QStringList list;
    // Начиная со 2-го элемента (i=1), так как первый — это мы сами
    for (int i = 1; i < m_users.size(); ++i) {
        UserInfo u = m_users.at(i);
        QString ip = "";
        if (netType == 0) ip = u.ip0;
        else if (netType == 1) ip = u.ip1;
        else if (netType == 2) ip = u.ip2;

        if (!ip.isEmpty()) {
            list.append(u.name + " (" + ip + ")");
        }
    }
    return list;
}

void NetworkEngine::debugUsers() {
    qDebug() << "=== СПИСОК ПИРОВ В ПАМЯТИ ===";
    for (const UserInfo &u : m_users) {
        qDebug() << "Имя:" << u.name << "| IP0:" << u.ip0 << "| IP1:" << u.ip1 << "| IP2:" << u.ip2;
    }
}

void NetworkEngine::parseIncomingSyncData(const QByteArray &data, const QString &senderIpStr) {
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) return;

    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();

    if (type == "discovery") {
        QString name = obj["name"].toString();
        QString ip0 = obj["ip0"].toString();
        QString ip1 = obj["ip1"].toString();
        QString ip2 = obj["ip2"].toString();
        //qDebug() << "%%%%%%%%%%%%%%%%%%%%%%" << name;
        bool found = false;
        for (int i = 0; i < m_users.size(); ++i) {
            if (m_users.at(i).name == name) {
                UserInfo u = m_users.at(i);
                u.ip0 = ip0; u.ip1 = ip1; u.ip2 = ip2; u.isAlive = true;
                m_users.replace(i, u);
                found = true;
                break;
            }
        }
        if (!found) {
            UserInfo u;
            u.name = name; u.ip0 = ip0; u.ip1 = ip1; u.ip2 = ip2; u.isAlive = true;
            m_users.append(u);
            savePeersToConfig();
        }
        emit peerListChanged(); // Ваш оригинальный сигнал изменения списка пиров
    }
    else if (type == "incoming_call") {
        QString callerName = obj["name"].toString();
        int netType = obj["net_type"].toInt();
        m_activePeerIp = senderIpStr;
        m_activeCallNetType = netType;
        m_incomingRing->play();
        emit incomingCall(callerName, netType);
    }
    else if (type == "accept_call") {
        m_ringbackTone->stop();
        m_isCallActive = true;
        // Здесь ваша рация начинает захват звука, test.wav включится по кнопке отладки
        emit callAccepted(); // Ваш оригинальный сигнал
    }
    else if (type == "reject_call") {
        m_ringbackTone->stop();
        m_busyTone->play();
        emit callStopped(); // Ваш оригинальный сигнал сброса
    }
    else if (type == "end_call") {
        m_incomingRing->stop();
        m_ringbackTone->stop();
        m_isCallActive = false;
        emit callStopped(); // Ваш оригинальный сигнал сброса
    }
}
void NetworkEngine::onNewConnection() {
    QTcpSocket *clientSocket = tcpServer->nextPendingConnection();
    if (!clientSocket) return;

    connect(clientSocket, &QTcpSocket::readyRead, this, [this, clientSocket]() {
        QString senderIp = clientSocket->peerAddress().toString();
        if (senderIp.startsWith("::ffff:")) {
            senderIp.remove("::ffff:");
        }

        QByteArray requestData = clientSocket->readAll();
        QString s(requestData);
       // qDebug() << "TCP=%%%%%%%%%%%%%%%%%%%%%%%%%%%" << s;
        /parseIncomingSyncData(requestData, senderIp);
        clientSocket->disconnectFromHost();
    });

    connect(clientSocket, &QTcpSocket::disconnected, clientSocket, &QTcpSocket::deleteLater);
}

void NetworkEngine::onReadyTcpRead() {
    // Используется для кастомной обработки входящего TCP-клиента при необходимости
}

void NetworkEngine::onReadyUdpRead() {
    //qDebug() << "%%%%%%%%%%%%%%%%%%%%%%%%%OnReadyUdp";
    while (udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(udpSocket->pendingDatagramSize());
        QHostAddress senderHost;
        udpSocket->readDatagram(datagram.data(), datagram.size(), &senderHost);
        QString s = datagram;
        //qDebug() << "UDP=$$$$$$$$$$$$$$$$$$$" << s;
        QString senderIp = senderHost.toString();
        if (senderIp.startsWith("::ffff:")) {
            senderIp.remove("::ffff:");
        }

        if (m_users.isEmpty() || senderIp == m_users.at(0).ip0 ||
            senderIp == m_users.at(0).ip1 || senderIp == m_users.at(0).ip2) {
            continue;
        }

        parseIncomingSyncData(datagram, senderIp);
    }
}

void NetworkEngine::callSpecificIp(const QString &targetIp, int netType) {
    if (m_users.isEmpty() || targetIp.isEmpty()) return;
    UserInfo me = m_users.at(0);

    QString cleanIp = targetIp.trimmed();
    if (cleanIp.startsWith("::ffff:")) { cleanIp.remove("::ffff:"); }

    m_activePeerIp = cleanIp;
    m_activeCallNetType = netType;

    // СЕТЕВОЙ БУДИЛЬНИК: Пинаем UDP-бродкастом закрытый Android соседа
    QUdpSocket wakeSocket;
    QJsonObject wakeObj;
    wakeObj["type"] = "wake_up_impulse";
    wakeObj["action"] = "org.qtproject.example.appteleloc.WAKE_UP_ACTION";
    QByteArray wakeData = QJsonDocument(wakeObj).toJson(QJsonDocument::Compact);

    emit messageReceived("СИСТЕМА", "Отправка импульса пробуждения для " + cleanIp);
    wakeSocket.writeDatagram(wakeData, QHostAddress("255.255.255.255"), PORT);
    wakeSocket.writeDatagram(wakeData, QHostAddress("192.168.43.255"), PORT);
    wakeSocket.writeDatagram(wakeData, QHostAddress("192.168.137.255"), PORT);
    wakeSocket.writeDatagram(wakeData, QHostAddress(cleanIp), PORT);

    QThread::msleep(350); // Даем 10-секундному стражу соседа очнуться и открыть TCP порт

    QJsonObject obj;
    obj["type"] = "incoming_call";
    obj["name"] = me.name;
    obj["net_type"] = netType;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";

    emit messageReceived("СИСТЕМА", "Вызов по IP: " + cleanIp + " на порт 28500");

    tcpSocket->abort();
    tcpSocket->connectToHost(cleanIp, 28500);

    if (tcpSocket->waitForConnected(1200)) {
        tcpSocket->write(data);
        tcpSocket->waitForBytesWritten(500);
        tcpSocket->disconnectFromHost();
    } else {
        // Резервный откат на порт 28000, если 28500 занят/не успел открыться
        tcpSocket->abort();
        tcpSocket->connectToHost(cleanIp, PORT);
        if (tcpSocket->waitForConnected(1000)) {
            tcpSocket->write(data);
            tcpSocket->waitForBytesWritten(500);
            tcpSocket->disconnectFromHost();
        }
    }
    m_ringbackTone->play();
}

void NetworkEngine::startAudioCall(const QString &targetPeerName, int netType) {
    // Находим IP по имени пира и вызываем callSpecificIp
    for (const UserInfo &u : m_users) {
        if (u.name == targetPeerName) {
            QString ip = (netType == 0) ? u.ip0 : ((netType == 1) ? u.ip1 : u.ip2);
            if (!ip.isEmpty()) {
                callSpecificIp(ip, netType);
                break;
            }
        }
    }
}

void NetworkEngine::acceptAudioCall(const QString &targetPeerName, int netType) {
    Q_UNUSED(targetPeerName);
    Q_UNUSED(netType);
    if (m_activePeerIp.isEmpty()) return;
    UserInfo me = m_users.at(0);

    QJsonObject obj;
    obj["type"] = "accept_call";
    obj["name"] = me.name;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";

    sendTcpPacket(m_activePeerIp, data);
    m_incomingRing->stop();
    m_isCallActive = true;
    emit callAccepted();
}

void NetworkEngine::stopAudioCall() {
    if (m_activePeerIp.isEmpty()) return;
    QJsonObject obj;
    obj["type"] = m_isCallActive ? "end_call" : "reject_call";
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";

    sendTcpPacket(m_activePeerIp, data);
    m_incomingRing->stop();
    m_ringbackTone->stop();
    m_isCallActive = false;
    emit callStopped();
}

void NetworkEngine::sendMessage(const QString &targetPeer, const QString &text) {
    Q_UNUSED(targetPeer);
    Q_UNUSED(text);
}

QString NetworkEngine::getSavedName() {
    if (m_users.isEmpty()) return "Дачник";
    return m_users.at(0).name;
}

void NetworkEngine::saveNameToFile(const QString &name) {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);
    me.name = name;
    m_users.replace(0, me);
    savePeersToConfig();
}

void NetworkEngine::sendTcpPacket(const QString &ip, const QByteArray &data) {
    QTcpSocket socket;
    socket.connectToHost(ip, PORT);
    if (socket.waitForConnected(1000)) {
        socket.write(data);
        socket.waitForBytesWritten(500);
        socket.disconnectFromHost();
    }
}

void NetworkEngine::sendDiscovery() {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);

    QJsonObject obj;
    obj["type"] = "discovery";
    obj["name"] = me.name;
    obj["ip0"] = me.ip0;
    obj["ip1"] = me.ip1;
    obj["ip2"] = me.ip2;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    //qDebug() << "%%%%%%%%%%%%%%%%SendDiscovery UDP to 255.255.255.255";
    udpSocket->writeDatagram(data, QHostAddress::Broadcast, PORT);
}

void NetworkEngine::updateInterfaces() {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);
    me.ip0 = ""; me.ip1 = ""; me.ip2 = "";

    int count = 0;
    const QList<QHostAddress> list = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : list) {
        if (address.isLoopback() || address.protocol() != QAbstractSocket::IPv4Protocol) {
            continue;
        }
        QString ip = address.toString();
        if (count == 0) me.ip0 = ip;
        else if (count == 1) me.ip1 = ip;
        else if (count == 2) me.ip2 = ip;
        count++;
    }
    m_users.replace(0, me);
}

void NetworkEngine::readConfig() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/teleloc.conf";
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        UserInfo me; me.name = "Дачник"; me.isAlive = true;
        m_users.append(me);
        return;
    }
    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();

    UserInfo me;
    me.name = obj["my_name"].toString(); me.isAlive = true;
    m_users.append(me);

    QJsonArray arr = obj["peers"].toArray();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject p = arr.at(i).toObject();
        UserInfo u;
        u.name = p["name"].toString();
        u.ip0 = p["ip0"].toString();
        u.ip1 = p["ip1"].toString();
        u.ip2 = p["ip2"].toString();
        u.isAlive = false;
        m_users.append(u);
    }
}

void NetworkEngine::savePeersToConfig() {
    if (m_users.isEmpty()) return;
    QJsonObject root;
    root["my_name"] = m_users.at(0).name;

    QJsonArray arr;
    for (int i = 1; i < m_users.size(); ++i) {
        QJsonObject p;
        p["name"] = m_users.at(i).name;
        p["ip0"] = m_users.at(i).ip0;
        p["ip1"] = m_users.at(i).ip1;
        p["ip2"] = m_users.at(i).ip2;
        arr.append(p);
    }
    root["peers"] = arr;

    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/teleloc.conf";
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}
