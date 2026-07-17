#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H
#include <QTcpServer>
#include <QTcpSocket>
#include <QObject>
#include <QByteArray>
#include <QMediaCaptureSession>
#include <QAudioInput>
#include <QAudioSink>
#include <QAudioSource>
#include <QMediaDevices>
#include <QAudioFormat>
#include <QBuffer>

class AudioEngine : public QObject
{
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine();

    // Методы управления звуком, которые вызывает NetworkEngine
    void startRecording();
    void playFrame(const QByteArray &frame);
    void stop();

signals:
    // Сигнал, сообщающий сети, что готов новый кусочек голоса с микрофона
    void frameReady(const QByteArray &frame);

private slots:
    void handleInputReady();

private:
    QAudioSource *m_audioSource = nullptr;
    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_inputDevice = nullptr;
    QIODevice *m_outputDevice = nullptr;
    QAudioFormat m_format;
};

#endif // AUDIOENGINE_H
