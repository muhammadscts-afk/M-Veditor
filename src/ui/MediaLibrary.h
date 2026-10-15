#pragma once
#include "ProjectModel.h"
#include <QLineEdit>
#include <QListWidget>
#include <QWidget>

namespace mv {

class MediaList : public QListWidget {
    Q_OBJECT
public:
    using QListWidget::QListWidget;
protected:
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override;
    void startDrag(Qt::DropActions) override;
};

class MediaLibrary : public QWidget {
    Q_OBJECT
public:
    explicit MediaLibrary(ProjectModel *model, QWidget *parent = nullptr);
    QString currentMediaId() const;
signals:
    void importRequested();
    void removeRequested(const QString &mediaId);
    void renameRequested(const QString &mediaId);
    void previewRequested(const QString &mediaId);
    void addToTimelineRequested(const QString &mediaId);
    void contextMenuRequested(const QString &mediaId, const QPoint &globalPos);
    void filesDropped(const QStringList &paths);
protected:
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
private:
    void refresh();
    ProjectModel *m;
    MediaList *m_list;
    QLineEdit *m_filter;
};

} // namespace mv
