#include "FfmpegTools.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace mv::tools {

void quiet(QProcess *p) {
#ifdef Q_OS_WIN
    p->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= 0x08000000; /* CREATE_NO_WINDOW */ });
#else
    Q_UNUSED(p);
#endif
}

static QMutex g_mutex;
static QString g_override;
static QString g_ffmpeg, g_ffprobe, g_version;
static bool g_located = false, g_encLoaded = false, g_filtLoaded = false;
static QSet<QString> g_encoders, g_filters;
static int g_amix = -1;

#ifdef Q_OS_WIN
static const char *kExe = ".exe";
#else
static const char *kExe = "";
#endif

static QString findTool(const QString &base) {
    const QString exe = base + kExe;
    QStringList dirs;
    if (!g_override.isEmpty()) dirs << g_override;
    const QString env = qEnvironmentVariable("MVEDITOR_FFMPEG_DIR");
    if (!env.isEmpty()) dirs << env;
    const QString app = QCoreApplication::applicationDirPath();
    dirs << app + "/ffmpeg" << app << app + "/bin";
    for (const auto &d : dirs) {
        QFileInfo fi(QDir(d).absoluteFilePath(exe));
        if (fi.exists() && fi.isFile()) return fi.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(base); // developer fallback only
}

static void locate() {
    if (g_located) return;
    g_ffmpeg = findTool("ffmpeg");
    g_ffprobe = findTool("ffprobe");
    g_located = true;
}

void setOverrideDir(const QString &dir) {
    QMutexLocker l(&g_mutex);
    g_override = dir; g_located = false; g_encLoaded = g_filtLoaded = false; g_version.clear(); g_amix = -1;
}
QString ffmpegPath() { QMutexLocker l(&g_mutex); locate(); return g_ffmpeg; }
QString ffprobePath() { QMutexLocker l(&g_mutex); locate(); return g_ffprobe; }

bool available(QString *why) {
    if (ffmpegPath().isEmpty() || ffprobePath().isEmpty()) {
        if (why) *why = QObject::tr("The FFmpeg components (ffmpeg and ffprobe) were not found next to the application. "
                                    "Please reinstall M Veditor.");
        return false;
    }
    return true;
}

QString capture(const QString &program, const QStringList &args, int timeoutMs, bool includeStderr) {
    QProcess p;
    quiet(&p);
    p.setProgram(program);
    p.setArguments(args);
    if (!includeStderr) p.setProcessChannelMode(QProcess::SeparateChannels);
    else p.setProcessChannelMode(QProcess::MergedChannels);
    p.start();
    if (!p.waitForFinished(timeoutMs)) { p.kill(); p.waitForFinished(2000); }
    return QString::fromUtf8(p.readAllStandardOutput());
}

QString versionString() {
    const QString ff = ffmpegPath();
    QMutexLocker l(&g_mutex);
    if (g_version.isEmpty() && !ff.isEmpty()) {
        const QString out = capture(ff, {"-hide_banner", "-version"});
        g_version = out.section('\n', 0, 0).trimmed();
    }
    return g_version;
}
int majorVersion() {
    static const QRegularExpression re(R"(version\s+n?(\d+)\.)");
    auto m = re.match(versionString());
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

static QSet<QString> parseList(const QString &text, int nameColumn) {
    QSet<QString> s;
    for (const auto &line : text.split('\n')) {
        const QStringList parts = line.simplified().split(' ');
        if (line.startsWith(' ') && parts.size() > nameColumn && parts[0].size() <= 6)
            s.insert(parts[nameColumn]);
    }
    return s;
}

bool hasEncoder(const QString &name) {
    const QString ff = ffmpegPath();
    QMutexLocker l(&g_mutex);
    if (!g_encLoaded && !ff.isEmpty()) {
        g_encoders = parseList(capture(ff, {"-hide_banner", "-encoders"}), 1);
        g_encLoaded = true;
    }
    return g_encoders.contains(name);
}
bool hasFilter(const QString &name) {
    const QString ff = ffmpegPath();
    QMutexLocker l(&g_mutex);
    if (!g_filtLoaded && !ff.isEmpty()) {
        g_filters = parseList(capture(ff, {"-hide_banner", "-filters"}), 1);
        g_filtLoaded = true;
    }
    return g_filters.contains(name);
}
bool amixHasNormalize() {
    const QString ff = ffmpegPath();
    QMutexLocker l(&g_mutex);
    if (g_amix < 0 && !ff.isEmpty())
        g_amix = capture(ff, {"-hide_banner", "-h", "filter=amix"}).contains("normalize") ? 1 : 0;
    return g_amix == 1;
}

} // namespace mv::tools
