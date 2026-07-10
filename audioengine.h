#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QIODevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QAudioSink>

// В Qt 6 для прямого захвата буфера наследуемся от QIODevice
class AudioEngine : public QIODevice
{
    Q_OBJECT
    Q_PROPERTY(double micVolume READ micVolume NOTIFY micVolumeChanged)

public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine();

    double micVolume() const { return m_micVolume; }

    // Обязательные системные методы для QIODevice в Qt 6
    bool open(OpenMode mode) override;
    void close() override;
    qint64 readData(char *data, qint64 maxlen) override { Q_UNUSED(data); Q_UNUSED(maxlen); return 0; }

    // СЮДА ANDROID БУДЕТ СИЛОЙ СЛИВАТЬ БАЙТЫ МИКРОФОНА
    qint64 writeData(const char *data, qint64 len) override;

public slots:
    void startAudio();
    void stopAudio();
    void handleIncomingAudio(const QByteArray &audioData);

signals:
    void audioReadyToPacket(const QByteArray &audioData);
    void micVolumeChanged();

private:
    QAudioFormat m_format;
    QAudioSource *m_audioSource;
    QAudioSink   *m_audioSink;
    QIODevice    *m_speakerDevice;

    bool m_isAudioActive;
    double m_micVolume;
};

#endif // AUDIOENGINE_H
