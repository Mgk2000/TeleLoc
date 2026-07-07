#ifndef NETWORKENGINE_H
#define NETWORKENGINE_H

#include <QObject>
#include <QUdpSocket>

class NetworkEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString myName READ myName WRITE setMyName NOTIFY myNameChanged)
    Q_PROPERTY(bool isRegistered READ isRecording NOTIFY isRegisteredChanged)

public:
    explicit NetworkEngine(QObject *parent = nullptr);

    QString myName() const { return m_myName; }
    void setMyName(const QString &name);

    bool isRecording() const { return m_isRegistered; }

    Q_INVOKABLE void startCall(const QString &targetName);
    Q_INVOKABLE void saveNameToFile(const QString &name); // Сохранение на флешку
    Q_INVOKABLE void resetRegistration(); // Кнопка сброса имени

signals:
    void myNameChanged();
    void isRegisteredChanged();

private slots:
    void readPendingDatagrams();

private:
    QUdpSocket *m_socket;
    quint16 m_port;
    QString m_myName;
    bool m_isRegistered;

    QString getConfigPath() const; // Путь к файлу teleloc.conf
    void loadNameFromFile();       // Чтение с флешки при старте
};

#endif // NETWORKENGINE_H
