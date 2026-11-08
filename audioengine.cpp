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

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
    , m_worker(nullptr)
    , m_isEchoTestMode(false)
{
    m_maxSpeakerLevel = getAndroidMaxSpeakerVolume();

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
    qDebug() << "@@@ [Controller] Noise Suppress toggled to:" << sup;

    if (m_worker->isRunning()) {
        QMetaObject::invokeMethod(m_worker, [this]() {
                // Динамически перенастраиваем VoIP режим (AEC/NS) без полного перезапуска аудио-потоков
                m_worker->setAndroidVoipMode(true);
            }, Qt::QueuedConnection);
    }
}

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
    qDebug() << "@@@audio data=" << m_worker->speakerDataCount;
    if (m_worker->wasSound && !m_worker->speakerDataCount) {
        this->stop();
    }
    m_worker->speakerDataCount = 0;
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

        // Читаем сжатый Opus-пакет из сети UDP
        compressedChunk.resize(static_cast<int>(m_udpReceiver->pendingDatagramSize()));
        int dataSize = compressedChunk.size();
        speakerDataCount++;
        wasSound = true;

        m_udpReceiver->readDatagram(compressedChunk.data(), dataSize, &senderAddress, &senderPort);
        if (compressedChunk.isEmpty()) continue;

        if (m_isEchoTestMode && m_udpSender) {
            m_udpSender->writeDatagram(compressedChunk, senderAddress, 28002);
            continue;
        }

        // Временный буфер для разжатия PCM (запас 960 сэмплов)
        QByteArray pcmChunk;
        int maxFrameSize = 960;
        pcmChunk.resize(maxFrameSize * sizeof(short));

        // ВЕРНУЛИ ОПУС-ДЕКОДЕР: Превращаем сжатый сетевой чанк обратно в сырой PCM
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
                // Подрезаем буфер под реальное количество декодированных сэмплов (обычно 320)
                pcmChunk.resize(decodedSamples * sizeof(short));

                // ИСПРАВЛЕНО: Записываем в файл чистый, только что декодированный PCM!
                if (m_writeToFile) {
                    openFiles();
                    m_unixFileReceiverNet.write(pcmChunk);
                    m_unixSizeReceiverNet += pcmChunk.size();
                }
            } else {
                qWarning() << "@@@ [Opus] Ошибка декодирования кадра:" << decodedSamples;
                continue;
            }
        } else {
            continue;
        }

        // Расчет уровня громкости для ползунка/шкалы интерфейса Qt
        int len = pcmChunk.size();
        int currentVolume = 0;
        const short *ptr = reinterpret_cast<const short*>(pcmChunk.constData());
        for (int i = 0; i < len / 2; ++i) {
            int sample = ptr[i];
            if (sample < 0) sample = -sample;
            if (sample > currentVolume) currentVolume = sample;
        }
        currentVolume = (currentVolume * 100) / 32767;
        if (currentVolume > 0) {
            emit speakerVolumeMeasured(currentVolume);
        }

        // ПРИМЕНЯЕМ ОБНОВЛЕННЫЙ ФИЛЬТР БАТТЕРВОРТА (жесткий срез скрипов выше 4.5 кГц)
        applyFilters(reinterpret_cast<short*>(pcmChunk.data()), pcmChunk.size() / 2);

        if (m_writeToFile) {
            openFiles();
            m_unixFileReceiverOut.write(pcmChunk);
            m_unixSizeReceiverOut += pcmChunk.size();
        }

        // Кладём очищенный и отфильтрованный PCM в буфер, откуда его плавно заберет динамик Oboe
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
    // ------------------------------------------------------------------------
    // СТАБИЛЬНЫЙ ЖЕЛЕЗОБЕТОННЫЙ ЗАХВАТ МИКРОФОНА (ЗАЩИТА ОТ ПОТЕРЬ КАДРОВ)
    // ------------------------------------------------------------------------
    oboe::AudioStreamBuilder recorderBuilder;
    recorderBuilder.setDirection(oboe::Direction::Input);

    // Переводим микрофон из LowLatency в режим максимальной стабильности потока.
    // Это заставит Android выделить глубокий системный буфер для захвата голоса.
    recorderBuilder.setPerformanceMode(oboe::PerformanceMode::None);
    recorderBuilder.setSharingMode(oboe::SharingMode::Shared);

    recorderBuilder.setSampleRate(16000);
    recorderBuilder.setChannelCount(1);
    recorderBuilder.setFormat(oboe::AudioFormat::I16);

    // Возвращаем исходный пресет (выберите нужный: VoiceCommunication или Camcorder)
    recorderBuilder.setInputPreset(oboe::InputPreset::VoiceCommunication);

    recorderBuilder.setDataCallback(this);
    recorderBuilder.setErrorCallback(this);

    oboe::Result recResult = recorderBuilder.openManagedStream(m_recordingStream);
    if (recResult != oboe::Result::OK) {
        qWarning() << "@@@ [Oboe] Failed to open recording stream";
    } else {
        m_androidSessionId = m_recordingStream->getSessionId();

        // КРИТИЧЕСКИЙ ШАГ: Принудительно расширяем внутренний буфер записи Oboe в 4 раза!
        // Это создаст глубокий резервуар прямо в аудио-драйвере Android.
        // Пока Qt обрабатывает сеть или графику, звук будет монолитно копиться в этой подушке.
        int32_t burstSize = m_recordingStream->getFramesPerBurst();
        // Если это планшет — даем ему максимальный запас (x8), так как его процессор медленнее.
        // Для телефона Poco X7 оставляем стабильный x6.
        if (isTablet) {
            m_recordingStream->setBufferSizeInFrames(burstSize * 8);
            qDebug() << "@@@ [Oboe] Планшет: буфер захвата микрофона расширен до максимума:" << (burstSize * 8);
        } else {
            m_recordingStream->setBufferSizeInFrames(burstSize * 6);
            qDebug() << "@@@ [Oboe] Телефон: буфер захвата микрофона расширен до:" << (burstSize * 6);
        }    }
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
    qDebug() << "@@@ [Worker] Starting audio streams";

    setAndroidVoipMode(true, m_androidSessionId);

#if defined(Q_OS_ANDROID)
    if (m_playbackStream) {
        m_playbackStream->requestStart();
    }
    if (m_recordingStream) {
        m_recordingStream->requestStart();
    }
#else
    m_audioOutputDevice = m_audioSink->start();
    m_audioInputDevice = m_audioSource->start();

    if (!m_audioInputDevice) {
        qWarning() << "@@@ [Worker] Failed to open audio input device!";
        return;
    }

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
    // ПОТОК МИКРОФОНА: НАКОПЛЕНИЕ И СЖАТИЕ В OPUS
    // ------------------------------------------------------------------------
    if (audioStream->getDirection() == oboe::Direction::Input) {
        QByteArray data16k(reinterpret_cast<const char*>(audioData), bytesNeeded);

        if (m_writeToFile) {
            openFiles();
            m_unixFileSenderIn.write(data16k);
            m_unixSizeSenderIn += data16k.size();
        }

        static QByteArray micAccumulator;
        micAccumulator.append(data16k);

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

            // Мягкий эхо-демпинг
            bool isPartnerSpeaking = false;
            {
                QMutexLocker locker(&m_bufferMutex);
                isPartnerSpeaking = !m_jitterBuffer.isEmpty();
            }

            if (isPartnerSpeaking) {
                for (int i = 0; i < len / 2; ++i) {
                    pcmPtr[i] = static_cast<short>(pcmPtr[i] * 0.35f);
                }
            } else if (m_micLevel != 30) {
                float factor = static_cast<float>(m_micLevel) / 100.0f;
                for (int i = 0; i < len / 2; ++i) {
                    int calculatedSample = static_cast<int>(pcmPtr[i] * factor);
                    if (calculatedSample > 32767) calculatedSample = 32767;
                    if (calculatedSample < -32768) calculatedSample = -32768;
                    pcmPtr[i] = static_cast<short>(calculatedSample);
                }
            }

            // ИСПРАВЛЕНО: Записываем в WAV-файл ЧИСТЫЙ PCM (chunk) вместо сжатых байт!
            if (m_writeToFile) {
                openFiles();
                m_unixFileSenderNet.write(chunk);
                m_unixSizeSenderNet += chunk.size();
            }

            // Кодируем фрейм в Opus для передачи по сети
            if (m_opusEncoder && m_udpSender && !m_targetIp.isEmpty()) {
                QByteArray compressed;
                compressed.resize(4000);

                int compressedBytes = opus_encode(
                    reinterpret_cast<OpusEncoder*>(m_opusEncoder),
                    reinterpret_cast<const opus_int16*>(chunk.constData()),
                    320, // 20 мс фрейм
                    reinterpret_cast<unsigned char*>(compressed.data()),
                    compressed.size()
                    );

                if (compressedBytes > 0) {
                    compressed.resize(compressedBytes);
                    // Отправляем сжатый пакет по сети
                    m_udpSender->writeDatagram(compressed, QHostAddress(m_targetIp), 28002);
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    // ПОТОК ДИНАМИКА: АДАПТИВНЫЙ БУФЕР СГЛАЖИВАНИЯ
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

void AudioEngine::setSpeakerLevel(int level, bool save, bool sign) {
    if (level == 0)
        qCritical() << "@@@sound speak=00000000000000000000";
    if (remoteControl)
    {
        netEngine->setRemoteSpeakerLevel(level);
        return;
    }

    m_worker->setSpeakerLevel(level);

    // Рассчитываем абсолютную громкость на основе лимитов потока звонка
    setAndroidAbsoluteSpeakerVolume(level * m_maxSpeakerLevel / 100.0f);

    if (save)
        saveConfigLevels();

    qDebug() << "@@@sound setSpeakerLevel" << level << m_maxSpeakerLevel << save << sign;
    if (sign)
        emit remoteSetSpeakerLevel(level);
}

void AudioEngine::setWriteToFile(bool w)
{
    m_writeToFile = w;
    qDebug() << "@@@sound setWriteToFile=" << w;
}

// ------------------------------------------------------------------------
// НАЖАТЫЕ ИСПРАВЛЕННЫЕ НАТИВНЫЕ МЕТОДЫ ANDROID (STREAM_VOICE_CALL = 0)
// ------------------------------------------------------------------------

void setAndroidAbsoluteSpeakerVolume(int volume) {
#ifdef Q_OS_ANDROID
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject audioServiceString = QJniObject::getStaticObjectField("android/content/Context", "AUDIO_SERVICE", "Ljava/lang/String;");
    QJniObject audioManager = context.callObjectMethod("getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", audioServiceString.object());

    if (audioManager.isValid()) {
        // КРИТИЧЕСКИ ВАЖНО: Для голосовой связи используем STREAM_VOICE_CALL (0) вместо STREAM_MUSIC (3)
        int streamType = 0;
        int flags = 0; // 0 - менять тихо в фоне, 1 - показать шторку

        audioManager.callMethod<void>("setStreamVolume", "(III)V", streamType, (jint)volume, flags);
    }
#endif
}

int getAndroidMaxSpeakerVolume() {
#ifdef Q_OS_ANDROID
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject audioServiceString = QJniObject::getStaticObjectField("android/content/Context", "AUDIO_SERVICE", "Ljava/lang/String;");
    QJniObject audioManager = context.callObjectMethod("getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", audioServiceString.object());

    if (audioManager.isValid()) {
        // КРИТИЧЕСКИ ВАЖНО: Запрашиваем максимум для STREAM_VOICE_CALL (0)
        return audioManager.callMethod<jint>("getStreamMaxVolume", "(I)I", 0);
    }
#endif
    return 5; // У потока звонка обычно от 5 до 7 шагов регулировки (в отличие от 15 у музыки)
}

int getAndroidAbsoluteSpeakerVolume() {
#ifdef Q_OS_ANDROID
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject audioServiceString = QJniObject::getStaticObjectField("android/content/Context", "AUDIO_SERVICE", "Ljava/lang/String;");
    QJniObject audioManager = context.callObjectMethod("getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", audioServiceString.object());

    if (audioManager.isValid()) {
        // КРИТИЧЕСКИ ВАЖНО: Читаем текущую громкость потока разговора STREAM_VOICE_CALL (0)
        int streamType = 0;
        int currentVol = audioManager.callMethod<jint>("getStreamVolume", "(I)I", streamType);
        return currentVol;
    }
#endif
    return 100;
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
