#include "FontResolver.h"
#include "FfmpegTools.h"
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSettings>
#include <QStandardPaths>

namespace mv {

#ifdef Q_OS_WIN
// Windows keeps "Arial Bold Italic (TrueType)" -> "arialbi.ttf" in the registry (system-wide and per-user fonts).
static QString lookupWindows(const QString &family, bool bold, bool italic) {
    static QHash<QString, QString> table;
    if (table.isEmpty()) {
        const QString fontsDir = qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts/";
        for (const char *hive : {"HKEY_LOCAL_MACHINE", "HKEY_CURRENT_USER"}) {
            QSettings reg(QString("%1\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts").arg(hive), QSettings::NativeFormat);
            for (const QString &k : reg.allKeys()) {
                QString name = k; name.replace('\\', '/');
                name = name.section('/', -1);
                name.remove(" (TrueType)").remove(" (OpenType)");
                QString file = reg.value(k).toString();
                if (file.isEmpty() || name.contains(" & ")) continue;
                if (QFileInfo(file).isRelative()) file = fontsDir + file;
                table.insert(name.toLower(), QDir::fromNativeSeparators(file));
            }
        }
    }
    const QString fam = family.toLower();
    QStringList tries;
    if (bold && italic) tries << fam + " bold italic";
    else if (bold) tries << fam + " bold";
    else if (italic) tries << fam + " italic";
    tries << fam << fam + " regular";
    for (const QString &t : tries) if (table.contains(t) && QFileInfo::exists(table[t])) return table[t];
    return {};
}
#else
static QString lookupFontconfig(const QString &family, bool bold, bool italic) {
    const QString fc = QStandardPaths::findExecutable("fc-match");
    if (fc.isEmpty()) return {};
    QString pattern = family;
    if (bold) pattern += ":bold";
    if (italic) pattern += ":italic";
    const QString out = tools::capture(fc, {"-f", "%{file}", pattern}, 5000).trimmed();
    return QFileInfo::exists(out) ? out : QString();
}
#endif

QString resolveFontFile(const QString &family, bool bold, bool italic) {
    if (family.trimmed().isEmpty()) return {};
#ifdef Q_OS_WIN
    return lookupWindows(family.trimmed(), bold, italic);
#else
    return lookupFontconfig(family.trimmed(), bold, italic);
#endif
}

} // namespace mv
