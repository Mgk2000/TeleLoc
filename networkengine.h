#ifndef NETWORKENGINE_H
#define NETWORKENGINE_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QDateTime>
#include <QHostAddress>
#include "audioengine.h"

struct OfflineMessage {
    QString from;
    QString to;
    QString text;
    QDateTime timestamp;
};

class NetworkEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString myName READ myName WRITE setMyName NOTIFY myNameChanged)
    Q_PROPERTY(bool isRegistered READ isRegistered NOTIFY isRegisteredChanged)
    Q_PROPERTY(QString chatLog READ chatLog NOTIFY chatLogChanged)
    Q_PROPERTY(QString activeChatPeer READ activeChatPeer WRITE setActiveChatPeer NOTIFY activeChatPeerChanged)
    Q_PROPERTY(QString callStatus READ callStatus NOTIFY callStatusChanged)

public:
    explicit NetworkEngine(QObject *parent = nullptr);

    QString myName() const { return m_myName; }
    void setMyName(const QString &name);

    bool isRegistered() const { return m_isRegistered; }

    QString chatLog() const { return m_chatLog; }
    void setActiveChatPeer(const QString &peer);

    QString activeChatPeer() const { return m_activeChatPeer; }
    QString callStatus() const { return m_callStatus; }

    void setAudioEngine(AudioEngine *eng) { m_audioEngine = eng; }

    Q_INVOKABLE void startCall(const QString &targetName);
    Q_INVOKABLE void acceptCall();
    Q_INVOKABLE void rejectOrEndCall();
    Q_INVOKABLE void sendTextMessage(const QString &text);
    Q_INVOKABLE void saveNameToFile(const QString &name);
    Q_INVOKABLE void resetRegistration();
    Q_INVOKABLE void clearChatHistory(const QString &peer);

signals:
    void sendAudioBlock(const QByteArray &audioData);

    void myNameChanged();
    void isRegisteredChanged();
    void chatLogChanged();
    void activeChatPeerChanged();
    void callStatusChanged();

    void callStarted();
    void callEnded();
    void requestOpenChat(QString fromPeer);

public slots:
    void sendAudioPacket(const QByteArray &audioData);

private slots:
    void readPendingDatagrams();
    void onPingTimer();

private:
    QUdpSocket *m_socket;
    quint16 m_port;
    QString m_myName;
    bool m_isRegistered;
    QString m_chatLog;
    QString m_activeChatPeer;
    QString m_callStatus;

    QTimer *m_pingTimer;
    QList<OfflineMessage> m_offlineQueue;
    AudioEngine *m_audioEngine;

    QHostAddress m_subnetBroadcast;
    QHostAddress getRealHardwareAddress() const;
    QString getConfigPath(const QString &fileName) const;
    void loadNameFromFile();
    void saveQueueToFile();
    void loadQueueFromFile();
    void cleanOldMessages();
};

#endif // NETWORKENGINE_H
