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
#include <QFile>
// Вспомогательная структура для правильного WAV-заголовка
struct WAVHeader {
    char chunkId[4] = {'R', 'I', 'F', 'F'};
    quint32 chunkSize = 0;
    char format[4] = {'W', 'A', 'V', 'E'};
    char subchunk1Id[4] = {'f', 'm', 't', ' '};
    quint32 subchunk1Size = 16;
    quint16 audioFormat = 1; // PCM
    quint16 numChannels = 1;  // Моно
    quint32 sampleRate = 16000; // 16 кГц (или ваша частота рации)
    quint32 byteRate = 32000;   // sampleRate * numChannels * bitsPerSample/8
    quint16 blockAlign = 2;     // numChannels * bitsPerSample/8
    quint16 bitsPerSample = 16; // 16 бит
    char subchunk2Id[4] = {'d', 'a', 't', 'a'};
    quint32 subchunk2Size = 0;
};

class AudioEngine : public QObject
{
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine();

    void startRecording(const QString &targetIp);
    void stop();
    // НОВЫЕ МЕТОДЫ ДЛЯ ОТЛАДОЧНОЙ ПАНЕЛИ ЗАПИСИ И ВОСПРОИЗВЕДЕНИЯ
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void playRecordedFile();
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
    const int AUDIO_PORT = 28001;
    QTimer* volumeTimer;
private:
    QAudioFormat m_audioFormat;
    QFile m_recordFile;
    bool m_isRecording = false;

    // Объекты для воспроизведения записанного файла
    QFile m_playFile;
signals:
    void micVolumeChanged(int volume);
    void netVolumeChanged(int volume);

};

#endif // AUDIOENGINE_H
