#include "audioengine.h"
#include "networkengine.h"
#include <unistd.h>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QJsonObject>
#include <QDateTime>
#include <opus.h>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#endif

// ============================================================================
// РЕАЛИЗАЦИЯ CONTROLLER (РАБОТАЕТ В MAIN/GUI ПОТОКЕ)
// ============================================================================

AudioEngine::AudioEngine(QObject *parent)

    : QObject(parent)
    , m_worker(nullptr)
    , m_micLevel(100)
    , m_speakerLevel(100)
    , m_writeToFile(false)
    , m_isEchoTestMode(false)
{
    readLevels();

    // 1. Создаем воркер и поток для звука
    m_worker = new AudioWorker();
    m_worker->moveToThread(&m_workerThread);

    // 2. Связываем сигналы управления потоком
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    // Перенаправляем замеры громкости из потока звука в GUI/QML интерфейс
    connect(m_worker, &AudioWorker::micVolumeMeasured, this, &AudioEngine::micVolumeUpdated);
    connect(m_worker, &AudioWorker::speakerVolumeMeasured, this, &AudioEngine::netVolumeUpdated);

    // 3. Запускаем поток обработки звука
    m_workerThread.start(QThread::TimeCriticalPriority); // Высокий приоритет для аудио
    speakerTimer.setInterval(1000);
    connect(&speakerTimer, SIGNAL (timeout()), this, SLOT(onSpeakerTick()));
}

AudioEngine::~AudioEngine()
{
    m_workerThread.quit();
    m_workerThread.wait();
}

void AudioEngine::startRecording(const QString &targetIp)
{
    qDebug() << "@@@ [Controller] Requesting audio start targeting IP:" << targetIp;

    // Синхронизируем текущие настройки перед запуском
    m_worker->setMute(m_micLevel == 0);
    m_worker->setMicVolume(m_micLevel);
    m_worker->setSpeakerVolume(m_speakerLevel);
    m_worker->setWriteToFile(m_writeToFile);
    m_worker->setEchoTest(m_isEchoTestMode);
    m_worker->wasSound = false;
    speakerTimer.start();

    // Безопасно инициализируем и запускаем аудиосистемы внутри контекста QThread
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

void AudioEngine::setMicLevel(int level)
{
    if (m_micLevel != level) {
        m_micLevel = level;
        saveLevels();
        QMetaObject::invokeMethod(m_worker, [this, level]() {
                m_worker->setMicVolume(level);
            }, Qt::QueuedConnection);
    }
}

void AudioEngine::setSpeakerLevel(int level)
{
    if (m_speakerLevel != level) {
        m_speakerLevel = level;
        saveLevels();
        QMetaObject::invokeMethod(m_worker, [this, level]() {
                m_worker->setSpeakerVolume(level);
            }, Qt::QueuedConnection);
    }
}

void AudioEngine::toggleEchoTest()
{
    m_isEchoTestMode = !m_isEchoTestMode;
    qDebug() << "@@@ [Controller] Echo test toggled to:" << m_isEchoTestMode;
    QMetaObject::invokeMethod(m_worker, [this]() {
            m_worker->setEchoTest(m_isEchoTestMode);
        }, Qt::QueuedConnection);
}

// Заглушки для сохранения уровней (реализуйте по вашей логике)
void AudioEngine::saveLevels() {}
void AudioEngine::readLevels() {
    emit initMicLevel(m_micLevel);
    emit initSpeakerLevel(m_speakerLevel);
}

void AudioEngine::onSpeakerTick()
{
    qDebug() << "@@@audio data=" << m_worker->speakerDataCount;
    if (m_worker-> wasSound && !m_worker->speakerDataCount) {
        this->stop();
        netEngine->closeCallDialog();
    }
    m_worker->speakerDataCount = 0;
}
#include "audioengine.h"
#include <QDebug>
#include <QDateTime>
#include <opus.h>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <QtCore/private/qandroidextras_p.h>
#endif

// ============================================================================
// РЕАЛИЗАЦИЯ WORKER (ВЫПОЛНЯЕТСЯ СТРОГО В КОНТЕКСТЕ QTHREAD)
// ============================================================================

AudioWorker::AudioWorker(QObject *parent)
    : QObject(parent)
{
}

AudioWorker::~AudioWorker()
{
    stopAudio();
}

void AudioWorker::stopAudio()
{
    qDebug() << "@@@ [Worker] Stopping audio engine and closing structures";

    // 1. Останавливаем таймер обработки буферов
    if (m_playbackTimer) {
        m_playbackTimer->stop();
        m_playbackTimer->deleteLater();
        m_playbackTimer = nullptr;
    }

    // 2. Отключаем и уничтожаем микрофон
    if (m_audioInputDevice) {
        m_audioInputDevice->disconnect(this);
        m_audioInputDevice = nullptr;
    }
    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
    }

    // 3. Останавливаем динамик
    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink->deleteLater();
        m_audioSink = nullptr;
    }
    m_audioOutputDevice = nullptr;

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
    //if (false)
    {
        QMutexLocker locker(&m_bufferMutex);
        m_jitterBuffer.clear();
    }

    // Переводим аудиосистему Android обратно в штатный режим (Normal)
    setAndroidVoipMode(false, 0);
}

void AudioWorker::setAndroidVoipMode(bool enable, int audioSessionId)
{
#ifdef Q_OS_ANDROID
    qDebug() << "@@@ [JNI] setAndroidVoipMode init. Enable:" << enable << "SessionID:" << audioSessionId;
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

    int mode = enable ? 3 : 0;
    audioManager.callMethod<void>("setMode", "(I)V", mode);

    if (enable) {
        // Жестко принуждаем Android вещать звук через громкий СПИКЕРФОН, игнорируя датчик уха
        audioManager.callMethod<void>("setSpeakerphoneOn", "(Z)V", true);

        jboolean aecAvailable = QJniObject::callStaticMethod<jboolean>(
            "android/media/audiofx/AcousticEchoCanceler", "isAvailable");
        qDebug() << "@@@ [JNI] Hardware AcousticEchoCanceler available:" << aecAvailable;

        if (aecAvailable && audioSessionId > 0) {
            QJniObject aecObject = QJniObject::callStaticObjectMethod(
                "android/media/audiofx/AcousticEchoCanceler",
                "create",
                "(I)Landroid/media/audiofx/AcousticEchoCanceler;",
                audioSessionId
                );

            if (aecObject.isValid()) {
                jint result = aecObject.callMethod<jint>("setEnabled", "(Z)I", true);
                qDebug() << "@@@ [JNI] Hardware AEC binding result:" << (result == 0 ? "SUCCESS" : "ERROR");
            }
        }
    }
    qDebug() << "@@@ [JNI] setAndroidVoipMode finished successfully";
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
        int dataSize = compressedChunk.size();
        speakerDataCount++;
        wasSound = true;

        m_udpReceiver->readDatagram(compressedChunk.data(), dataSize, &senderAddress, &senderPort);
        if (compressedChunk.isEmpty()) continue;

        if (m_isEchoTestMode && m_udpSender) {
            m_udpSender->writeDatagram(compressedChunk, senderAddress, 28002);
            continue;
        }

        QByteArray pcmChunk;
        // Даем Opus декодеру максимальный запас по памяти (960 семплов), чтобы избежать обрезки данных
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
                // Корректно подрезаем буфер под РЕАЛЬНО декодированное количество семплов
                pcmChunk.resize(decodedSamples * sizeof(short));

                if (m_writeToFile && m_unixFileReceiverNet.isOpen()) {
                    m_unixFileReceiverNet.write(pcmChunk);
                    m_unixSizeReceiverNet += pcmChunk.size();
                }
            } else {
                continue;
            }
        } else {
            continue;
        }

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

        applyFilters(reinterpret_cast<short*>(pcmChunk.data()), pcmChunk.size() / 2);

        if (m_writeToFile && m_unixFileReceiverOut.isOpen()) {
            m_unixFileReceiverOut.write(pcmChunk);
            m_unixSizeReceiverOut += pcmChunk.size();
        }

        {
            QMutexLocker locker(&m_bufferMutex);
            m_jitterBuffer.enqueue(pcmChunk);

            if (m_jitterBuffer.size() > 15) {
                m_jitterBuffer.dequeue();
            }
        }
    }
}
#include "audioengine.h"
#include <QDataStream>
#include <QDebug>
#include <QDateTime>
#include <opus.h>

// ============================================================================
// ОСТАВШАЯСЯ ЛОГИКА WORKER (ВЫПОЛНЯЕТСЯ СТРОГО В КОНТЕКСТЕ QTHREAD)
// ============================================================================

void AudioWorker::initAudio(const QString &targetIp)
{
    m_targetIp = targetIp;

    // Инициализируем сокеты внутри потока
    if (!m_udpReceiver) {
        m_udpReceiver = new QUdpSocket(this);
        m_udpReceiver->bind(QHostAddress::AnyIPv4, 28002);
        connect(m_udpReceiver, &QUdpSocket::readyRead, this, &AudioWorker::onReadyReadUdp);
    }
    if (!m_udpSender) {
        m_udpSender = new QUdpSocket(this);
    }

    // Задаем нативный аудиоформат (16000 Гц как в вашем исходнике)
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    // Конфигурируем динамик
    m_audioSink = new QAudioSink(QMediaDevices::defaultAudioOutput(), format, this);
    m_audioSink->setVolume(static_cast<float>(m_speakerLevel) / 100.0f);
    m_audioSink->setBufferSize(35280);

    // Конфигурируем микрофон
    m_audioSource = new QAudioSource(QMediaDevices::defaultAudioInput(), format, this);

    // Создаем кодеки Opus
    initOpus();

    // Создаем системный таймер для плавной выборки из Jitter-буфера (шаг 20 мс)
    if (!m_playbackTimer) {
        m_playbackTimer = new QTimer(this);
        connect(m_playbackTimer, &QTimer::timeout, this, &AudioWorker::processPlayback);
    }
}

void AudioWorker::startAudio()
{
    qDebug() << "@@@ [Worker] Starting audio streams";

    // Первичный перевод Android в режим связи для замера сессии
    setAndroidVoipMode(true, 0);

    // Запуск устройств
    m_audioOutputDevice = m_audioSink->start();
    m_audioInputDevice = m_audioSource->start();

    if (!m_audioInputDevice) {
        qWarning() << "@@@ [Worker] Failed to open audio input device!";
        return;
    }

    // Вытаскиваем сгенерированный Android Session ID и принудительно цепляем AEC
    int sessionId = 0;
#ifdef Q_OS_ANDROID
    sessionId = m_audioSource->property("audioSessionId").toInt();
#endif
    setAndroidVoipMode(true, sessionId);

    // Запускаем таймер воспроизведения (интервал 20 мс равен одному фрейму Opus)
    m_playbackTimer->start(20);

    // Привязываем обработчик захвата микрофона
    connect(m_audioInputDevice, &QIODevice::readyRead, this, &AudioWorker::onMicDataReady);
}

void AudioWorker::onMicDataReady()
{
    if (!m_audioInputDevice || m_isMuted) return;

    QByteArray data = m_audioInputDevice->readAll();
    if (data.isEmpty()) return;

    // 1. Отладочный файл: Чистый захват с микрофона
    if (m_writeToFile && m_unixFileSenderIn.isOpen()) {
        m_unixFileSenderIn.write(data);
        m_unixSizeSenderIn += data.size();
    }

    // Накапливаем данные (размер одного кадра для 16кГц 20мс — это 640 байт)
    static QByteArray micAccumulator;
    micAccumulator.append(data);

    while (micAccumulator.size() >= 640) {
        QByteArray chunk = micAccumulator.left(640);
        micAccumulator.remove(0, 640);

        // Расчет уровня громкости микрофона для UI
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

        // --- ДИНАМИЧЕСКИЙ ЭХО-ДЕМПИНГ ---
        bool isPartnerSpeaking = false;
        {
            QMutexLocker locker(&m_bufferMutex);
            isPartnerSpeaking = !m_jitterBuffer.isEmpty();
        }
        if (isPartnerSpeaking) {
            for (int i = 0; i < len / 2; ++i) {
                pcmPtr[i] = static_cast<short>(pcmPtr[i] * 0.2f); // Ослабляем микрофон, когда идет звук из сети
            }
        }

        // 2. Отладочный файл: Обработанный микрофон перед отправкой в сеть/кодек
        if (m_writeToFile && m_unixFileSenderNet.isOpen()) {
            m_unixFileSenderNet.write(chunk);
            m_unixSizeSenderNet += chunk.size();
        }

        // Кодируем и отправляем по UDP
        if (m_udpSender && m_opusEncoder) {
            unsigned char compressedData[256];
            int compressedBytes = opus_encode(
                reinterpret_cast<OpusEncoder*>(m_opusEncoder),
                reinterpret_cast<const opus_int16*>(chunk.constData()),
                320, // 320 сэмплов = 640 байт
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

void AudioWorker::setWriteToFile(bool enable)
{
    m_writeToFile = enable;

    if (m_writeToFile) {
        // Получаем путь к безопасной Music-песочнице на Android или Windows
        QString musicDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        QDir().mkpath(musicDir);
        if (!musicDir.endsWith('/')) musicDir += '/';

        m_unixSizeSenderIn = 0; m_unixSizeSenderNet = 0;
        m_unixSizeReceiverNet = 0; m_unixSizeReceiverOut = 0;

        QByteArray dummyHeader(44, 0);

        m_unixFileSenderIn.setFileName(musicDir + "senderIn.wav");
        if (m_unixFileSenderIn.open(QIODevice::WriteOnly | QIODevice::Truncate)) m_unixFileSenderIn.write(dummyHeader);

        m_unixFileSenderNet.setFileName(musicDir + "senderNet.wav");
        if (m_unixFileSenderNet.open(QIODevice::WriteOnly | QIODevice::Truncate)) m_unixFileSenderNet.write(dummyHeader);

        m_unixFileReceiverNet.setFileName(musicDir + "receiverNet.wav");
        if (m_unixFileReceiverNet.open(QIODevice::WriteOnly | QIODevice::Truncate)) m_unixFileReceiverNet.write(dummyHeader);

        m_unixFileReceiverOut.setFileName(musicDir + "receiverOut.wav");
        if (m_unixFileReceiverOut.open(QIODevice::WriteOnly | QIODevice::Truncate)) m_unixFileReceiverOut.write(dummyHeader);

        qDebug() << "@@@ [Worker] Отладочные WAV файлы открыты по пути:" << musicDir;
    } else {
        // Закрываем файлы с перезаписью заголовков
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
    out << static_cast<quint32>(16);    // Размер структуры fmt
    out << static_cast<quint16>(1);     // PCM формат
    out << static_cast<quint16>(1);     // 1 канал (Моно)
    out << static_cast<quint32>(16000); // Sample Rate
    out << static_cast<quint32>(32000); // Byte Rate (16000 * 1 канал * 2 байта)
    out << static_cast<quint16>(2);     // Block Align
    out << static_cast<quint16>(16);    // Bits per sample
    out.writeRawData("data", 4);
    out << static_cast<quint32>(dataSize);
}

void AudioWorker::applyFilters(short *samples, int count)
{
    // Фильтр нижних частот Баттерворта 2-го порядка (срез ~3.8 КГц при 16кГц)
    const float b0 = 0.3012f;
    const float b1 = 0.6025f;
    const float b2 = 0.3012f;
    const float a1 = -0.4908f;
    const float a2 = 0.6958f;

    for (int i = 0; i < count; ++i) {
        float x0 = static_cast<float>(samples[i]);
        float y0 = (b0 * x0) + (b1 * m_x1) + (b2 * m_x2) - (a1 * m_y1) - (a2 * m_y2);

        m_x2 = m_x1;
        m_x1 = x0;
        m_y2 = m_y1;
        m_y1 = y0;

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
void AudioWorker::setMicVolume(int level) { m_micLevel = level; }
// Было с ошибкой: static_cast(level)
void AudioWorker::setSpeakerVolume(int level) { m_speakerLevel = level; if (m_audioSink) m_audioSink->setVolume(static_cast<float>(level) / 100.0f); }
void AudioEngine::setWriteToFile(bool enable)
{
    if (enable) startWriteToFile();
    else stopWriteToFile();
}
