#pragma once
#include "ProjectModel.h"
#include "RenderGraph.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

namespace mv {

class ExportPanel : public QWidget {
    Q_OBJECT
public:
    explicit ExportPanel(ProjectModel *model, QWidget *parent = nullptr);
    ExportOptions currentOptions() const;      // outputPath filled from folder + file name
    void selectFormat(const QString &id);
    void setBusy(bool busy);
    void setProgress(double fraction, const QString &text);
    static QString uniquePath(const QString &path);
signals:
    void exportRequested();
    void cancelRequested();
private slots:
    void updateReencodeInfo();
    void onFormatChanged();
private:
    ProjectModel *m;
    QComboBox *m_format, *m_res, *m_fps, *m_quality, *m_abr, *m_sr, *m_ch, *m_hw;
    QSpinBox *m_vbr;
    QLineEdit *m_dir, *m_name;
    QCheckBox *m_copy;
    QLabel *m_info, *m_status;
    QPushButton *m_go, *m_cancel;
    QProgressBar *m_bar;
    QWidget *m_videoBox;
};

} // namespace mv
