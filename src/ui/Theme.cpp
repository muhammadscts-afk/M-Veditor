#include "Theme.h"
#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace mv {
static bool g_dark = true;
bool Theme::isDark() { return g_dark; }

void Theme::apply(bool dark) {
    g_dark = dark;
    qApp->setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    if (dark) {
        p.setColor(QPalette::Window, QColor(37, 38, 42));
        p.setColor(QPalette::WindowText, QColor(228, 229, 232));
        p.setColor(QPalette::Base, QColor(27, 28, 31));
        p.setColor(QPalette::AlternateBase, QColor(44, 45, 50));
        p.setColor(QPalette::ToolTipBase, QColor(52, 53, 58));
        p.setColor(QPalette::ToolTipText, QColor(235, 235, 238));
        p.setColor(QPalette::Text, QColor(228, 229, 232));
        p.setColor(QPalette::Button, QColor(52, 54, 60));
        p.setColor(QPalette::ButtonText, QColor(228, 229, 232));
        p.setColor(QPalette::BrightText, Qt::red);
        p.setColor(QPalette::Highlight, QColor(66, 133, 244));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::PlaceholderText, QColor(130, 132, 140));
        p.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 122, 128));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 122, 128));
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 122, 128));
    } else {
        p.setColor(QPalette::Window, QColor(240, 241, 244));
        p.setColor(QPalette::WindowText, QColor(30, 32, 36));
        p.setColor(QPalette::Base, Qt::white);
        p.setColor(QPalette::AlternateBase, QColor(232, 234, 238));
        p.setColor(QPalette::ToolTipBase, QColor(255, 255, 230));
        p.setColor(QPalette::ToolTipText, Qt::black);
        p.setColor(QPalette::Text, QColor(30, 32, 36));
        p.setColor(QPalette::Button, QColor(228, 230, 235));
        p.setColor(QPalette::ButtonText, QColor(30, 32, 36));
        p.setColor(QPalette::BrightText, Qt::red);
        p.setColor(QPalette::Highlight, QColor(25, 103, 210));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Disabled, QPalette::Text, QColor(150, 152, 158));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(150, 152, 158));
    }
    qApp->setPalette(p);
    qApp->setStyleSheet(
        "QToolTip { padding: 4px; border: 1px solid #777; }"
        "QDockWidget::title { padding: 5px; font-weight: bold; }"
        "QToolButton { padding: 4px; }"
        "QPushButton#primary { background: #2d7ff9; color: white; font-weight: bold; padding: 7px 18px; border-radius: 4px; }"
        "QPushButton#primary:disabled { background: #5c6b82; color: #ccd; }"
        "QPushButton#primary:hover { background: #4a93ff; }"
        "QGroupBox { margin-top: 10px; font-weight: bold; } QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }");
}

QColor Theme::bg() { return g_dark ? QColor(24, 25, 28) : QColor(226, 228, 232); }
QColor Theme::panel() { return g_dark ? QColor(37, 38, 42) : QColor(240, 241, 244); }
QColor Theme::trackA() { return g_dark ? QColor(32, 33, 37) : QColor(236, 238, 242); }
QColor Theme::trackB() { return g_dark ? QColor(28, 29, 33) : QColor(228, 230, 235); }
QColor Theme::text() { return g_dark ? QColor(228, 229, 232) : QColor(30, 32, 36); }
QColor Theme::dim() { return g_dark ? QColor(140, 142, 150) : QColor(100, 104, 112); }
QColor Theme::grid() { return g_dark ? QColor(70, 72, 80) : QColor(185, 188, 196); }
QColor Theme::accent() { return QColor(255, 71, 87); }
QColor Theme::clipColor(const QString &k) {
    if (k == "audio") return QColor(46, 160, 110);
    if (k == "music") return QColor(0, 150, 160);
    if (k == "voice") return QColor(224, 140, 40);
    if (k == "title") return QColor(150, 90, 200);
    if (k == "image") return QColor(90, 150, 220);
    return QColor(52, 112, 200);
}
} // namespace mv
