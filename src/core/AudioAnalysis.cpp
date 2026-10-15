#include "AudioAnalysis.h"
#include "FfmpegTools.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

namespace mv {

static QString g_cacheDir;
QString WaveformCache::cacheDir() {
    if (g_cacheDir.isEmpty()) g_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (g_cacheDir.isEmpty()) g_cacheDir = QDir::tempPath() + "/mveditor-cache";
    QDir().mkpath(g_cacheDir);
    return g_cacheDir;
}
void WaveformCache::setCacheDir(const QString &dir) { g_cacheDir = dir; }
QString WaveformCache::keyFor(const QString &path) {
    QFileInfo fi(path);
    const QByteArray src = (fi.absoluteFilePath() + "|" + QString::number(fi.size()) + "|" + QString::number(fi.lastModified().toSecsSinceEpoch())).toUtf8();
    return QCryptographicHash::hash(src, QCryptographicHash::Sha1).toHex().left(20);
}
bool WaveformCache::load(const QString &path, QVector<float> *peaks) {
    QFile f(cacheDir() + "/wave_" + keyFor(path) + ".peaks");
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray d = f.readAll();
    peaks->resize(d.size() / int(sizeof(float)));
    memcpy(peaks->data(), d.constData(), size_t(peaks->size()) * sizeof(float));
    return !peaks->isEmpty();
}
void WaveformCache::store(const QString &path, const QVector<float> &peaks) {
    QFile f(cacheDir() + "/wave_" + keyFor(path) + ".peaks");
    if (f.open(QIODevice::WriteOnly)) f.write(reinterpret_cast<const char *>(peaks.constData()), qint64(peaks.size()) * qint64(sizeof(float)));
}
qint64 WaveformCache::usedBytes() {
    qint64 n = 0;
    QDirIterator it(cacheDir(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); n += it.fileInfo().size(); }
    return n;
}
qint64 WaveformCache::clearAll() {
    const qint64 n = usedBytes();
    QDir d(cacheDir());
    for (auto &e : d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
        e.isDir() ? QDir(e.absoluteFilePath()).removeRecursively() : QFile::remove(e.absoluteFilePath());
    return n;
}

// ------------------------------------------------------------- waveform
void WaveformLoader::start(const QString &path) {
    m_path = path;
    if (WaveformCache::load(path, &m_peaks)) { emit ready(path, m_peaks); return; }
    if (!tools::available()) { emit failed(path); return; }
    m_peaks.clear(); m_pending.clear(); m_cur = 0; m_count = 0;
    m_proc = new QProcess(this);
    tools::quiet(m_proc);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this] {
        m_pending += m_proc->readAllStandardOutput();
        const int n = m_pending.size() / 2;
        const qint16 *s = reinterpret_cast<const qint16 *>(m_pending.constData());
        const int per = 8000 / WaveformCache::kPeaksPerSecond;
        for (int i = 0; i < n; ++i) {
            m_cur = std::max(m_cur, std::fabs(s[i] / 32768.0f));
            if (++m_count == per) { m_peaks.push_back(m_cur); m_cur = 0; m_count = 0; }
        }
        m_pending.remove(0, n * 2);
    });
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (m_count > 0) m_peaks.push_back(m_cur);
        if (code != 0 || m_peaks.isEmpty()) { emit failed(m_path); return; }
        WaveformCache::store(m_path, m_peaks);
        emit ready(m_path, m_peaks);
    });
    m_proc->start(tools::ffmpegPath(), {"-v", "error", "-i", QFileInfo(path).absoluteFilePath(), "-vn", "-ac", "1", "-ar", "8000", "-f", "s16le", "-"});
}

// ------------------------------------------------------------- silence
QVector<TimeRange> SilenceDetector::parseSilences(const QString &text, double total) {
    static const QRegularExpression rs(R"(silence_start:\s*(-?[\d.]+))"), re(R"(silence_end:\s*(-?[\d.]+))");
    QVector<TimeRange> v;
    double curStart = -1;
    for (const auto &line : text.split('\n')) {
        auto ms = rs.match(line);
        if (ms.hasMatch()) { curStart = std::max(0.0, ms.captured(1).toDouble()); continue; }
        auto me = re.match(line);
        if (me.hasMatch() && curStart >= 0) { v.push_back({curStart, me.captured(1).toDouble()}); curStart = -1; }
    }
    if (curStart >= 0) v.push_back({curStart, total});   // silence runs to the end
    return v;
}
QVector<TimeRange> SilenceDetector::invert(const QVector<TimeRange> &sil, double total, double minKeep) {
    QVector<TimeRange> out;
    double pos = 0;
    for (auto &s : sil) { if (s.start - pos > minKeep) out.push_back({pos, s.start}); pos = std::max(pos, s.end); }
    if (total - pos > minKeep) out.push_back({pos, total});
    return out;
}
void SilenceDetector::start(const QString &path, double db, double minSec, double total) {
    m_total = total;
    m_proc = new QProcess(this);
    tools::quiet(m_proc);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        const QString text = QString::fromUtf8(m_proc->readAll());
        emit finished(invert(parseSilences(text, m_total), m_total), code == 0);
    });
    m_proc->start(tools::ffmpegPath(), {"-hide_banner", "-nostats", "-i", QFileInfo(path).absoluteFilePath(), "-vn", "-af",
                  QString("silencedetect=noise=%1dB:d=%2").arg(QString::number(db, 'f', 1), QString::number(minSec, 'f', 3)), "-f", "null", "-"});
}

// ------------------------------------------------------------- thumbnails
void ThumbnailGenerator::request(const QString &id, const QString &path, double at, bool image) {
    m_queue.push_back({id, path, at, image});
    if (!m_proc) next();
}
void ThumbnailGenerator::next() {
    if (m_queue.isEmpty()) return;
    const Job j = m_queue.takeFirst();
    QDir().mkpath(WaveformCache::cacheDir() + "/thumbs");
    const QString out = WaveformCache::cacheDir() + "/thumbs/" + WaveformCache::keyFor(j.path) + ".jpg";
    if (QFileInfo::exists(out)) { emit ready(j.id, out); next(); return; }
    m_proc = new QProcess(this);
    tools::quiet(m_proc);
    connect(m_proc, &QProcess::finished, this, [this, j, out](int code, QProcess::ExitStatus) {
        m_proc->deleteLater(); m_proc = nullptr;
        if (code == 0 && QFileInfo::exists(out)) emit ready(j.id, out);
        next();
    });
    QStringList a = {"-v", "error", "-y"};
    if (!j.image) a << "-ss" << QString::number(std::max(0.0, j.at), 'f', 2);
    a << "-i" << QFileInfo(j.path).absoluteFilePath() << "-frames:v" << "1" << "-vf" << "scale=240:-2" << out;
    m_proc->start(tools::ffmpegPath(), a);
}

} // namespace mv
