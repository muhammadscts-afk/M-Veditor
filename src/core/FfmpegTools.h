// Locates the bundled FFmpeg/FFprobe executables and queries their capabilities.
#pragma once
#include <QString>
#include <QStringList>

class QProcess;
namespace mv::tools {

// Windows: stop console windows from flashing when the GUI app starts ffmpeg/ffprobe. No-op elsewhere.
void quiet(QProcess *p);

void setOverrideDir(const QString &dir);        // user setting / tests
QString ffmpegPath();                           // empty if not found
QString ffprobePath();
bool available(QString *whyNot = nullptr);
QString versionString();
int majorVersion();                             // 0 when unknown (git builds are treated as new)
bool hasEncoder(const QString &name);
bool hasFilter(const QString &name);
bool amixHasNormalize();
// Runs a tool with an argument list (never through a shell). Returns stdout (+stderr if requested).
QString capture(const QString &program, const QStringList &args, int timeoutMs = 15000, bool includeStderr = false);

} // namespace mv::tools
