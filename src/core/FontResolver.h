// Finds the font FILE for a family + style (FFmpeg's drawtext needs a file path on Windows).
#pragma once
#include <QString>
namespace mv {
// Returns an absolute path to a .ttf/.otf/.ttc file, or an empty string if nothing suitable is installed.
QString resolveFontFile(const QString &family, bool bold, bool italic);
}
