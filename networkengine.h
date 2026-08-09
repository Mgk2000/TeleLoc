#ifndef NETWORKENGINE_H
#define NETWORKENGINE_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QTcpSocket>
#include <QTcpServer>
#include <QUdpSocket>
#include <QSoundEffect>
#include <QUrl>
#include "audioengine.h"

struct UserInfo {
    QString name;
    QString ip0;
    QString ip1;
    QString ip2;
    bool isAlive;
};

class NetworkEngine : public QObject
{
    Q_OBJECT
public:
    explicit NetworkEngine(QObject *parent = nullptr);
    ~NetworkEngine();

    Q_INVOKABLE bool isNetTypeAvailable(int netType);
    Q_INVOKABLE void startAudioCall(const QString &targetPeerName, int netType);
    Q_INVOKABLE void acceptAudioCall(const QString &targetPeerName, int netType);
    Q_INVOKABLE void stopAudioCall();
    Q_INVOKABLE void sendMessage(const QString &targetPeer, const QString &text);
    Q_INVOKABLE void saveNameToFile(const QString &name);
    Q_INVOKABLE QStringList getUsers(int netType);
    Q_INVOKABLE void debugUsers();
    Q_INVOKABLE QString getSavedName();

signals:
    void peerListChanged();
    void messageReceived(const QString &fromIp, const QString &message);
    void incomingCall(const QString &peerName, int netType);
    void callAccepted();
    void callStopped();

private slots:
    void onNewConnection();
    void onReadyTcpRead();
    void onReadyUdpRead();
    void sendDiscovery();
    void updateInterfaces();

private:
    void parseIncomingSyncData(const QByteArray &data, const QString &senderIpStr);
    void readConfig();

    QTcpServer *tcpServer;
    QTcpSocket *tcpSocket;
    QTcpSocket *tcpClientSocket;
    QUdpSocket *udpSocket;
    AudioEngine *audioEngine;

    QVector<UserInfo> m_users;
    QString m_activePeerIp;
    int m_activeCallNetType;
    const int PORT = 28000;

    QSoundEffect *m_ringbackTone;
    QSoundEffect *m_busyTone;
    QSoundEffect *m_incomingRing;
    bool m_isCallActive;
};

#endif // NETWORKENGINE_H
