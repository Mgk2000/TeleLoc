#ifndef NETWORKENGINE_H
#define NETWORKENGINE_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QDateTime>

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

public:
    explicit NetworkEngine(QObject *parent = nullptr);

    QString myName() const { return m_myName; }
    void setMyName(const QString &name);

    bool isRegistered() const { return m_isRegistered; }

    QString chatLog() const { return m_chatLog; }

    QString activeChatPeer() const { return m_activeChatPeer; }
    void setActiveChatPeer(const QString &peer);

    Q_INVOKABLE void startCall(const QString &targetName);
    Q_INVOKABLE void sendTextMessage(const QString &text);
    Q_INVOKABLE void saveNameToFile(const QString &name);
    Q_INVOKABLE void resetRegistration();

signals:
    void myNameChanged();
    void isRegisteredChanged();
    void chatLogChanged();
    void activeChatPeerChanged();

    // СИГНАЛ-ВСПЫШКА: Заставит QML автоматически распахнуть окно чата!
    void requestOpenChat(QString fromPeer);

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

    QTimer *m_pingTimer;
    QList<OfflineMessage> m_offlineQueue;

    QString getConfigPath(const QString &fileName) const;
    void loadNameFromFile();

    void saveQueueToFile();
    void loadQueueFromFile();
    void cleanOldMessages();
};

#endif // NETWORKENGINE_H
