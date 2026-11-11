#include "audioengine.h"
#include "networkengine.h"
#include <unistd.h>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QJsonObject>
#include <QDateTime>
#include <opus.h>
#include <QAudioFormat>
#include <QAudioSource>
#include <QAudioSink>
#include <QAudioDevice>
// ============================================================================
// РЕАЛИЗАЦИЯ CONTROLLER (РАБОТАЕТ В MAIN/GUI ПОТОКЕ)
// ============================================================================
bool m_writeToFile = false;
int m_savedMicLevel, m_savedSpeakerLevel;
int remoteMicLevel, remoteSpeakerLevel;
int m_maxSpeakerLevel;
int m_micLevel = 30, m_speakerLevel = 30;
bool noiseSuppressFlag = true; // По умолчанию включено для борьбы со свистом
bool m_Notch = true;           // Флаг включения/выключения Notch-фильтра
bool m_preventWhistle = true;
qint64 m_sampleCounter = 0;
int m_whistleLimit =10000000;
int m_maxNoWhistleLevel;
bool m_whistle;
int m_whistleTicks;
QJniObject audioManager;
jint maxSpeakVolume, maxMusicVolume;
jint VOICE_STREAM_TYPE, MUSIC_STREAM_TYPE;

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
    , m_worker(nullptr)
    , m_isEchoTestMode(false)
{
    initAudioManager();
    readLevels();

    // 1. Создаем воркер и поток для звука
    m_worker = new AudioWorker();
    m_worker->moveToThread(&m_workerThread);

    // 2. Связываем сигналы управления потоком
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    // Перенаправляем замеры громкости из потока звука в GUI/QML интерфейс
    connect(m_worker, &AudioWorker::micVolumeMeasured, this, &AudioEngine::micVolumeUpdated);
    connect(m_worker, &AudioWorker::speakerVolumeMeasured, this, &AudioEngine::netVolumeUpdated);

    // 3. Запускаем поток обработки звука с наивысшим приоритетом
    m_workerThread.start(QThread::TimeCriticalPriority);

    speakerTimer.setInterval(1000);
    connect(&speakerTimer, SIGNAL(timeout()), this, SLOT(onSpeakerTick()));
    initConfigLevels();
}

AudioEngine::~AudioEngine()
{
    m_workerThread.quit();
    m_workerThread.wait();
}

void AudioEngine::startRecording(const QString &targetIp)
{
    qDebug() << "@@@sound startRecording write=" << m_writeToFile;
    initConfigLevels();

    // Синхронизируем базовые флаги перед запуском
    m_worker->setWriteToFile(m_writeToFile);
    m_worker->setEchoTest(m_isEchoTestMode);
    m_worker->wasSound = false;
    speakerTimer.start();

    // Безопасно инициализируем и запускаем Oboe внутри контекста QThread
    QMetaObject::invokeMethod(m_worker, [this, targetIp]() {
            m_worker->initAudio(targetIp);
            m_worker->startAudio();
        }, Qt::QueuedConnection);
}

void AudioEngine::stop()
{
    qDebug() << "@@@ [Controller] Requesting audio stop";
    QMetaObject::invokeMethod(m_worker, &AudioWorker::stopAudio, Qt::QueuedConnection);
    speakerTimer.stop();
    netEngine->closeCallDialog();

    restoreLevels();
}

void AudioEngine::startWriteToFile()
{
    m_writeToFile = true;
    QMetaObject::invokeMethod(m_worker, [this]() {
            m_worker->setWriteToFile(true);
        }, Qt::QueuedConnection);
}

void AudioEngine::stopWriteToFile()
{
    m_writeToFile = false;
    emit stopWritingSound();
    QMetaObject::invokeMethod(m_worker, [this]() {
            m_worker->setWriteToFile(false);
        }, Qt::QueuedConnection);
}

void AudioEngine::muteMicrophone(bool mute)
{
    QMetaObject::invokeMethod(m_worker, [this, mute]() {
            m_worker->setMute(mute);
        }, Qt::QueuedConnection);
}

void AudioEngine::setMicLevel(int level, bool save, bool sign)
{
    qDebug() << "@@@sound setMicLevel" << level << "remote" << remoteControl << "save=" << save << "signal" << sign;
    if (remoteControl)
    {
        netEngine->setRemoteMicLevel(level);
        return;
    }
    QMetaObject::invokeMethod(m_worker, [this, level]() {
            m_worker->setMicLevel(level);
        }, Qt::QueuedConnection);

    if (save)
        saveConfigLevels();
    if (sign)
        emit remoteSetMicLevel(level);
}

void AudioEngine::toggleEchoTest()
{
    m_isEchoTestMode = !m_isEchoTestMode;
    qDebug() << "@@@ [Controller] Echo test toggled to:" << m_isEchoTestMode;
    QMetaObject::invokeMethod(m_worker, [this]() {
            m_worker->setEchoTest(m_isEchoTestMode);
        }, Qt::QueuedConnection);
}

void AudioEngine::setNoiseSuppress(bool sup)
{
    noiseSuppressFlag = sup;
    qDebug() << "@@@sound [Controller] Noise Suppress toggled to:" << sup;

    if (false && m_worker->isRunning()) {
        QMetaObject::invokeMethod(m_worker, [this]() {
                // Динамически перенастраиваем VoIP режим (AEC/NS) без полного перезапуска аудио-потоков
                m_worker->setAndroidVoipMode(true);
            }, Qt::QueuedConnection);
    }
}

void AudioEngine::setNotch(bool bn) {m_Notch = bn;}

void AudioEngine::setPreventWhistle(bool bn)
{
    m_preventWhistle = bn;
}
bool Notch() {return m_Notch;}
void AudioEngine::readLevels() {
    // Ваша существующая логика чтения уровней
}

int AudioEngine::getRemoteSpeakerLevel() const { return remoteSpeakerLevel; }
void AudioEngine::setRemoteSpeakerLevel(int newRemoteSpeakerLevel) { remoteSpeakerLevel = newRemoteSpeakerLevel; }
int AudioEngine::getRemoteMicLevel() const { return remoteMicLevel; }
void AudioEngine::setRemoteMicLevel(int newRemoteMicLevel) { remoteMicLevel = newRemoteMicLevel; }

void AudioEngine::saveConfigLevels()
{
    auto conf = readJsonConfig();
    conf["mic_level"] = getMicLevel();
    conf["speaker_level"] = getSpeakerLevel();
    writeJsonConfig(conf);
    qDebug() << "@@@sound SaveConfig mic=" << getMicLevel() << "speaker=" << getSpeakerLevel();
}
void AudioEngine::initConfigLevels()
{
    auto conf = readJsonConfig();
    int mic = conf["mic_level"].toInt(30);
    int speak = conf["speaker_level"].toInt(30);
    setMicLevel(mic, false, true);
    setSpeakerLevel(speak, false, true);
    qDebug() << "@@@sound init mic=" << mic << "speak=" << speak;
}

int AudioEngine::currMicLevel()
{
    return remoteControl ? remoteMicLevel : getMicLevel();
}

int AudioEngine::currSpeakerLevel()
{
    return remoteControl ? remoteSpeakerLevel : getSpeakerLevel();
}

void AudioEngine::onSpeakerTick()
{
    m_whistle = m_sampleCounter > m_whistleLimit;
    if (m_whistle) {
        qCritical() << "@@@sound audio data=" << m_worker->speakerDataCount << m_sampleCounter << getSpeakerVolumePercentage(true);
        if (m_speakerLevel>=30 && (m_whistleTicks == 0 || m_whistleTicks >= 5))
            m_maxNoWhistleLevel -= 5;
            m_speakerLevel -=20;
            setSpeakerLevel(m_speakerLevel, true, true);
            if (m_whistleTicks  >=5) m_whistleTicks = 0;
            else m_whistleTicks++;
        }
    else {
        qDebug() << "@@@sound audio data=" << m_worker->speakerDataCount << m_sampleCounter  << getSpeakerVolumePercentage(true) << maxSpeakVolume << maxMusicVolume;
        if (m_speakerLevel < m_maxNoWhistleLevel) {
                m_speakerLevel += 5;
                setSpeakerLevel(m_speakerLevel, true, true);
                m_whistleTicks = 0;
        }
    }
    if (m_worker->wasSound && !m_worker->speakerDataCount) {
        this->stop();
    }
    m_worker->speakerDataCount = 0;
    m_sampleCounter = 0;
}

// ============================================================================
// РЕАЛИЗАЦИЯ WORKER (ВЫПОЛНЯЕТСЯ СТРОГО В КОНТЕКСТЕ QTHREAD)
// ============================================================================

AudioWorker::AudioWorker(QObject *parent)
    : QObject(parent)
{
    QString musicDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    QDir().mkpath(musicDir);
    if (!musicDir.endsWith('/')) musicDir += '/';
    m_unixFileSenderIn.setFileName(musicDir + "senderIn.wav");
    m_unixFileSenderNet.setFileName(musicDir + "senderNet.wav");
    m_unixFileReceiverNet.setFileName(musicDir + "receiverNet.wav");
    m_unixFileReceiverOut.setFileName(musicDir + "receiverOut.wav");
}

AudioWorker::~AudioWorker()
{
    stopAudio();
}

void AudioWorker::stopAudio()
{
    qDebug() << "@@@ [Worker] Stopping audio engine and closing structures";

#if defined(Q_OS_ANDROID)
    // 1. Остановка Oboe потоков воспроизведения и записи
    if (m_recordingStream) {
        m_recordingStream->requestStop();
        m_recordingStream->close();
        m_recordingStream.reset();
    }
    if (m_playbackStream) {
        m_playbackStream->requestStop();
        m_playbackStream->close();
        m_playbackStream.reset();
    }
#else
    // 1. Останавливаем таймер обработки буферов на Десктопе
    if (m_playbackTimer) {
        m_playbackTimer->stop();
        m_playbackTimer->deleteLater();
        m_playbackTimer = nullptr;
    }

    // 2. Отключаем и уничтожаем микрофон на Десктопе
    if (m_audioInputDevice) {
        m_audioInputDevice->disconnect(this);
        m_audioInputDevice = nullptr;
    }
    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
    }

    // 3. Останавливаем динамик на Десктопе
    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink->deleteLater();
        m_audioSink = nullptr;
    }
    m_audioOutputDevice = nullptr;
#endif

    // 4. Закрываем и сохраняем отладочные файлы (.wav)
    setWriteToFile(false);

    // 5. Разрушаем кодеки Opus
    freeOpus();

    // 6. Освобождаем сетевые сокеты
    if (m_udpReceiver) {
        m_udpReceiver->close();
        m_udpReceiver->deleteLater();
        m_udpReceiver = nullptr;
    }
    if (m_udpSender) {
        m_udpSender->close();
        m_udpSender->deleteLater();
        m_udpSender = nullptr;
    }

    // Очищаем внутренние буферы
    {
        QMutexLocker locker(&m_bufferMutex);
        m_jitterBuffer.clear();
    }

    // Переводим аудиосистему Android обратно в штатный режим (Normal)
    setAndroidVoipMode(false, 0);
}

bool AudioWorker::isRunning()
{
    return !!m_udpSender;
}

void AudioWorker::setAndroidVoipMode(bool enable, int audioSessionId)
{
#ifdef Q_OS_ANDROID
    qWarning() << "@@@sound setAndroidVoipMode init 1. Enable:" << enable << "SessionID:" << audioSessionId;
    QJniEnvironment env;
    if (!env.isValid()) return;

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) return;

    QJniObject audioServiceString = QJniObject::getStaticObjectField(
        "android/content/Context", "AUDIO_SERVICE", "Ljava/lang/String;");
    if (!audioServiceString.isValid()) return;

    QJniObject audioManager = context.callObjectMethod(
        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", audioServiceString.object());
    if (!audioManager.isValid()) return;

    // Режим связи: 3 (MODE_IN_COMMUNICATION), обычный: 0 (MODE_NORMAL)
    int mode = enable ? 3 : 0;
    audioManager.callMethod<void>("setMode", "(I)V", mode);

    if (enable)
    {
        // Включаем громкую связь для планшета
        audioManager.callMethod<void>("setSpeakerphoneOn", "(Z)V", true);

        // Проверяем и инициализируем Аппаратное Эхоподавление (AEC)
        jboolean aecAvailable = QJniObject::callStaticMethod<jboolean>(
            "android/media/audiofx/AcousticEchoCanceler", "isAvailable");
        qDebug() << "@@@sound setAndroidVoipMode Hardware AcousticEchoCanceler available:" << aecAvailable;

        if (aecAvailable && audioSessionId > 0) {
            QJniObject aecObject = QJniObject::callStaticObjectMethod(
                "android/media/audiofx/AcousticEchoCanceler",
                "create",
                "(I)Landroid/media/audiofx/AcousticEchoCanceler;",
                audioSessionId
                );

            if (aecObject.isValid()) {
                jint result = aecObject.callMethod<jint>("setEnabled", "(Z)I", true);
                qDebug() << "@@@sound Hardware AEC binding result:" << (result == 0 ? "SUCCESS" : "ERROR");
            }
        }

        // Проверяем и инициализируем Подавление Шума/Свиста (NS)
        jboolean nsAvailable = QJniObject::callStaticMethod<jboolean>(
            "android/media/audiofx/NoiseSuppression", "isAvailable");
        qDebug() << "@@@sound Hardware NoiseSuppression available:" << nsAvailable;

        if (nsAvailable && audioSessionId > 0) {
            QJniObject nsObject = QJniObject::callStaticObjectMethod(
                "android/media/audiofx/NoiseSuppression",
                "create",
                "(I)Landroid/media/audiofx/NoiseSuppression;",
                audioSessionId
                );

            if (nsObject.isValid()) {
                // if флаг noiseSuppressFlag выключен в GUI, мы явно передаем false, чтобы отключить эффект
                jint result = nsObject.callMethod<jint>("setEnabled", "(Z)I", noiseSuppressFlag);
                qDebug() << "@@@sound Hardware NS state set to:" << noiseSuppressFlag << "result:" << (result == 0 ? "SUCCESS" : "ERROR");
            }
        }
    }
    qDebug() << "@@@sound setAndroidVoipMode finished successfully";
#else
    Q_UNUSED(enable);
    Q_UNUSED(audioSessionId);
#endif
}
void AudioWorker::onReadyReadUdp()
{
    if (!m_udpReceiver) return;

    while (m_udpReceiver->hasPendingDatagrams()) {
        QByteArray compressedChunk;
        QHostAddress senderAddress;
        quint16 senderPort;

        compressedChunk.resize(static_cast<int>(m_udpReceiver->pendingDatagramSize()));
        m_udpReceiver->readDatagram(compressedChunk.data(), compressedChunk.size(), &senderAddress, &senderPort);
        if (compressedChunk.isEmpty()) continue;

        speakerDataCount++;
        wasSound = true;

        if (m_isEchoTestMode && m_udpSender) {
            m_udpSender->writeDatagram(compressedChunk, senderAddress, 28002);
            continue;
        }

        QByteArray pcmChunk;
        int maxFrameSize = 960;
        pcmChunk.resize(maxFrameSize * sizeof(short));

        if (m_opusDecoder) {
            int decodedSamples = opus_decode(
                reinterpret_cast<OpusDecoder*>(m_opusDecoder),
                reinterpret_cast<const unsigned char*>(compressedChunk.constData()),
                compressedChunk.size(),
                reinterpret_cast<short*>(pcmChunk.data()),
                maxFrameSize,
                0
                );

            if (decodedSamples > 0) {
                pcmChunk.resize(decodedSamples * sizeof(short));
                if (m_writeToFile) {
                    openFiles();
                    m_unixFileReceiverNet.write(pcmChunk);
                    m_unixSizeReceiverNet += pcmChunk.size();
                }
            } else {
                continue;
            }
        } else {
            continue;
        }

        int samplesCount = pcmChunk.size() / sizeof(short);
        short *pcmPtr = reinterpret_cast<short*>(pcmChunk.data());

        // ------------------------------------------------------------------------
        // СБОР МЕТРИК ЗВУКА И ОПРЕДЕЛЕНИЕ ЧАСТОТЫ СВИСТА
        // ------------------------------------------------------------------------
        int zeroCrossings = 0;
        int maxAmp = 0;

        for (int i = 0; i < samplesCount; ++i) {
            int sample = pcmPtr[i];
            if (sample < 0) sample = -sample;
            if (sample > maxAmp) maxAmp = sample;
            m_sampleCounter += sample;

            if (i > 0) {
                zeroCrossings += static_cast<int>((pcmPtr[i - 1] ^ pcmPtr[i]) < 0);
            }
        }

        int currentVolume = (maxAmp * 100) / 32767;
        emit speakerVolumeMeasured(currentVolume);
        // ------------------------------------------------------------------------
        // 3. ПРИМЕНЕНИЕ КИХ-ФИЛЬТРА СГЛАЖИВАНИЯ (Срез песка выше 4.5 КГц)
        // ------------------------------------------------------------------------
        if (samplesCount > 2) {
            QByteArray filterCopy = pcmChunk;
            const short *srcPtr = reinterpret_cast<const short*>(filterCopy.constData());
            for (int i = 1; i < samplesCount - 1; ++i) {
                int smoothed = (static_cast<int>(srcPtr[i - 1]) +
                                static_cast<int>(srcPtr[i]) +
                                static_cast<int>(srcPtr[i + 1])) / 3;
                pcmPtr[i] = static_cast<short>(smoothed);
            }
        }
        if (m_Notch){
        // ------------------------------------------------------------------------
        // ПРИМЕНЕНИЕ ГРОМКОСТИ, ЭХО-ДЕМПИНГА И ДИНАМИЧЕСКОГО ГЛУШИТЕЛЯ
        // ------------------------------------------------------------------------
        float finalAttenuator = m_autoVolumeAttenuator;
        if (m_micLevel == 0) {
            finalAttenuator *= 0.4f; // Мягкий дуплексный демпинг
        }

        if (finalAttenuator < 1.0f) {
            for (int i = 0; i < samplesCount; ++i) {
                int adjusted = static_cast<int>(pcmPtr[i] * finalAttenuator);
                if (adjusted > 32767) adjusted = 32767;
                if (adjusted < -32768) adjusted = -32768;
                pcmPtr[i] = static_cast<short>(adjusted);
            }
        }
        }
        if (m_writeToFile) {
            openFiles();
            m_unixFileReceiverOut.write(pcmChunk);
            m_unixSizeReceiverOut += pcmChunk.size();
        }

        // Отправляем в джиттер-очередь Oboe
        {
            QMutexLocker locker(&m_bufferMutex);
            m_jitterBuffer.enqueue(pcmChunk);
            if (m_jitterBuffer.size() > 15) {
                m_jitterBuffer.dequeue();
            }
        }
    }
}

namespace oboe_lib = oboe;

void AudioWorker::initAudio(const QString &targetIp)
{
    m_targetIp = targetIp;

    if (!m_udpReceiver) {
        m_udpReceiver = new QUdpSocket(this);
        m_udpReceiver->bind(QHostAddress::AnyIPv4, 28002);
        connect(m_udpReceiver, &QUdpSocket::readyRead, this, &AudioWorker::onReadyReadUdp);
    }
    if (!m_udpSender) {
        m_udpSender = new QUdpSocket(this);
    }

    initOpus();

#if defined(Q_OS_ANDROID)
    qDebug() << "@@@ [Oboe] Инициализация сырого студийного потока микрофона...";

    m_recordingStream.reset();
    m_playbackStream.reset();

    // ------------------------------------------------------------------------
    // НАСТРОЙКА МИКРОФОНА: СЫРОЙ НЕОБРАБОТАННЫЙ ПОТОК (Защита от дыр)
    // ------------------------------------------------------------------------
    oboe::AudioStreamBuilder recorderBuilder;
    recorderBuilder.setDirection(oboe::Direction::Input);

    // ОТКЛЮЧАЕМ агрессивный LowLatency. Пусть ОС отдает данные редкими, но большими и целыми блоками!
    recorderBuilder.setPerformanceMode(oboe::PerformanceMode::None);
    recorderBuilder.setSharingMode(oboe::SharingMode::Shared);

    // Ставим нативную частоту 44100 Гц. Это уберет системный ресемплер Android, который рвет звук
    recorderBuilder.setSampleRate(44100);
    recorderBuilder.setChannelCount(1);
    recorderBuilder.setFormat(oboe::AudioFormat::I16);

    // КРИТИЧНО: Пресет Unprocessed отключает абсолютно все телефонные шумодавы ОС
    recorderBuilder.setInputPreset(oboe::InputPreset::Unprocessed);

    recorderBuilder.setDataCallback(this);
    recorderBuilder.setErrorCallback(this);

    oboe::Result recResult = recorderBuilder.openManagedStream(m_recordingStream);
    if (recResult != oboe::Result::OK) {
        qCritical() << "@@@ [Oboe] Не удалось открыть микрофон:" << oboe::convertToText(recResult);
        return;
    }
    m_androidSessionId = m_recordingStream->getSessionId();

    // Запрашиваем у аудиоплаты глубокий резервуар для накопления звука (x8)
    int32_t recBurstSize = m_recordingStream->getFramesPerBurst();
    m_recordingStream->setBufferSizeInFrames(recBurstSize * 8);
    qDebug() << "@@@ [Oboe] Микрофон успешно открыт на 44100 Гц в режиме Unprocessed";

    // ------------------------------------------------------------------------
    // ШАГ 2: НАСТРОЙКА И ОТКРЫТИЕ ДИНАМИКА (ПЕРЕДАЧА ПО ССЫЛКЕ)
    // ------------------------------------------------------------------------
    oboe::AudioStreamBuilder playbackBuilder;
    playbackBuilder.setDirection(oboe::Direction::Output);

    if (isTablet) {
        playbackBuilder.setPerformanceMode(oboe::PerformanceMode::None);
        playbackBuilder.setSharingMode(oboe::SharingMode::Shared);
    } else {
        playbackBuilder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
        playbackBuilder.setSharingMode(oboe::SharingMode::Exclusive);
    }

    playbackBuilder.setSampleRate(16000);
    playbackBuilder.setChannelCount(1);
    playbackBuilder.setFormat(oboe::AudioFormat::I16);

    playbackBuilder.setUsage(oboe::Usage::VoiceCommunication);
    playbackBuilder.setContentType(oboe::ContentType::Speech);
    playbackBuilder.setDataCallback(this);
    playbackBuilder.setErrorCallback(this);

    // Связываем сессию воспроизведения с глобальным микшером звонков Android
    playbackBuilder.setSessionId(oboe::SessionId::Allocate);

    // Вызов СТРОГО по сигнатуре: передаем ссылку на m_playbackStream в качестве аргумента
    oboe::Result playResult = playbackBuilder.openManagedStream(m_playbackStream);

    if (playResult != oboe::Result::OK) {
        qCritical() << "@@@ [Oboe] Ошибка открытия динамика по ссылке:" << oboe::convertToText(playResult);
        return;
    }

    // Проверяем валидность инициализации
    if (m_playbackStream) {
        int32_t playBurstSize = m_playbackStream->getFramesPerBurst();
        if (isTablet) {
            m_playbackStream->setBufferSizeInFrames(playBurstSize * 4);
        } else {
            m_playbackStream->setBufferSizeInFrames(playBurstSize * 2);
        }
        qDebug() << "@@@ [Oboe] Поток m_playbackStream УСПЕШНО инициализирован и готов к работе!";
    } else {
        qCritical() << "@@@ [Oboe] Крах: Функция вернула OK, но m_playbackStream остался равен 0!";
    }

#else
    // НАСТРОЙКА QT MULTIMEDIA ДЛЯ ДЕСКТОПА
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    m_audioSink = new QAudioSink(QMediaDevices::defaultAudioOutput(), format, this);
    m_audioSink->setVolume(static_cast<float>(m_speakerLevel) / 100.0f);
    m_audioSink->setBufferSize(35280);

    QAudioDevice voiceInputDevice = QMediaDevices::defaultAudioInput();
    m_audioSource = new QAudioSource(voiceInputDevice, format, this);

    if (!m_playbackTimer) {
        m_playbackTimer = new QTimer(this);
        connect(m_playbackTimer, &QTimer::timeout, this, &AudioWorker::processPlayback);
    }
#endif
}
void AudioWorker::startAudio()
{
    qDebug() << "@@@ [Worker] Запуск воспроизведения звука Oboe...";
    m_maxNoWhistleLevel = m_speakerLevel;
    // Включаем нативный Voip-режим Android
    setAndroidVoipMode(true, 0);

#if defined(Q_OS_ANDROID)
    if (m_playbackStream) {
        oboe::Result result = m_playbackStream->requestStart();
        if (result != oboe::Result::OK) {
            qCritical() << "@@@ [Oboe] ОшибкаrequestStart динамика:" << oboe::convertToText(result);
        } else {
            qDebug() << "@@@ [Oboe] Динамик успешно переведен в режим Started!";
        }
    }

    if (m_recordingStream) {
        oboe::Result result = m_recordingStream->requestStart();
        if (result != oboe::Result::OK) {
            qCritical() << "@@@ [Oboe] Ошибка requestStart микрофона:" << oboe::convertToText(result);
        } else {
            qDebug() << "@@@ [Oboe] Микрофон успешно переведен в режим Started!";
        }
    }
#else
    m_audioOutputDevice = m_audioSink->start();
    m_audioInputDevice = m_audioSource->start();

    if (m_playbackTimer) {
        m_playbackTimer->start(20);
    }
    connect(m_audioInputDevice, &QIODevice::readyRead, this, &AudioWorker::onMicDataReady);
#endif
}
#if defined(Q_OS_ANDROID)
// ============================================================================
// ВЫСОКОПРИОРИТЕТНЫЕ КОЛЛБЭКИ GOOGLE OBOE (РАБОТАЮТ В ПОТОКЕ ANDROID AUDIO)
// ============================================================================
#include <algorithm>

#include <algorithm>

#include <algorithm>

#include <algorithm>
#include <cmath>

oboe::DataCallbackResult AudioWorker::onAudioReady(oboe::AudioStream *audioStream, void *audioData, int32_t numFrames)
{
    if (m_isMuted || !audioData || numFrames <= 0) {
        if (audioStream->getDirection() == oboe::Direction::Output) {
            memset(audioData, 0, numFrames * sizeof(short));
        }
        return oboe::DataCallbackResult::Continue;
    }

    int bytesNeeded = numFrames * sizeof(short);

    // ------------------------------------------------------------------------
    // ОБРАБОТКА ВВОДА: Микрофон (МАКСИМАЛЬНО ОБЛЕГЧЕННЫЙ ПОТОК)
    // ------------------------------------------------------------------------
    if (audioStream->getDirection() == oboe::Direction::Input) {
        const short *inputBuffer44k = reinterpret_cast<const short*>(audioData);

        int samples16kCount = static_cast<int>(numFrames * 16000.0f / 44100.0f);
        QByteArray data16k;
        data16k.resize(samples16kCount * sizeof(short));
        short *data16kPtr = reinterpret_cast<short*>(data16k.data());

        for (int i = 0; i < samples16kCount; ++i) {
            int srcIndex = static_cast<int>(i * 44100.0f / 16000.0f);
            if (srcIndex < numFrames) {
                data16kPtr[i] = inputBuffer44k[srcIndex];
            } else {
                data16kPtr[i] = 0;
            }
        }

        if (m_writeToFile) {
            openFiles();
            m_unixFileSenderIn.write(data16k);
            m_unixSizeSenderIn += data16k.size();
            m_unixFileSenderNet.write(data16k);
            m_unixSizeSenderNet += data16k.size();
        }

        static QByteArray micAccumulator;
        micAccumulator.append(data16k);

        while (micAccumulator.size() >= 640) {
            QByteArray chunk = micAccumulator.left(640);
            micAccumulator.remove(0, 640);

            if (m_opusEncoder && m_udpSender && !m_targetIp.isEmpty()) {
                QByteArray compressed;
                compressed.resize(4000);

                int compressedBytes = opus_encode(
                    reinterpret_cast<OpusEncoder*>(m_opusEncoder),
                    reinterpret_cast<const opus_int16*>(chunk.constData()),
                    320,
                    reinterpret_cast<unsigned char*>(compressed.data()),
                    compressed.size()
                    );

                if (compressedBytes > 0) {
                    compressed.resize(compressedBytes);
                    m_udpSender->writeDatagram(compressed, QHostAddress(m_targetIp), 28002);
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    // ПОТОК ДИНАМИКА: Стабильное чтение из сетевого буфера (Без изменений)
    // ------------------------------------------------------------------------
    if (audioStream->getDirection() == oboe::Direction::Output) {
        short *outputBuffer = reinterpret_cast<short*>(audioData);
        int samplesNeeded = numFrames;

        static QByteArray staticPlaybackBuffer;
        static bool isJitterBuffering = true;

        {
            QMutexLocker locker(&m_bufferMutex);
            while (!m_jitterBuffer.isEmpty()) {
                staticPlaybackBuffer.append(m_jitterBuffer.dequeue());
            }
        }

        int bytesAvailable = staticPlaybackBuffer.size();
        int samplesAvailable = bytesAvailable / sizeof(short);

        if (isTablet) {
            if (isJitterBuffering) {
                if (samplesAvailable < samplesNeeded * 6) {
                    memset(outputBuffer, 0, bytesNeeded);
                    return oboe::DataCallbackResult::Continue;
                }
                isJitterBuffering = false;
            }
        }

        if (samplesAvailable >= samplesNeeded) {
            const short *framePtr = reinterpret_cast<const short*>(staticPlaybackBuffer.constData());
            float speakerFactor = static_cast<float>(m_speakerLevel) / 100.0f;

            for (int i = 0; i < samplesNeeded; ++i) {
                int outSample = static_cast<int>(framePtr[i] * speakerFactor);
                if (outSample > 32767) outSample = 32767;
                if (outSample < -32768) outSample = -32768;
                outputBuffer[i] = static_cast<short>(outSample);
            }

            staticPlaybackBuffer.remove(0, bytesNeeded);
        } else {
            memset(outputBuffer, 0, bytesNeeded);
            if (isTablet) {
                isJitterBuffering = true;
            }
        }
    }

    return oboe::DataCallbackResult::Continue;
}
void AudioWorker::onErrorAfterClose(oboe::AudioStream *audioStream, oboe::Result error)
{
    qWarning() << "@@@ [Oboe] Stream error encountered:" << oboe::convertToText(error) << ". Re-initializing audio.";
    if (error == oboe::Result::ErrorDisconnected) {
        QMetaObject::invokeMethod(this, [this]() {
                initAudio(m_targetIp);
                startAudio();
            }, Qt::QueuedConnection);
    }
}
#endif
// ============================================================================
// ОСТАВШАЯСЯ ЛОГИКА WORKER (ВЫПОЛНЯЕТСЯ СТРОГО В КОНТЕКСТЕ QTHREAD)
// ============================================================================

#if !defined(Q_OS_ANDROID)
// Реализация для Десктопа (сохраняем оригинальную логику из вашего исходника)
void AudioWorker::onMicDataReady()
{
    if (!m_audioInputDevice || m_isMuted) return;

    QByteArray data = m_audioInputDevice->readAll();
    if (data.isEmpty()) return;

    if (m_writeToFile) {
        openFiles();
        m_unixFileSenderIn.write(data);
        m_unixSizeSenderIn += data.size();
    }

    static QByteArray micAccumulator;
    micAccumulator.append(data);

    while (micAccumulator.size() >= 640) {
        QByteArray chunk = micAccumulator.left(640);
        micAccumulator.remove(0, 640);

        int len = chunk.size();
        int currentVolume = 0;
        short *pcmPtr = reinterpret_cast<short*>(chunk.data());
        for (int i = 0; i < len / 2; ++i) {
            int sample = pcmPtr[i];
            if (sample < 0) sample = -sample;
            if (sample > currentVolume) currentVolume = sample;
        }
        currentVolume = (currentVolume * 100) / 32767;
        emit micVolumeMeasured(currentVolume);

        if (m_micLevel == 0) {
            bool isPartnerSpeaking = false;
            {
                QMutexLocker locker(&m_bufferMutex);
                isPartnerSpeaking = !m_jitterBuffer.isEmpty();
            }
            if (isPartnerSpeaking) {
                for (int i = 0; i < len / 2; ++i) {
                    pcmPtr[i] = static_cast<short>(pcmPtr[i] * 0.2f);
                }
            }
        }
        else {
            float factor = static_cast<float>(m_micLevel) / 100.0f;
            for (int i = 0; i < len / 2; ++i) {
                int calculatedSample = static_cast<int>(pcmPtr[i] * factor);
                if (calculatedSample > 32767) calculatedSample = 32767;
                if (calculatedSample < -32768) calculatedSample = -32768;
                pcmPtr[i] = static_cast<short>(calculatedSample);
            }
        }

        if (m_writeToFile) {
            openFiles();
            m_unixFileSenderNet.write(chunk);
            m_unixSizeSenderNet += chunk.size();
        }

        if (m_udpSender && m_opusEncoder) {
            unsigned char compressedData[256];
            int compressedBytes = opus_encode(
                reinterpret_cast<OpusEncoder*>(m_opusEncoder),
                reinterpret_cast<const opus_int16*>(chunk.constData()),
                320,
                compressedData,
                sizeof(compressedData)
                );

            if (compressedBytes > 0) {
                m_udpSender->writeDatagram(
                    reinterpret_cast<const char*>(compressedData),
                    compressedBytes,
                    QHostAddress(m_targetIp),
                    28002
                    );
            }
        }
    }
}

void AudioWorker::processPlayback()
{
    if (!m_audioOutputDevice || !m_audioSink) return;

    QByteArray pcmFrame;
    static bool isPlaybackStarted = false;

    {
        QMutexLocker locker(&m_bufferMutex);

        if (!isPlaybackStarted) {
            if (m_jitterBuffer.size() < m_minJitterPackets) {
                return;
            }
            isPlaybackStarted = true;
        }

        if (m_jitterBuffer.isEmpty()) {
            isPlaybackStarted = false;
            return;
        }

        pcmFrame = m_jitterBuffer.dequeue();
    }

    if (!pcmFrame.isEmpty()) {
        m_audioOutputDevice->write(pcmFrame);
    }
}
#endif

void AudioWorker::openFiles()
{
    if (m_unixFileSenderIn.isOpen())
        return;
    m_unixFileSenderIn.open(QIODevice::WriteOnly);
    m_unixFileSenderNet.open(QIODevice::WriteOnly);
    m_unixFileReceiverNet.open(QIODevice::WriteOnly);
    m_unixFileReceiverOut.open(QIODevice::WriteOnly);
}

void AudioWorker::setWriteToFile(bool enable)
{
    m_writeToFile = enable;

    if (m_writeToFile) {
        m_unixSizeSenderIn = 0; m_unixSizeSenderNet = 0;
        m_unixSizeReceiverNet = 0; m_unixSizeReceiverOut = 0;
        openFiles();
        QByteArray dummyHeader(44, 0);
        m_unixFileSenderIn.write(dummyHeader);
        m_unixFileSenderNet.write(dummyHeader);
        m_unixFileReceiverNet.write(dummyHeader);
        m_unixFileReceiverOut.write(dummyHeader);
    } else {
        if (m_unixFileSenderIn.isOpen()) { writeWavHeader(m_unixFileSenderIn, m_unixSizeSenderIn); m_unixFileSenderIn.close(); }
        if (m_unixFileSenderNet.isOpen()) { writeWavHeader(m_unixFileSenderNet, m_unixSizeSenderNet); m_unixFileSenderNet.close(); }
        if (m_unixFileReceiverNet.isOpen()) { writeWavHeader(m_unixFileReceiverNet, m_unixSizeReceiverNet); m_unixFileReceiverNet.close(); }
        if (m_unixFileReceiverOut.isOpen()) { writeWavHeader(m_unixFileReceiverOut, m_unixSizeReceiverOut); m_unixFileReceiverOut.close(); }
        qDebug() << "@@@ [Worker] Отладочные WAV файлы успешно закрыты.";
    }
}

void AudioWorker::writeWavHeader(QFile &file, int dataSize)
{
    file.seek(0);
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);

    out.writeRawData("RIFF", 4);
    out << static_cast<quint32>(36 + dataSize);
    out.writeRawData("WAVE", 4);
    out.writeRawData("fmt ", 4);
    out << static_cast<quint32>(16);
    out << static_cast<quint16>(1);
    out << static_cast<quint16>(1);
    out << static_cast<quint32>(16000);
    out << static_cast<quint32>(32000);
    out << static_cast<quint16>(2);
    out << static_cast<quint16>(16);
    out.writeRawData("data", 4);
    out << static_cast<quint32>(dataSize);
}

void AudioWorker::applyFilters(short *samples, int count)
{
    // Коэффициенты фильтра Баттерворта 2-го порядка
    // Частота дискретизации = 16000 Гц, частота среза = 4500 Гц
    const float b0 = 0.4034f;
    const float b1 = 0.8068f;
    const float b2 = 0.4034f;
    const float a1 = -0.1349f;
    const float a2 = 0.4785f;

    for (int i = 0; i < count; ++i) {
        float x0 = static_cast<float>(samples[i]);
        float y0 = (b0 * x0) + (b1 * m_x1) + (b2 * m_x2) - (a1 * m_y1) - (a2 * m_y2);

        m_x2 = m_x1;
        m_x1 = x0;
        m_y2 = m_y1;
        m_y1 = y0;

        // Защита от выхода за границы
        if (y0 > 32767.0f) y0 = 32767.0f;
        if (y0 < -32768.0f) y0 = -32768.0f;

        samples[i] = static_cast<short>(y0);
    }
}

void AudioWorker::initOpus()
{
    int error = 0;
    m_opusEncoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &error);
    if (error == OPUS_OK) {
        opus_encoder_ctl(reinterpret_cast<OpusEncoder*>(m_opusEncoder), OPUS_SET_BITRATE(16000));
    }
    m_opusDecoder = opus_decoder_create(16000, 1, &error);
}

void AudioWorker::freeOpus()
{
    if (m_opusEncoder) {
        opus_encoder_destroy(reinterpret_cast<OpusEncoder*>(m_opusEncoder));
        m_opusEncoder = nullptr;
    }
    if (m_opusDecoder) {
        opus_decoder_destroy(reinterpret_cast<OpusDecoder*>(m_opusDecoder));
        m_opusDecoder = nullptr;
    }
}
void AudioWorker::setMute(bool mute) { m_isMuted = mute; }
void AudioWorker::setEchoTest(bool enable) { m_isEchoTestMode = enable; }

void AudioEngine::setSpeakerLevel(int level, bool save, bool automatic) {
    if (level == 0)
        qCritical() << "@@@sound speak=00000000000000000000";
    if (remoteControl)
    {
        netEngine->setRemoteSpeakerLevel(level);
        return;
    }

    m_worker->setSpeakerLevel(level);

    // Рассчитываем абсолютную громкость на основе лимитов потока звонка
    //setAndroidAbsoluteSpeakerVolume(level * m_maxSpeakerLevel / 100.0f);
    setSpeakerVolumePercentage(true, level, false);
    if (save)
        saveConfigLevels();

    qDebug() << "@@@sound setSpeakerLevel" << level << m_maxSpeakerLevel << save << automatic;
    if (automatic)
        emit remoteSetSpeakerLevel(level);
    else m_maxNoWhistleLevel = level;
}

void AudioEngine::setWriteToFile(bool w)
{
    m_writeToFile = w;
    qDebug() << "@@@sound setWriteToFile=" << w;
}
void AudioWorker::detectAndSuppressWhistle(short *samples, int count)
{
    if (count <= 0) return;

    int zeroCrossings = 0;
    int maxAmp = 0;

    // 1. Считаем количество переходов через ноль и ищем пиковую амплитуду кадра
    for (int i = 1; i < count; ++i) {
        // Если знаки соседних сэмплов разные — волна пересекла ноль
        if ((samples[i - 1] >= 0 && samples[i] < 0) || (samples[i - 1] < 0 && samples[i] >= 0)) {
            zeroCrossings++;
        }

        int absSample = samples[i] < 0 ? -samples[i] : samples[i];
        if (absSample > maxAmp) {
            maxAmp = absSample;
        }
    }

    // 2. Анализируем стабильность частоты (с допуском микро-погрешности в 3 перехода)
    bool isFrequencyStable = (std::abs(zeroCrossings - m_lastZeroCrossings) <= 3);

    // Порог тишины: детектор не должен срабатывать, если в комнате просто тихо (шумы ниже ~3% громкости)
    bool isLoudEnough = (maxAmp > 1000);

    if (isFrequencyStable && isLoudEnough && (maxAmp > m_lastMaxAmplitude)) {
        // Амплитуда растет на одной и той же частоте! Накапливаем счетчик опасности
        m_consecutiveGrowthFrames++;
    } else {
        // Естественное колебание речи или пауза — плавно сбрасываем счетчик
        if (m_consecutiveGrowthFrames > 0) m_consecutiveGrowthFrames--;
    }

    // Сохраняем значения для следующего кадра
    m_lastZeroCrossings = zeroCrossings;
    m_lastMaxAmplitude = maxAmp;

    // 3. РЕАКЦИЯ: Если звук монотонно растет 8 кадров подряд (~160 мс) — это начало свиста!
    if (m_consecutiveGrowthFrames >= 8) {
        qWarning() << "@@@ [DSP] Обнаружено начало развития свиста! Глушим поток. Пересечений нуля:" << zeroCrossings;

        // Мгновенно зажимаем громкость до 15%, пресекая свист в зародыше
        m_autoVolumeAttenuator = 0.15f;

        // Сбрасываем счетчик, чтобы дать системе шанс перепроверить звук в следующем цикле
        m_consecutiveGrowthFrames = 0;
    }
    else {
        // Если свист прекратился, плавно (по 2% за кадр) возвращаем громкость к исходной,
        // чтобы у пользователя не было ощущения резкого обрыва связи
        if (m_autoVolumeAttenuator < 1.0f) {
            m_autoVolumeAttenuator += 0.02f;
            if (m_autoVolumeAttenuator > 1.0f) m_autoVolumeAttenuator = 1.0f;
        }
    }

    // 4. Применяем наш динамический аттенюатор к текущим сэмплам
    if (m_autoVolumeAttenuator < 1.0f) {
        for (int i = 0; i < count; ++i) {
            samples[i] = static_cast<short>(samples[i] * m_autoVolumeAttenuator);
        }
    }
}

void setAndroidSystemMicMute(bool mute) {
#ifdef Q_OS_ANDROID
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject audioServiceString = QJniObject::getStaticObjectField("android/content/Context", "AUDIO_SERVICE", "Ljava/lang/String;");
    QJniObject audioManager = context.callObjectMethod("getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", audioServiceString.object());

    if (audioManager.isValid()) {
        audioManager.callMethod<void>("setMicrophoneMute", "(Z)V", (jboolean)mute);
    }
#endif
}
#include <QCoreApplication>
#include <QJniObject>
#include <QJniEnvironment>
#include <QDebug>
#include <algorithm>
void initAudioManager()
{
#ifdef Q_OS_ANDROID
    // 1. Получаем текущую активность Android (контекст)
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;"
        );

    if (!activity.isValid()) {
        qWarning() << "Ошибка: Не удалось получить Android Activity context.";
        return;
    }

    // 2. Получаем константу названия аудио-сервиса (Context.AUDIO_SERVICE)
    QJniObject audioServiceName = QJniObject::getStaticObjectField(
        "android/content/Context",
        "AUDIO_SERVICE",
        "Ljava/lang/String;"
        );

    // 3. Запрашиваем системный сервис AudioManager
    audioManager = activity.callObjectMethod(
        "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;",
        audioServiceName.object<jstring>()
        );

    // Кэшируем только неизменяемые ID потоков
    VOICE_STREAM_TYPE = QJniObject::getStaticField<jint>(
        "android/media/AudioManager",
        "STREAM_VOICE_CALL"
        );
    MUSIC_STREAM_TYPE = QJniObject::getStaticField<jint>(
        "android/media/AudioManager",
        "STREAM_MUSIC"
        );
#endif
}

int getSpeakerVolumePercentage(bool voice) {
#ifdef Q_OS_ANDROID
    if (!audioManager.isValid()) {
        qWarning() << "AudioManager не инициализирован! Вызовите initAudioManager сначала.";
        return -1;
    }

    // 1. Определяем тип потока
    jint streamType = voice ? VOICE_STREAM_TYPE : MUSIC_STREAM_TYPE;

    // 2. Получаем АКТУАЛЬНЫЕ значения текущего шага и максимума шкалы
    jint currentVol = audioManager.callMethod<jint>(
        "getStreamVolume",
        "(I)I",
        streamType
        );

    jint maxVol = audioManager.callMethod<jint>(
        "getStreamMaxVolume",
        "(I)I",
        streamType
        );

    // Проверяем JNI на исключения
    QJniEnvironment env;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        qWarning() << "Ошибка: Исключение Android при чтении громкости.";
        return -1;
    }

    // 3. Защита от деления на ноль
    if (maxVol <= 0) {
        return 0;
    }

    // 4. Переводим в проценты и ограничиваем диапазон [0, 100]
    int percentage = (currentVol * 100) / maxVol;
    return std::clamp(percentage, 0, 100);
#else
    Q_UNUSED(voice);
    return 100; // Дефолтное значение для других ОС
#endif
}
void setSpeakerVolumePercentage(bool voice, int percentage, bool speakerphone) {
#ifdef Q_OS_ANDROID
    if (!audioManager.isValid()) {
        qWarning() << "AudioManager не инициализирован! Вызовите initAudioManager сначала.";
        return;
    }

    // 1. Управление громкой связью (Speakerphone) и режимом аудио
    jint MODE_IN_COMMUNICATION = QJniObject::getStaticField<jint>(
        "android/media/AudioManager",
        "MODE_IN_COMMUNICATION"
        );
    jint MODE_NORMAL = QJniObject::getStaticField<jint>(
        "android/media/AudioManager",
        "MODE_NORMAL"
        );

    if (speakerphone) {
        audioManager.callMethod<void>("setMode", "(I)V", MODE_IN_COMMUNICATION);
        audioManager.callMethod<void>("setSpeakerphoneOn", "(Z)V", true);
    } else {
        audioManager.callMethod<void>("setSpeakerphoneOn", "(Z)V", false);
        audioManager.callMethod<void>("setMode", "(I)V", MODE_NORMAL);
    }

    // 2. Запрос АКТУАЛЬНОГО максимума шкалы для выбранного потока
    jint streamType = voice ? VOICE_STREAM_TYPE : MUSIC_STREAM_TYPE;

    jint maxVol = audioManager.callMethod<jint>(
        "getStreamMaxVolume",
        "(I)I",
        streamType
        );

    // Защита: если операционная система вернула некорректное значение шкалы
    if (maxVol <= 0) {
        qWarning() << "Не удалось получить максимальную громкость для потока:" << streamType;
        return;
    }

    // 3. Расчет и установка громкости
    percentage = std::clamp(percentage, 0, 100);

    // Математическое округление
    jint targetVolume = (percentage * maxVol + 50) / 100;
    targetVolume = std::clamp(targetVolume, 0, maxVol);

    // Устанавливаем громкость
    audioManager.callMethod<void>(
        "setStreamVolume",
        "(III)V",
        streamType,
        targetVolume,
        0 // 0 означает скрытое изменение (без вывода системного ползунка на экран)
        );

    // Проверяем JNI на исключения
    QJniEnvironment env;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        qWarning() << "Ошибка: Исключение Android при изменении громкости или режима связи.";
    }
#else
    Q_UNUSED(voice);
    Q_UNUSED(percentage);
    Q_UNUSED(speakerphone);
#endif
}
