#include "networkengine.h"
#include <QDataStream>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

NetworkEngine::NetworkEngine(QObject *parent)
    : QObject(parent), m_port(45454), m_isRegistered(false)
{
    m_socket = new QUdpSocket(this);
    m_socket->bind(QHostAddress::AnyIPv4, m_port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    connect(m_socket, &QUdpSocket::readyRead, this, &NetworkEngine::readPendingDatagrams);

    // ПРИ СТАРТЕ: Проверяем, есть ли уже готовый файл teleloc.conf на диске
    loadNameFromFile();
}

QString NetworkEngine::getConfigPath() const {
    // Каноничный неизменяемый путь к папке данных приложения на Android
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    return path + "/teleloc.conf";
}

void NetworkEngine::loadNameFromFile() {
    QFile file(getConfigPath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString savedName = in.readAll().trimmed();
        file.close();

        if (!savedName.isEmpty()) {
            m_myName = savedName;
            m_isRegistered = true;
            emit myNameChanged();
            emit isRegisteredChanged();
            qDebug() << "===> [КОНФИГ] Найдена постоянная запись! Добро пожаловать заново," << m_myName;
        }
    }
}

void NetworkEngine::saveNameToFile(const QString &name) {
    QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) return;

    QFile file(getConfigPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << trimmedName;
        file.close();

        m_myName = trimmedName;
        m_isRegistered = true;
        emit myNameChanged();
        emit isRegisteredChanged();
        qDebug() << "===> [КОНФИГ] Имя" << m_myName << "намертво записано в файл:" << getConfigPath();
    }
}

void NetworkEngine::resetRegistration() {
    QFile file(getConfigPath());
    file.remove(); // Стираем файл физически

    m_myName = "";
    m_isRegistered = false;
    emit myNameChanged();
    emit isRegisteredChanged();
    qDebug() << "===> [КОНФИГ] Файл конфигурации удален. Регистрация сброшена.";
}

void NetworkEngine::setMyName(const QString &name) {
    if (m_myName != name) {
        m_myName = name;
        emit myNameChanged();
    }
}

void NetworkEngine::startCall(const QString &targetName) {
    if (m_myName.isEmpty() || targetName.isEmpty()) return;

    QByteArray datagram;
    QDataStream out(&datagram, QIODevice::WriteOnly);

    // Пакет: ТИП | ОТ КОГО | КОМУ
    out << QString("CALL_REQ") << m_myName << targetName;

    m_socket->writeDatagram(datagram, QHostAddress::Broadcast, m_port);
    qDebug() << "===> [СЕТЬ] УСПЕХ! Звонок улетел в Wi-Fi эфир! От:" << m_myName << "Кому:" << targetName;
}

void NetworkEngine::readPendingDatagrams() {
    while (m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_socket->pendingDatagramSize());
        m_socket->readDatagram(datagram.data(), datagram.size());

        QDataStream in(&datagram, QIODevice::ReadOnly);
        QString type, from, to;
        in >> type >> from >> to;

        if (from == m_myName) continue;

        qDebug() << "<=== [СЕТЬ] Поймали входящий пакет! Тип:" << type << "От:" << from << "Кому:" << to;

        // РАСПРЕДЕЛЕННАЯ СЕЛЕКТИВНАЯ ФИЛЬТРАЦИЯ:
        // Если Иван звонит Петру, то ВСЕ гаджеты, где в файле написано "Петр", поймают этот пакет и отреагируют!
        if (to != m_myName) {
            continue;
        }

        if (type == "CALL_REQ") {
            qDebug() << "     [УСПЕХ] Внимание! На этот наш гаджет проходит вызов от:" << from;
        }
    }
}
