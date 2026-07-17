#ifndef NETWORKENGINE_H
#define NETWORKENGINE_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QDateTime>
#include <QHostAddress>
#include <QList>
#include <QHash>
#include <QSettings>
#include "audioengine.h"

struct OfflineMessage {
    QString sender;
    QString text;
    QDateTime timestamp;
};

class NetworkEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList peerList READ peerList NOTIFY peerListChanged)

public:
    explicit NetworkEngine(QObject *parent = nullptr);
    ~NetworkEngine();

    // Методы управления, доступные из QML
    Q_INVOKABLE void start(const QString &username);
    Q_INVOKABLE void sendMessage(const QString &text);
    // Добавьте эту строчку:
    Q_INVOKABLE void sendMessage(const QString &targetPeer, const QString &text);
    Q_INVOKABLE void sendFile(const QString &filePath);
    Q_INVOKABLE void startAudioCall(const QString &targetPeerName);
    Q_INVOKABLE void stopAudioCall();

    // Новые методы для сохранения авторизации
    Q_INVOKABLE bool isRegistered() const;
    Q_INVOKABLE QString getSavedName() const;
    Q_INVOKABLE void saveNameToFile(const QString &username);
    Q_INVOKABLE void resetRegistration();
    Q_INVOKABLE void startChatSession(const QString &targetPeer);

    QStringList peerList() const;
    // В секцию public:
    Q_PROPERTY(int micLevel READ micLevel NOTIFY micLevelChanged)
    Q_PROPERTY(int netLevel READ netLevel NOTIFY netLevelChanged)

    int micLevel() const { return m_micLevel; }
    int netLevel() const { return m_netLevel; }
signals:
    void peerListChanged();
    void messageReceived(const QString &senderName, const QString &text);
    void fileReceived(const QString &senderName, const QString &fileName, const QByteArray &fileData);
    void incomingCall(const QString &peerName);
    void callAccepted();
    void callRejected();
    void callEnded();
    void requestOpenChat(const QString &peerName);
    void micLevelChanged();
    void netLevelChanged();

private slots:
    void readPendingDatagrams();
    void sendHeartbeat();
    void checkDeadPeers();
    void handleAudioFrameReady(const QByteArray &frame);

private:
    QUdpSocket *m_udpSocket = nullptr;
    QUdpSocket *m_audioSocket = nullptr;
    const QHostAddress m_multicastAddress;
    const quint16 m_port = 45454;
    const quint16 m_audioPort = 45455;

    QString m_username;
    QTimer *m_heartbeatTimer = nullptr;
    QTimer *m_expiryTimer = nullptr;

    struct PeerInfo {
        QHostAddress address;
        QDateTime lastSeen;
    };
    QHash<QString, PeerInfo> m_discoveredPeers;

    AudioEngine *m_audioEngine = nullptr;
    QString m_currentCallPeer;
    bool m_inCall = false;

    void broadcastDatagram(const QJsonObject &json);
    void processJsonMessage(const QJsonObject &json, const QHostAddress &senderAddress);
    void configureNetworkInterfaces();

    // СПИСОК ДЛЯ ХРАНЕНИЯ НАЙДЕННЫХ БРОДКАСТ-АДРЕСОВ WI-FI
    QList<QHostAddress> m_broadcastAddresses;
    const quint16 m_tcpPort = 45456; // Новый выделенный порт для TCP-чата
    QTcpServer *m_tcpServer = nullptr;

    void initTcpServer();
    QTcpSocket *m_ivanSocket = nullptr;
    QTcpSocket *m_activeTunnel = nullptr;
    int m_micLevel = 0;
    int m_netLevel = 0;
private slots:
    void handleNewTcpConnection();
    void handleTcpReadyRead();

};

#endif // NETWORKENGINE_H
