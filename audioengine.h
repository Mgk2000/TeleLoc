#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QObject>
#include <QThread>
#include <QUdpSocket>
#include <QAudioSource>
#include <QAudioSink>
#include <QMediaDevices>
#include <QIODevice>
#include <QByteArray>
#include <QFile>
#include <QTimer>
#include <QMutex>
#include <QQueue>

// ============================================================================
// WORKER: Вся тяжелая логика звука, Opus и UDP (выполняется в QThread)
// ============================================================================
class AudioWorker : public QObject
{
    Q_OBJECT
public:
    explicit AudioWorker(QObject *parent = nullptr);
    ~AudioWorker();

    void initAudio(const QString &targetIp);
    void startAudio();
    void stopAudio();

    void setWriteToFile(bool enable);
    void setMute(bool mute);
    void setEchoTest(bool enable);
    void setMicVolume(int level);
    void setSpeakerVolume(int level);
    void setAndroidVoipMode(bool enable, int audioSessionId = 0);

signals:
    void micVolumeMeasured(int volume);
    void speakerVolumeMeasured(int volume);

private slots:
    void onReadyReadUdp();
    void onMicDataReady();
    void processPlayback();

private:
    void writeWavHeader(QFile &file, int dataSize);
    void applyFilters(short *samples, int count);
    void initOpus();
    void freeOpus();

    QUdpSocket *m_udpReceiver = nullptr;
    QUdpSocket *m_udpSender = nullptr;
    QString m_targetIp;

    QAudioSource *m_audioSource = nullptr;
    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_audioInputDevice = nullptr;
    QIODevice *m_audioOutputDevice = nullptr;

    QTimer *m_playbackTimer = nullptr;
    QQueue<QByteArray> m_jitterBuffer;
    QMutex m_bufferMutex;
    int m_minJitterPackets = 3;

    QFile m_unixFileSenderIn;
    int   m_unixSizeSenderIn = 0;
    QFile m_unixFileSenderNet;
    int   m_unixSizeSenderNet = 0;
    QFile m_unixFileReceiverNet;
    int   m_unixSizeReceiverNet = 0;
    QFile m_unixFileReceiverOut;
    int   m_unixSizeReceiverOut = 0;

    bool m_writeToFile = false;
    bool m_isMuted = false;
    bool m_isEchoTestMode = false;

    int m_micLevel = 100;
    int m_speakerLevel = 100;

    void *m_opusEncoder = nullptr;
    void *m_opusDecoder = nullptr;

    float m_x1 = 0.0f, m_x2 = 0.0f;
    float m_y1 = 0.0f, m_y2 = 0.0f;
};

// ============================================================================
// CONTROLLER: Внешний интерфейс для QML / Widgets / NetworkEngine (Main Thread)
// ============================================================================
class AudioEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool writeToFile READ isWritingToFile WRITE setWriteToFile NOTIFY writeToFileChanged)
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine();

    Q_INVOKABLE void startRecording(const QString &targetIp);
    Q_INVOKABLE void stop();

    // Методы для обратной совместимости с вашим NetworkEngine
    Q_INVOKABLE void startWriteToFile();
    Q_INVOKABLE void stopWriteToFile();
    Q_INVOKABLE void setWriteToFile(bool enable);
    bool isWritingToFile() const { return m_writeToFile; }
    bool writeToFile = false; // Для прямой совместимости с кодом `if (audioEngine->writeToFile)`

    Q_INVOKABLE void muteMicrophone(bool mute);
    Q_INVOKABLE void setMicLevel(int level);
    Q_INVOKABLE void setSpeakerLevel(int level);

    Q_INVOKABLE int getMicLevel() { return m_micLevel; }
    Q_INVOKABLE int getSpeakerLevel() { return m_speakerLevel; }
    Q_INVOKABLE void toggleEchoTest();

signals:
    void micVolumeUpdated(int volume);
    void netVolumeUpdated(int volume);
    void initMicLevel(int level);
    void initSpeakerLevel(int level);
    void stopWritingSound(); // Добавили пропущенный сигнал!
    void writeToFileChanged();

private:
    void saveLevels();
    void readLevels();

    QThread m_workerThread;
    AudioWorker *m_worker = nullptr;

    int m_micLevel = 100;
    int m_speakerLevel = 100;
    bool m_writeToFile = false;
    bool m_isEchoTestMode = false;
};

#endif // AUDIOENGINE_H
