#pragma once
#include "ProjectModel.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QWidget>
#include <functional>

namespace mv {

class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(ProjectModel *model, QWidget *parent = nullptr);
signals:
    void speedDialogRequested();
private:
    void refresh();
    using Getter = std::function<double(const Clip &)>;
    using Setter = std::function<void(Clip &, double)>;
    QDoubleSpinBox *addSpin(QFormLayout *f, const QString &label, double mn, double mx, double step, int dec, const QString &suffix,
                            double scale, Getter g, Setter s, const QString &tip = {}, bool multi = true);
    QCheckBox *addCheck(QFormLayout *f, const QString &label, std::function<bool(const Clip &)> g, std::function<void(Clip &, bool)> s, const QString &tip = {});
    void edit(const QString &what, const std::function<void(Clip &)> &fn);

    ProjectModel *m;
    QLabel *m_head, *m_dur, *m_speed, *m_name;
    QGroupBox *gClip, *gTrans, *gVideo, *gAudio, *gTitle;
    QVector<std::function<void(const Clip &)>> m_refresh;
    bool m_busy = false;
    QPlainTextEdit *m_text;
    QPushButton *m_color, *m_boxColor;
};

} // namespace mv
