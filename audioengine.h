#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QObject>
#include <QAudioFormat>
#include <QAudioSource>
#include <QAudioSink>
#include <QIODevice>
#include <QTcpSocket>
#include <QTcpServer>
#include <QTimer>

class AudioEngine : public QObject
{
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine();

    void startRecording(const QString &targetIp);
    void stop();

    void writeAudioFrame(const QByteArray &data);
private slots:
    void onNewConnection();
    void onReadyRead();

private:
    QAudioFormat m_format;
    QAudioSource *m_audioSource;
    QAudioSink *m_audioSink;
    QIODevice *m_inputDevice;
    QIODevice *m_outputDevice;
    QTcpServer *m_tcpAudioServer;
    QTcpSocket *m_tcpAudioSocket;
    QTcpSocket *m_tcpAudioClient;
    const int AUDIO_PORT = 28002;
    QTimer* volumeTimer;
signals:
    void micVolumeChanged(int volume);
    void netVolumeChanged(int volume);

};

#endif // AUDIOENGINE_H
