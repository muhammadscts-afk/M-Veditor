// Reads media metadata with ffprobe (blocking and asynchronous variants).
#pragma once
#include "Project.h"
#include <QObject>
#include <QProcess>

namespace mv {

struct ProbeResult {
    bool ok = false;
    QString error;           // beginner-friendly explanation when !ok
    MediaItem item;
    QByteArray rawJson;      // full ffprobe output, for the metadata inspector
};

ProbeResult parseProbeJson(const QByteArray &json, const QString &path);
ProbeResult probeBlocking(const QString &path, int timeoutMs = 30000);
bool isImagePath(const QString &path);
QString humanSize(qint64 bytes);
QString humanTime(double seconds, bool millis = false);

class ProbeTask : public QObject {
    Q_OBJECT
public:
    explicit ProbeTask(QObject *parent = nullptr) : QObject(parent) {}
    void start(const QString &path);
signals:
    void finished(const mv::ProbeResult &result);
private:
    QProcess *m_proc = nullptr;
    QString m_path;
};

} // namespace mv
Q_DECLARE_METATYPE(mv::ProbeResult)
