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
#include <QJsonObject>
#include <QVector>
#include "audioengine.h"

class NetworkEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList peerList READ peerList NOTIFY peerListChanged)
    Q_PROPERTY(int micLevel READ micLevel NOTIFY micLevelChanged)
    Q_PROPERTY(int netLevel READ netLevel NOTIFY netLevelChanged)

public:
    explicit NetworkEngine(QObject *parent = nullptr);
    ~NetworkEngine();

    Q_INVOKABLE void start(const QString &username);
    Q_INVOKABLE void sendMessage(const QString &targetPeer, const QString &text);
    Q_INVOKABLE void startChatSession(const QString &targetPeerName);
    Q_INVOKABLE void startAudioCall(const QString &targetPeerName);
    Q_INVOKABLE void acceptAudioCall(const QString &targetPeerName);
    Q_INVOKABLE void stopAudioCall();
    Q_INVOKABLE void setDebugFrequency(double hz);

    Q_INVOKABLE void saveNameToFile(const QString &name);
    Q_INVOKABLE QString getSavedName() const;
    Q_INVOKABLE bool isRegistered() const;

    QStringList peerList() const;
    int micLevel() const { return m_micLevel; }
    int netLevel() const { return m_netLevel; }

signals:
    void messageReceived(const QString &sender, const QString &text);
    void peerListChanged();
    void incomingCall(const QString &peerName);
    void callAccepted();
    void callEnded();
    void requestOpenChat(const QString &peerName);
    void micLevelChanged();
    void netLevelChanged();

private slots:
    void readPendingDatagrams();
    void sendHeartbeat();
    void handleAudioFrameReady(const QByteArray &frame);

private:
    struct PeerInfo {
        QHostAddress address;
        QDateTime lastSeen;
    };

    void broadcastDatagram(const QJsonObject &json);
    void processJsonMessage(const QJsonObject &json, const QHostAddress &senderAddress);

    QUdpSocket *m_udpSocket = nullptr;
    QUdpSocket *m_sendUdpSocket = nullptr;
    QUdpSocket *m_audioSocket = nullptr;

    int m_port = 45455;
    int m_audioPort = 45456;

    QString m_username;
    bool m_inCall = false;

    int m_micLevel = 0;
    int m_netLevel = 0;

    AudioEngine *m_audioEngine = nullptr;
    QTimer *m_heartbeatTimer = nullptr;

    QHash<QString, PeerInfo> m_discoveredPeers;
    QStringList m_activeCallPeers;
    QList<double> m_processedMessageIds;
    QByteArray m_netAudioBuffer;

    double m_debugFrequency = 0.0;
    double m_debugPhase = 0.0;
    QString m_currentActiveCallPeer;
};
#endif // NETWORKENGINE_H
