// Waveform peaks, silence detection, thumbnails: small background helpers built on FFmpeg.
#pragma once
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QVector>
#include <QPair>

namespace mv {

class WaveformCache {
public:
    static constexpr int kPeaksPerSecond = 100;
    static QString cacheDir();
    static void setCacheDir(const QString &dir);
    static QString keyFor(const QString &path);
    static bool load(const QString &path, QVector<float> *peaks);
    static void store(const QString &path, const QVector<float> &peaks);
    static qint64 clearAll();   // returns bytes freed (thumbnails + waveforms + proxies)
    static qint64 usedBytes();
};

class WaveformLoader : public QObject {
    Q_OBJECT
public:
    explicit WaveformLoader(QObject *parent = nullptr) : QObject(parent) {}
    void start(const QString &path);
signals:
    void ready(const QString &path, const QVector<float> &peaks);
    void failed(const QString &path);
private:
    QProcess *m_proc = nullptr;
    QString m_path;
    QByteArray m_pending;
    QVector<float> m_peaks;
    float m_cur = 0; int m_count = 0;
};

struct TimeRange { double start = 0, end = 0; };

class SilenceDetector : public QObject {
    Q_OBJECT
public:
    explicit SilenceDetector(QObject *parent = nullptr) : QObject(parent) {}
    void start(const QString &path, double thresholdDb, double minSeconds, double totalDuration);
    static QVector<TimeRange> parseSilences(const QString &stderrText, double totalDuration);
    static QVector<TimeRange> invert(const QVector<TimeRange> &silences, double totalDuration, double minKeep = 0.05);
signals:
    void finished(const QVector<mv::TimeRange> &nonSilent, bool ok);
private:
    QProcess *m_proc = nullptr;
    double m_total = 0;
};

class ThumbnailGenerator : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailGenerator(QObject *parent = nullptr) : QObject(parent) {}
    void request(const QString &mediaId, const QString &path, double atSeconds, bool isImage);
signals:
    void ready(const QString &mediaId, const QString &thumbPath);
private:
    void next();
    struct Job { QString id, path; double at; bool image; };
    QVector<Job> m_queue;
    QProcess *m_proc = nullptr;
};

} // namespace mv
Q_DECLARE_METATYPE(QVector<float>)
Q_DECLARE_METATYPE(QVector<mv::TimeRange>)
