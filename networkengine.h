#ifndef NETWORKENGINE_H
#define NETWORKENGINE_H

#include <QObject>
#include <QUdpSocket>

class NetworkEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString myName READ myName WRITE setMyName NOTIFY myNameChanged)

public:
    explicit NetworkEngine(QObject *parent = nullptr);

    QString myName() const { return m_myName; }
    void setMyName(const QString &name);

    // Метод вызова, который мы привяжем к кнопке 📞 в QML
    Q_INVOKABLE void startCall(const QString &targetName);

signals:
    void myNameChanged();

private slots:
    void readPendingDatagrams();

private:
    QUdpSocket *m_socket;
    quint16 m_port;
    QString m_myName;
};

#endif // NETWORKENGINE_H
