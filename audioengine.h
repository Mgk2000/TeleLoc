#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QObject>
#include <QThread>
#include <QUdpSocket>
#include <QMediaDevices>
#include <QIODevice>
#include <QByteArray>
#include <QFile>
#include <QTimer>
#include <QMutex>
#include <QQueue>

// Подключаем Oboe (только для Android)
#ifdef Q_OS_ANDROID
#include <oboe/Oboe.h>
#else
// Оставляем Qt Multimedia для десктоп-версии (удобно для тестов)
#include <QAudioSource>
#include <QAudioSink>
#endif

extern bool m_writeToFile;
extern int m_savedMicLevel, m_savedSpeakerLevel;
extern int remoteMicLevel, remoteSpeakerLevel;
extern int m_maxSpeakerLevel;
extern int m_micLevel, m_speakerLevel;

// ============================================================================
// WORKER: Вся тяжелая логика звука, Opus, UDP и Oboe Callbacks
// ============================================================================
class AudioWorker : public QObject
#ifdef Q_OS_ANDROID
    , public oboe::AudioStreamDataCallback
    , public oboe::AudioStreamErrorCallback
#endif
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
    void setAndroidVoipMode(bool enable, int audioSessionId = 0);
    int speakerDataCount;
    bool wasSound;
    int getMicLevel() const {return m_micLevel;}
    int getSpeakerLevel() const {return m_speakerLevel;}
    void setMicLevel (int l) {m_micLevel = l;}
    void setSpeakerLevel(int l) {m_speakerLevel = l;}
    bool isRunning();

#ifdef Q_OS_ANDROID
    // --- Интерфейсы Oboe ---
    // Вызывается в отдельном высокоприоритетном потоке Android, когда готовы данные микрофона или нужен буфер для динамика
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *audioStream, void *audioData, int32_t numFrames) override;
    // Вызывается, если отключили наушники, изменился аудио-тракт и т.д.
    void onErrorAfterClose(oboe::AudioStream *audioStream, oboe::Result error) override;
#endif


signals:
    void micVolumeMeasured(int volume);
    void speakerVolumeMeasured(int volume);

    void NotchChanged();

private slots:
    void onReadyReadUdp();
    // Избавляемся от QTimer и readyRead() для микрофона на Android, Oboe пушит данные сам через onAudioReady
#if !defined(Q_OS_ANDROID)
    void onMicDataReady();
    void processPlayback();
#endif

private:
    void writeWavHeader(QFile &file, int dataSize);
    void applyFilters(short *samples, int count);
    void initOpus();
    void freeOpus();
    void openFiles();

    QUdpSocket *m_udpReceiver = nullptr;
    QUdpSocket *m_udpSender = nullptr;
    QString m_targetIp;

#ifdef Q_OS_ANDROID
    // Ссылки на потоки Oboe
    oboe::ManagedStream m_recordingStream;
    oboe::ManagedStream m_playbackStream;
    int32_t m_androidSessionId = 0;
private:
    // ... ваши существующие переменные (сокеты, файлы, Opus кодеки) ...

    // Добавляем постоянные буферы и флаги на уровень класса
    QByteArray m_localCaptureBuffer;
    QByteArray m_localPlaybackBuffer;
    QByteArray m_lastValidBackupFrame;
    bool m_isPlaybackActive = false;

#else
    // Сохраняем Qt Multimedia для Windows/Linux сборок
    QAudioSource *m_audioSource = nullptr;
    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_audioInputDevice = nullptr;
    QIODevice *m_audioOutputDevice = nullptr;
    QTimer *m_playbackTimer = nullptr;
#endif
private:
    // Переменные для детектора свиста
    int m_lastZeroCrossings = 0;   // Предыдущее количество пересечений нуля
    int m_consecutiveGrowthFrames = 0; // Счетчик кадров непрерывного роста
    int m_lastMaxAmplitude = 0;    // Предыдущая максимальная амплитуда
    float m_autoVolumeAttenuator = 1.0f; // Динамический множитель громкости (1.0 = норма, 0.2 = зажато)
    // Управление Notch-фильтром
    float m_notchFreq = 0.0f;      // Текущая обнаруженная частота свиста

    // Переменные истории для Notch-фильтра (2-й порядок требует 2 сэмпла истории)
    float m_nz1 = 0.0f, m_nz2 = 0.0f;
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

    bool m_isMuted = false;
    bool m_isEchoTestMode = false;

    void *m_opusEncoder = nullptr;
    void *m_opusDecoder = nullptr;

    float m_x1 = 0.0f, m_x2 = 0.0f;
    float m_y1 = 0.0f, m_y2 = 0.0f;
    void detectAndSuppressWhistle(short *samples, int count);
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

    Q_INVOKABLE void startWriteToFile();
    Q_INVOKABLE void stopWriteToFile();
    Q_INVOKABLE void setWriteToFile(bool enable);
    bool isWritingToFile() const { return m_writeToFile; }

    Q_INVOKABLE void muteMicrophone(bool mute);
    Q_INVOKABLE void setMicLevel(int level, bool save = false, bool signal = false);
    Q_INVOKABLE void setSpeakerLevel(int level, bool save = false, bool signal = false);

    Q_INVOKABLE int getMicLevel() { return m_worker->getMicLevel(); }
    Q_INVOKABLE int getSpeakerLevel() { return m_worker->getSpeakerLevel(); }
    Q_INVOKABLE void toggleEchoTest();
    Q_INVOKABLE void initConfigLevels();
    Q_INVOKABLE int currMicLevel();
    Q_INVOKABLE int currSpeakerLevel();
    Q_INVOKABLE int getRemoteMicLevel() const;
    Q_INVOKABLE void setRemoteMicLevel(int newRemoteMicLevel);

    Q_INVOKABLE int getRemoteSpeakerLevel() const;
    Q_INVOKABLE void setRemoteSpeakerLevel(int newRemoteSpeakerLevel);
    Q_INVOKABLE void setNoiseSuppress(bool sup);
    Q_INVOKABLE void setNotch(bool bn);
    Q_INVOKABLE void setPreventWhistle(bool bn);
signals:
    void micVolumeUpdated(int volume);
    void netVolumeUpdated(int volume);
    void initMicLevel(int level);
    void initSpeakerLevel(int level);
    void stopWritingSound();
    void writeToFileChanged();
    void remoteSetMicLevel(int);
    void remoteSetSpeakerLevel(int);
private:
    void readLevels();

    QThread m_workerThread;
    AudioWorker *m_worker = nullptr;

    void saveLevels (){m_savedMicLevel = m_micLevel; m_savedSpeakerLevel = m_speakerLevel;}
    void restoreLevels() {m_micLevel = m_savedMicLevel; m_speakerLevel = m_savedSpeakerLevel;}
    void saveConfigLevels();
    bool m_isEchoTestMode = false;
    QTimer speakerTimer;
private slots:
    void onSpeakerTick();
};

void initAudioManager();

int getSpeakerVolumePercentage(bool);
//using namespace QNativeInterface;
void setAndroidSystemMicMute(bool mute);
void setSpeakerVolumePercentage(bool voice, int percentage, bool speakerphone);
#endif // AUDIOENGINE_H
