#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QObject>
#include <QByteArray>
#include <QAudioSource>
#include <QAudioSink>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QIODevice>

class AudioEngine : public QObject
{
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine();

    void startRecording();
    void playFrame(const QByteArray &frame);
    void stop();

signals:
    void frameReady(const QByteArray &frame);

private slots:
    void handleInputReady();

private:
    QAudioSource *m_audioSource = nullptr;
    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_inputDevice = nullptr;
    QIODevice *m_outputDevice = nullptr;
    QAudioFormat m_format;
    QByteArray m_playbackBuffer;
};

#endif // AUDIOENGINE_H
