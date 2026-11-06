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
extern bool m_writeToFile;
extern int m_savedMicLevel, m_savedSpeakerLevel;
extern int remoteMicLevel, remoteSpeakerLevel;
extern int m_maxSpeakerLevel;
extern int m_micLevel, m_speakerLevel;

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
    void setAndroidVoipMode(bool enable, int audioSessionId = 0);
    int speakerDataCount;
    bool wasSound;
    int getMicLevel() const {return m_micLevel;}
    int getSpeakerLevel() const {return m_speakerLevel;}
    void setMicLevel (int l) {m_micLevel = l;}
    void setSpeakerLevel(int l) {m_speakerLevel = l;}
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

    bool m_isMuted = false;
    bool m_isEchoTestMode = false;

    void *m_opusEncoder = nullptr;
    void *m_opusDecoder = nullptr;

    float m_x1 = 0.0f, m_x2 = 0.0f;
    float m_y1 = 0.0f, m_y2 = 0.0f;
    void openFiles();
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

signals:
    void micVolumeUpdated(int volume);
    void netVolumeUpdated(int volume);
    void initMicLevel(int level);
    void initSpeakerLevel(int level);
    void stopWritingSound(); // Добавили пропущенный сигнал!
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

#include <QtCore/qnativeinterface.h>
#include <QCoreApplication>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#endif
// ПОЛУЧИТЬ АБСОЛЮТНУЮ ГРОМКОСТЬ ДИНАМИКА (возвращает значение от 0 до Max)
int getAndroidAbsoluteSpeakerVolume();
using namespace QNativeInterface;
// УСТАНОВИТЬ АБСОЛЮТНУЮ ГРОМКОСТЬ ДИНАМИКА (принимает значение от 0 до Max)
void setAndroidAbsoluteSpeakerVolume(int volume);

// ПОЛУЧИТЬ МАКСИМАЛЬНЫЙ ПРЕДЕЛ ГРОМКОСТИ УСТРОЙСТВА (обычно на Poco это 15 шагов)
int getAndroidMaxSpeakerVolume();
void setAndroidSystemMicMute(bool mute);

#endif // AUDIOENGINE_H
