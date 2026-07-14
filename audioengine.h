#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QIODevice>
#include <QAudioSource>
#include <QAudioSink>
#include <QAudioFormat>
#include <QByteArray>

class AudioEngine : public QIODevice
{
    Q_OBJECT
    // Добавляем официальное свойство для прыгающей полоски микрофона в QML
    Q_PROPERTY(double micVolume READ micVolume NOTIFY micVolumeChanged)

public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine();

    void start();
    void stop();

    double micVolume() const { return m_micVolume; }

signals:
    void audioDataReady(const QByteArray &data);
    void micVolumeChanged();

public slots:
    void playAudioBlock(const QByteArray &data);

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;

private:
    QAudioFormat m_format;
    QAudioSource *m_audioSource;
    QAudioSink *m_audioSink;
    QByteArray m_buffer;
    double m_micVolume; // Хранилище уровня громкости
};

#endif // AUDIOENGINE_H
