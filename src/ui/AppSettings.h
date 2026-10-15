// Local-only preferences. Portable mode: a "portable.flag" file next to the exe keeps everything in ./data.
#pragma once
#include <QSettings>
#include <QString>
#include <QStringList>

namespace mv {
class AppSettings {
public:
    static bool portable();
    static QString dataDir();          // settings / recovery / cache root
    static QString recoveryDir();
    static QSettings &s();

    static bool darkTheme();            static void setDarkTheme(bool);
    static int autosaveMinutes();       static void setAutosaveMinutes(int);
    static QString exportDir();         static void setExportDir(const QString &);
    static QString importDir();         static void setImportDir(const QString &);
    static QString hwaccel();           static void setHwaccel(const QString &);
    static bool useProxies();           static void setUseProxies(bool);
    static bool snapping();             static void setSnapping(bool);
    static QString cacheDir();          static void setCacheDir(const QString &);
    static QString ffmpegDir();         static void setFfmpegDir(const QString &);
    static bool restoreSession();       static void setRestoreSession(bool);
    static QStringList recents();       static void addRecent(const QString &path); static void removeRecent(const QString &path);
    static void applyStartupWithWindows(bool on);
    static bool startupWithWindows();
};
} // namespace mv
