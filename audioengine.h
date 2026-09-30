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
#include <QTimer>
class NetworkEngine;
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
    QIODevice *m_audioOutputDevice;
    QByteArray m_ringBuffer, m_micBuffer;
    void setWriteToFile(bool w) {writeToFile = w;}
    void toggleEchoTest()
    {
        enableEchoTest(!m_isEchoTestMode);
        qDebug() << "@@@echo="<< m_isEchoTestMode;
    }
    void enableEchoTest(bool enable);
signals:
    void micVolumeUpdated(int volume);
    void netVolumeUpdated(int volume);

private slots:
    void onReadyReadUdp();
    void onTimer();


private:
    QUdpSocket *m_udpAudioReceiver;
    QUdpSocket *m_udpAudioSender;
    QAudioSource *m_audioSource;
    QAudioSink *m_audioSink;
    QIODevice *m_audioInputDevice;
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
    NetworkEngine * netEngine;
    QTimer audioTimer;
    void startAudioTimer();
    int inAudioSize = 0, outAuioSize = 0;
    void setAndroidVoipMode(bool enable, int audioSessionId = 0);
    bool m_isTalking = false;
    bool firstReceive = true;
    bool firstSend = true;
    bool writeToFile = false;
    bool m_isEchoTestMode = false;
    void *m_opusEncoder = nullptr;
    void *m_opusDecoder = nullptr;
    qint64 m_lastTimeSpoken = 0;
    // Переменные для хранения истории входных сэмплов
    float m_x1 = 0.0f;
    float m_x2 = 0.0f;
    // Переменные для хранения истории выходных сэмплов
    float m_y1 = 0.0f;
    float m_y2 = 0.0f;
    bool m_isOutputPlaying = false;
private slots:
    void processAudioOutput();

};

#endif // AUDIOENGINE_H
