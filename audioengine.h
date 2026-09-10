#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QObject>
#include <QUdpSocket>
#include <QAudioSource>
#include <QAudioSink>
#include <QMediaDevices>
#include <QIODevice>
#include <QByteArray>
#include <QFile>

class AudioEngine : public QObject
{
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    void startRecording(const QString &targetIp);
    void stop();
    Q_INVOKABLE void startWriteToFile(const QString &role);
    Q_INVOKABLE void stopWriteToFile();
    Q_INVOKABLE void muteMicrophone(bool mute);
signals:
    void micVolumeUpdated(int volume);
    void netVolumeUpdated(int volume);

private slots:
    void onReadyReadUdp();

private:
    QUdpSocket *m_udpAudioReceiver;
    QUdpSocket *m_udpAudioSender;
    QAudioSource *m_audioSource;
    QAudioSink *m_audioSink;
    QIODevice *m_audioInputDevice;
    QIODevice *m_audioOutputDevice;
    QByteArray m_ringBuffer;
    QString m_targetIp;
    void writeWavHeader(QFile &file, int dataSize);
    // Файлы для стороны Отправителя
    QFile m_unixFileSenderIn;
    int   m_unixSizeSenderIn;
    QFile m_unixFileSenderNet;
    int   m_unixSizeSenderNet;

    // Файлы для стороны Получателя
    QFile m_unixFileReceiverNet;
    int   m_unixSizeReceiverNet;
    QFile m_unixFileReceiverOut;
    int   m_unixSizeReceiverOut;

    QString m_unixCurrentRole; // "sender", "receiver" или "none"
    bool m_unixIsMuted;
};

#endif // AUDIOENGINE_H
