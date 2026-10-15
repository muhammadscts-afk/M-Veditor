// Runs one FFmpeg render in the background with progress, cancel, verification and safe output handling.
#pragma once
#include "Project.h"
#include "RenderGraph.h"
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <memory>

namespace mv {

class ExportJob : public QObject {
    Q_OBJECT
public:
    explicit ExportJob(QObject *parent = nullptr);
    ~ExportJob() override;

    // Validates and starts. Returns false (with a readable message) when the job can't start.
    bool start(const Project &project, const ExportOptions &options, QString *error);
    void cancel();
    bool isRunning() const { return m_running; }
    const QString &outputPath() const { return m_opts.outputPath; }

    // Checks the finished file with ffprobe. Returns an empty string when it is OK.
    static QString verifyOutput(const QString &path, const ExportOptions &o, double expectedDuration, bool expectVideo, bool expectAudio);
    static QString friendlyError(const QString &ffmpegStderr);

signals:
    void progress(double fraction, double etaSeconds, const QString &text);
    void finished(bool success, const QString &message, const QString &outputPath);
    void logLine(const QString &line);

private slots:
    void onStdout();
    void onStderr();
    void onFinished(int code, QProcess::ExitStatus st);
    void onProcError(QProcess::ProcessError e);

private:
    bool launch(bool allowHardware, QString *error);
    void finishWith(bool ok, const QString &msg);
    void cleanupPart();

    Project m_project;
    ExportOptions m_opts;
    RenderPlan m_plan;
    std::unique_ptr<QProcess> m_proc;
    std::unique_ptr<QTemporaryDir> m_tmp;
    QString m_part, m_buf, m_stderrTail;
    QElapsedTimer m_clock;
    bool m_running = false, m_cancelled = false, m_triedHw = false, m_retrying = false, m_done = false;
    double m_lastFraction = 0;
};

} // namespace mv
