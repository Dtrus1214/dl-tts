// TTS engine using sherpa-onnx C API (in-process, no Piper CLI).
// Requires: sherpa-onnx built with C API + TTS, include path and link to sherpa-onnx-c-api.
#include "ttsengine.h"
#include <QMediaPlayer>
#include <QMediaContent>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QTemporaryFile>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <vector>

#include "sherpa-onnx/c-api/c-api.h"
#include <cstring>

static QString runSynthesis(const QString &modelPath, const QString &tokensPath,
                            const QString &dataDir, const QString &text, float lengthScale)
{
    if (modelPath.isEmpty() || tokensPath.isEmpty() || dataDir.isEmpty() || text.trimmed().isEmpty())
        return QString();

    QByteArray modelPathUtf8 = modelPath.toUtf8();
    QByteArray tokensPathUtf8 = tokensPath.toUtf8();
    QByteArray dataDirUtf8 = dataDir.toUtf8();
    QByteArray textUtf8 = text.trimmed().toUtf8();

    SherpaOnnxOfflineTtsConfig config;
    memset(&config, 0, sizeof(config));
    config.model.vits.model = modelPathUtf8.constData();
    config.model.vits.tokens = tokensPathUtf8.constData();
    config.model.vits.data_dir = dataDirUtf8.constData();
    config.model.vits.noise_scale = 0.667f;
    config.model.vits.noise_scale_w = 0.8f;
    config.model.vits.length_scale = lengthScale;
    config.model.num_threads = 1;
    config.model.debug = 0;

    const SherpaOnnxOfflineTts *tts = SherpaOnnxCreateOfflineTts(&config);
    if (!tts) {
        return QString();
    }

    const SherpaOnnxGeneratedAudio *audio = SherpaOnnxOfflineTtsGenerate(
        tts, textUtf8.constData(), 0, 1.0f);
    if (!audio || !audio->samples || audio->n == 0) {
        SherpaOnnxDestroyOfflineTts(tts);
        return QString();
    }

    QTemporaryFile tmp;
    tmp.setFileTemplate(QDir::temp().filePath(QStringLiteral("crystaltts_XXXXXX.wav")));
    tmp.setAutoRemove(false);
    if (!tmp.open()) {
        SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        SherpaOnnxDestroyOfflineTts(tts);
        return QString();
    }
    QString wavPath = tmp.fileName();
    tmp.close();

    SherpaOnnxWriteWave(audio->samples, audio->n, audio->sample_rate, wavPath.toUtf8().constData());

    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    SherpaOnnxDestroyOfflineTts(tts);

    return wavPath;
}

static bool runSynthesisToPath(const QString &modelPath, const QString &tokensPath,
                               const QString &dataDir, const QString &text,
                               const QString &wavPath, float lengthScale)
{
    if (wavPath.trimmed().isEmpty())
        return false;

    if (modelPath.isEmpty() || tokensPath.isEmpty() || dataDir.isEmpty() || text.trimmed().isEmpty())
        return false;

    QByteArray modelPathUtf8 = modelPath.toUtf8();
    QByteArray tokensPathUtf8 = tokensPath.toUtf8();
    QByteArray dataDirUtf8 = dataDir.toUtf8();
    QByteArray textUtf8 = text.trimmed().toUtf8();

    SherpaOnnxOfflineTtsConfig config;
    memset(&config, 0, sizeof(config));
    config.model.vits.model = modelPathUtf8.constData();
    config.model.vits.tokens = tokensPathUtf8.constData();
    config.model.vits.data_dir = dataDirUtf8.constData();
    config.model.vits.noise_scale = 0.667f;
    config.model.vits.noise_scale_w = 0.8f;
    config.model.vits.length_scale = lengthScale;
    config.model.num_threads = 1;
    config.model.debug = 0;

    const SherpaOnnxOfflineTts *tts = SherpaOnnxCreateOfflineTts(&config);
    if (!tts)
        return false;

    const SherpaOnnxGeneratedAudio *audio = SherpaOnnxOfflineTtsGenerate(
        tts, textUtf8.constData(), 0, 1.0f);
    if (!audio || !audio->samples || audio->n == 0) {
        SherpaOnnxDestroyOfflineTts(tts);
        return false;
    }

    SherpaOnnxWriteWave(audio->samples, audio->n, audio->sample_rate, wavPath.toUtf8().constData());

    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    SherpaOnnxDestroyOfflineTts(tts);
    return QFileInfo::exists(wavPath);
}

static bool runSynthesisChunksToPath(const QString &modelPath, const QString &tokensPath,
                                     const QString &dataDir, const QStringList &chunks,
                                     const QString &wavPath, int silenceMsBetweenChunks,
                                     float lengthScale)
{
    if (wavPath.trimmed().isEmpty())
        return false;
    if (modelPath.isEmpty() || tokensPath.isEmpty() || dataDir.isEmpty())
        return false;

    QStringList trimmed;
    trimmed.reserve(chunks.size());
    for (const QString &c : chunks) {
        const QString t = c.trimmed();
        if (!t.isEmpty())
            trimmed.append(t);
    }
    if (trimmed.isEmpty())
        return false;

    QByteArray modelPathUtf8 = modelPath.toUtf8();
    QByteArray tokensPathUtf8 = tokensPath.toUtf8();
    QByteArray dataDirUtf8 = dataDir.toUtf8();

    SherpaOnnxOfflineTtsConfig config;
    memset(&config, 0, sizeof(config));
    config.model.vits.model = modelPathUtf8.constData();
    config.model.vits.tokens = tokensPathUtf8.constData();
    config.model.vits.data_dir = dataDirUtf8.constData();
    config.model.vits.noise_scale = 0.667f;
    config.model.vits.noise_scale_w = 0.8f;
    config.model.vits.length_scale = lengthScale;
    config.model.num_threads = 1;
    config.model.debug = 0;

    const SherpaOnnxOfflineTts *tts = SherpaOnnxCreateOfflineTts(&config);
    if (!tts)
        return false;

    std::vector<float> all;
    int sampleRate = 0;

    const int silenceMs = qMax(0, silenceMsBetweenChunks);

    for (int i = 0; i < trimmed.size(); ++i) {
        const QByteArray textUtf8 = trimmed[i].toUtf8();
        const SherpaOnnxGeneratedAudio *audio = SherpaOnnxOfflineTtsGenerate(
            tts, textUtf8.constData(), 0, 1.0f);
        if (!audio || !audio->samples || audio->n == 0) {
            if (audio)
                SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
            SherpaOnnxDestroyOfflineTts(tts);
            return false;
        }

        if (sampleRate <= 0)
            sampleRate = audio->sample_rate;
        if (audio->sample_rate != sampleRate) {
            SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
            SherpaOnnxDestroyOfflineTts(tts);
            return false;
        }

        all.insert(all.end(), audio->samples, audio->samples + audio->n);
        SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);

        if (silenceMs > 0 && i + 1 < trimmed.size() && sampleRate > 0) {
            const int silenceSamples = qMax(0, (sampleRate * silenceMs) / 1000);
            all.insert(all.end(), static_cast<size_t>(silenceSamples), 0.0f);
        }
    }

    SherpaOnnxDestroyOfflineTts(tts);

    if (all.empty() || sampleRate <= 0)
        return false;

    SherpaOnnxWriteWave(all.data(), static_cast<int32_t>(all.size()), sampleRate, wavPath.toUtf8().constData());
    return QFileInfo::exists(wavPath);
}

TtsEngine::TtsEngine(QObject *parent)
    : QObject(parent)
{
    m_mediaPlayer = new QMediaPlayer(this);
    connect(m_mediaPlayer, &QMediaPlayer::stateChanged, this, [this](QMediaPlayer::State s) {
        onMediaStateChanged(static_cast<int>(s));
    });
    connect(m_mediaPlayer, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
        onMediaStatusChanged(static_cast<int>(s));
    });
    connect(this, &TtsEngine::synthesisFinished, this, &TtsEngine::onSynthesisFinished);
}

TtsEngine::~TtsEngine()
{
    stop();
    if (!m_currentWavPath.isEmpty() && QFile::exists(m_currentWavPath))
        QFile::remove(m_currentWavPath);
    m_available = false;
}

void TtsEngine::setModelPath(const QString &path)
{
    m_modelPath = path.trimmed();
}

void TtsEngine::setTokensPath(const QString &path)
{
    m_tokensPath = path.trimmed();
}

void TtsEngine::setDataDir(const QString &path)
{
    m_dataDir = path.trimmed();
}

QString TtsEngine::defaultModelPath() const
{
    return QCoreApplication::applicationDirPath() + QLatin1String("/tts-model/model.onnx");
}

QString TtsEngine::defaultTokensPath() const
{
    return QCoreApplication::applicationDirPath() + QLatin1String("/tts-model/tokens.txt");
}

QString TtsEngine::defaultDataDir() const
{
    return QCoreApplication::applicationDirPath() + QLatin1String("/tts-model/espeak-ng-data");
}

QString TtsEngine::synthesizeToWavPath(const QString &text, const QString &desiredWavPath) const
{
    QString out = desiredWavPath.trimmed();
    if (out.isEmpty())
        return QString();

    // Ensure .wav extension
    if (!out.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive))
        out += QLatin1String(".wav");

    QString model = m_modelPath.isEmpty() ? defaultModelPath() : m_modelPath;
    QString tokens = m_tokensPath.isEmpty() ? defaultTokensPath() : m_tokensPath;
    QString dataDir = m_dataDir.isEmpty() ? defaultDataDir() : m_dataDir;

    const float lengthScale = 100.0f / qBound(50, m_speedPercent, 200);
    if (runSynthesisToPath(model, tokens, dataDir, text, out, lengthScale))
        return out;
    return QString();
}

void TtsEngine::setSpeedPercent(int percent)
{
    m_speedPercent = qBound(50, percent, 200);
}

int TtsEngine::speedPercent() const
{
    return m_speedPercent;
}

bool TtsEngine::initialize()
{
    QString model = m_modelPath.isEmpty() ? defaultModelPath() : m_modelPath;
    QString tokens = m_tokensPath.isEmpty() ? defaultTokensPath() : m_tokensPath;
    QString dataDir = m_dataDir.isEmpty() ? defaultDataDir() : m_dataDir;

    QFileInfo dataInfo(dataDir);
    if (!QFileInfo::exists(model) || !QFileInfo::exists(tokens) || !dataInfo.isDir()) {
        m_available = false;
        setState(BackendError);
        return false;
    }

    m_available = true;
    if (m_state == BackendError)
        setState(Ready);
    return true;
}

bool TtsEngine::isAvailable() const
{
    return m_available;
}

void TtsEngine::speak(const QString &text)
{
    QString t = text.trimmed();
    if (t.isEmpty())
        return;
    if (!m_available && !initialize())
        return;
    if (m_state == Paused) {
        resume();
        return;
    }
    stop();

    QString model = m_modelPath.isEmpty() ? defaultModelPath() : m_modelPath;
    QString tokens = m_tokensPath.isEmpty() ? defaultTokensPath() : m_tokensPath;
    QString dataDir = m_dataDir.isEmpty() ? defaultDataDir() : m_dataDir;
    const float lengthScale = 100.0f / qBound(50, m_speedPercent, 200);

    setState(Loading);

    QFuture<QString> future = QtConcurrent::run(runSynthesis, model, tokens, dataDir, t, lengthScale);
    QFutureWatcher<QString> *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        QString path = watcher->result();
        watcher->deleteLater();
        emit synthesisFinished(path);
    });
    watcher->setFuture(future);
}

void TtsEngine::exportWav(const QString &text, const QString &wavPath)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        emit exportFinished(false, QString(), QStringLiteral("No text to export."));
        return;
    }
    if (!m_available && !initialize()) {
        emit exportFinished(false, QString(), QStringLiteral("TTS engine is not available."));
        return;
    }

    QFuture<QString> future = QtConcurrent::run([this, t, wavPath]() {
        return synthesizeToWavPath(t, wavPath);
    });
    QFutureWatcher<QString> *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        const QString out = watcher->result();
        watcher->deleteLater();
        if (out.isEmpty()) {
            emit exportFinished(false, QString(), QStringLiteral("Export failed."));
        } else {
            emit exportFinished(true, out, QString());
        }
    });
    watcher->setFuture(future);
}

void TtsEngine::exportWavChunks(const QStringList &chunks, const QString &wavPath, int silenceMsBetweenChunks)
{
    if (chunks.isEmpty()) {
        emit exportFinished(false, QString(), QStringLiteral("No text to export."));
        return;
    }
    if (!m_available && !initialize()) {
        emit exportFinished(false, QString(), QStringLiteral("TTS engine is not available."));
        return;
    }

    const QString desired = wavPath.trimmed();
    if (desired.isEmpty()) {
        emit exportFinished(false, QString(), QStringLiteral("Invalid output path."));
        return;
    }

    const int speedPercent = qBound(50, m_speedPercent, 200);
    QFuture<QString> future = QtConcurrent::run([this, chunks, desired, silenceMsBetweenChunks, speedPercent]() {
        QString out = desired;
        if (!out.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive))
            out += QLatin1String(".wav");

        const QString model = m_modelPath.isEmpty() ? defaultModelPath() : m_modelPath;
        const QString tokens = m_tokensPath.isEmpty() ? defaultTokensPath() : m_tokensPath;
        const QString dataDir = m_dataDir.isEmpty() ? defaultDataDir() : m_dataDir;
        const float lengthScale = 100.0f / speedPercent;

        if (runSynthesisChunksToPath(model, tokens, dataDir, chunks, out, silenceMsBetweenChunks, lengthScale))
            return out;
        return QString();
    });

    QFutureWatcher<QString> *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        const QString out = watcher->result();
        watcher->deleteLater();
        if (out.isEmpty()) {
            emit exportFinished(false, QString(), QStringLiteral("Export failed."));
        } else {
            emit exportFinished(true, out, QString());
        }
    });
    watcher->setFuture(future);
}

void TtsEngine::onSynthesisFinished(const QString &wavPath)
{
    if (m_state != Loading)
        return;
    if (wavPath.isEmpty() || !QFileInfo::exists(wavPath)) {
        setState(BackendError);
        return;
    }
    if (!m_currentWavPath.isEmpty() && QFile::exists(m_currentWavPath))
        QFile::remove(m_currentWavPath);
    m_currentWavPath = wavPath;

    m_mediaPlayer->setMedia(QMediaContent(QUrl::fromLocalFile(m_currentWavPath)));
    m_mediaPlayer->play();
    setState(Speaking);
}

void TtsEngine::pause()
{
    if (m_state != Speaking || !m_mediaPlayer)
        return;
    m_mediaPlayer->pause();
    setState(Paused);
}

void TtsEngine::resume()
{
    if (m_state != Paused || !m_mediaPlayer)
        return;
    m_mediaPlayer->play();
    setState(Speaking);
}

void TtsEngine::stop()
{
    if (m_mediaPlayer)
        m_mediaPlayer->stop();
    setState(Ready);
    if (!m_currentWavPath.isEmpty() && QFile::exists(m_currentWavPath)) {
        QFile::remove(m_currentWavPath);
        m_currentWavPath.clear();
    }
}

int TtsEngine::state() const
{
    return m_state;
}

void TtsEngine::setState(int state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}

void TtsEngine::onMediaStateChanged(int newState)
{
    switch (static_cast<QMediaPlayer::State>(newState)) {
    case QMediaPlayer::StoppedState:
        if (m_state == Speaking || m_state == Paused) {
            if (!m_currentWavPath.isEmpty() && QFile::exists(m_currentWavPath))
                QFile::remove(m_currentWavPath);
            m_currentWavPath.clear();
            setState(Ready);
        }
        break;
    case QMediaPlayer::PlayingState:
        setState(Speaking);
        break;
    case QMediaPlayer::PausedState:
        setState(Paused);
        break;
    }
}

void TtsEngine::onMediaStatusChanged(int status)
{
    if (static_cast<QMediaPlayer::MediaStatus>(status) == QMediaPlayer::InvalidMedia) {
        if (m_state == Speaking || m_state == Paused)
            setState(BackendError);
    }
}
