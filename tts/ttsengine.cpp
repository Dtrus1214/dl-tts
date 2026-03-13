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

#include "sherpa-onnx/c-api/c-api.h"
#include <cstring>

static QString runSynthesis(const QString &modelPath, const QString &tokensPath,
                            const QString &dataDir, const QString &text)
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
    config.model.vits.length_scale = 1.0f;
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

    setState(Loading);

    QFuture<QString> future = QtConcurrent::run(runSynthesis, model, tokens, dataDir, t);
    QFutureWatcher<QString> *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        QString path = watcher->result();
        watcher->deleteLater();
        emit synthesisFinished(path);
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
