#include "AppSettings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace mv {

bool AppSettings::portable() { return QFile::exists(QCoreApplication::applicationDirPath() + "/portable.flag"); }

QString AppSettings::dataDir() {
    QString d = portable() ? QCoreApplication::applicationDirPath() + "/data"
                           : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(d);
    return d;
}
QString AppSettings::recoveryDir() { QString d = dataDir() + "/recovery"; QDir().mkpath(d); return d; }

QSettings &AppSettings::s() {
    static QSettings *inst = portable() ? new QSettings(dataDir() + "/settings.ini", QSettings::IniFormat)
                                        : new QSettings(QSettings::IniFormat, QSettings::UserScope, "MVeditor", "M Veditor");
    return *inst;
}

bool AppSettings::darkTheme() { return s().value("ui/dark", true).toBool(); }
void AppSettings::setDarkTheme(bool v) { s().setValue("ui/dark", v); }
int AppSettings::autosaveMinutes() { return s().value("project/autosaveMinutes", 2).toInt(); }
void AppSettings::setAutosaveMinutes(int v) { s().setValue("project/autosaveMinutes", v); }
QString AppSettings::exportDir() {
    return s().value("export/dir", QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).toString();
}
void AppSettings::setExportDir(const QString &v) { s().setValue("export/dir", v); }
QString AppSettings::importDir() {
    return s().value("import/dir", QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).toString();
}
void AppSettings::setImportDir(const QString &v) { s().setValue("import/dir", v); }
QString AppSettings::hwaccel() { return s().value("export/hw", "none").toString(); }
void AppSettings::setHwaccel(const QString &v) { s().setValue("export/hw", v); }
bool AppSettings::useProxies() { return s().value("preview/proxies", true).toBool(); }
void AppSettings::setUseProxies(bool v) { s().setValue("preview/proxies", v); }
bool AppSettings::snapping() { return s().value("timeline/snap", true).toBool(); }
void AppSettings::setSnapping(bool v) { s().setValue("timeline/snap", v); }
QString AppSettings::cacheDir() { return s().value("cache/dir", dataDir() + "/cache").toString(); }
void AppSettings::setCacheDir(const QString &v) { s().setValue("cache/dir", v); }
QString AppSettings::ffmpegDir() { return s().value("ffmpeg/dir").toString(); }
void AppSettings::setFfmpegDir(const QString &v) { s().setValue("ffmpeg/dir", v); }
bool AppSettings::restoreSession() { return s().value("project/restore", false).toBool(); }
void AppSettings::setRestoreSession(bool v) { s().setValue("project/restore", v); }
QStringList AppSettings::recents() { return s().value("project/recent").toStringList(); }
void AppSettings::addRecent(const QString &p) {
    QStringList l = recents(); l.removeAll(p); l.prepend(p);
    while (l.size() > 10) l.removeLast();
    s().setValue("project/recent", l);
}
void AppSettings::removeRecent(const QString &p) { QStringList l = recents(); l.removeAll(p); s().setValue("project/recent", l); }

void AppSettings::applyStartupWithWindows(bool on) {
#ifdef Q_OS_WIN
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (on) run.setValue("M Veditor", "\"" + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + "\"");
    else run.remove("M Veditor");
#else
    Q_UNUSED(on);
#endif
}
bool AppSettings::startupWithWindows() {
#ifdef Q_OS_WIN
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    return run.contains("M Veditor");
#else
    return false;
#endif
}
} // namespace mv
