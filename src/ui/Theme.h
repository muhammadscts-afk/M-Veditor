#pragma once
#include <QColor>
namespace mv {
struct Theme {
    static void apply(bool dark);
    static bool isDark();
    static QColor bg(), panel(), trackA(), trackB(), text(), dim(), grid(), accent();
    static QColor clipColor(const QString &kind); // video|audio|music|voice|title|image
};
} // namespace mv
