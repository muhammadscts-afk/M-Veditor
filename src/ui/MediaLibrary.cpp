#include "MediaLibrary.h"
#include "MediaProbe.h"
#include "Theme.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace mv {

static const char *kMediaMime = "application/x-mveditor-media";

QMimeData *MediaList::mimeData(const QList<QListWidgetItem *> &items) const {
    auto *md = new QMimeData;
    if (!items.isEmpty()) md->setData(kMediaMime, items.first()->data(Qt::UserRole).toString().toUtf8());
    return md;
}
void MediaList::startDrag(Qt::DropActions) {
    auto items = selectedItems();
    if (items.isEmpty()) return;
    auto *d = new QDrag(this);
    d->setMimeData(mimeData(items));
    d->setPixmap(items.first()->icon().pixmap(72, 48));
    d->exec(Qt::CopyAction);
}

MediaLibrary::MediaLibrary(ProjectModel *model, QWidget *parent) : QWidget(parent), m(model) {
    setAcceptDrops(true);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    auto *imp = new QPushButton(tr("Import…"));
    imp->setToolTip(tr("Import video, audio or image files (Ctrl+I). You can also drop files here."));
    auto *rem = new QPushButton(tr("Remove")); rem->setToolTip(tr("Remove from the project (the file on disk is never deleted)"));
    auto *ren = new QPushButton(tr("Rename")); ren->setToolTip(tr("Rename the item inside the project (the file keeps its name)"));
    m_filter = new QLineEdit; m_filter->setPlaceholderText(tr("Search…")); m_filter->setClearButtonEnabled(true);
    auto *type = new QComboBox; type->addItems({tr("All"), tr("Video"), tr("Audio"), tr("Images")}); type->setToolTip(tr("Show only one kind of media"));
    auto *grid = new QGridLayout; grid->setSpacing(4);          // row 1: Import | Remove      row 2: Rename | All
    grid->addWidget(imp, 0, 0); grid->addWidget(rem, 0, 1);
    grid->addWidget(ren, 1, 0); grid->addWidget(type, 1, 1);
    lay->addLayout(grid);
    m_list = new MediaList;
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(96, 54));
    m_list->setGridSize(QSize(112, 96));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(true);
    m_list->setDragEnabled(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setAccessibleName(tr("Media library"));
    lay->addWidget(m_list, 1);
    lay->addWidget(m_filter);

    connect(imp, &QPushButton::clicked, this, &MediaLibrary::importRequested);
    connect(rem, &QPushButton::clicked, this, [this] { if (!currentMediaId().isEmpty()) emit removeRequested(currentMediaId()); });
    connect(ren, &QPushButton::clicked, this, [this] { if (!currentMediaId().isEmpty()) emit renameRequested(currentMediaId()); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) { emit previewRequested(it->data(Qt::UserRole).toString()); });
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *it) { m->selectMedia(it ? it->data(Qt::UserRole).toString() : QString()); });
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint &p) {
        auto *it = m_list->itemAt(p);
        if (it) emit contextMenuRequested(it->data(Qt::UserRole).toString(), m_list->viewport()->mapToGlobal(p));
    });
    connect(m_filter, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(type, &QComboBox::currentIndexChanged, this, [this, type](int) { m_list->setProperty("typeFilter", type->currentIndex()); refresh(); });
    connect(m, &ProjectModel::changed, this, &MediaLibrary::refresh);
    connect(m, &ProjectModel::assetsChanged, this, &MediaLibrary::refresh);
    refresh();
}

QString MediaLibrary::currentMediaId() const { auto *it = m_list->currentItem(); return it ? it->data(Qt::UserRole).toString() : QString(); }

void MediaLibrary::refresh() {
    const QString keep = currentMediaId();
    const QString f = m_filter->text().trimmed();
    const int tf = m_list->property("typeFilter").toInt();
    m_list->clear();
    for (const MediaItem &mi : m->project().media) {
        if (!f.isEmpty() && !mi.name.contains(f, Qt::CaseInsensitive)) continue;
        if (tf == 1 && mi.kind != "video") continue;
        if (tf == 2 && mi.kind != "audio") continue;
        if (tf == 3 && mi.kind != "image") continue;
        auto *it = new QListWidgetItem(mi.name);
        it->setData(Qt::UserRole, mi.id);
        QPixmap pm(96, 54);
        const QString th = m->thumbnail(mi.id);
        if (!th.isEmpty() && pm.load(th)) pm = pm.scaled(96, 54, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        else {
            pm = QPixmap(96, 54); pm.fill(Theme::clipColor(mi.kind).darker(130));
            QPainter p(&pm); p.setPen(Qt::white); p.drawText(pm.rect(), Qt::AlignCenter, mi.kind == "audio" ? QString::fromUtf8("♪ audio") : mi.kind);
        }
        if (mi.missing) { QPainter p(&pm); p.fillRect(pm.rect(), QColor(200, 40, 40, 140)); p.setPen(Qt::white); p.drawText(pm.rect(), Qt::AlignCenter, tr("MISSING")); }
        it->setIcon(QIcon(pm));
        QString tip = QString("%1\n%2\n%3").arg(QDir::toNativeSeparators(mi.path), humanTime(mi.duration), humanSize(mi.sizeBytes));
        if (mi.width) tip += QString("\n%1×%2").arg(mi.width).arg(mi.height);
        if (mi.missing) tip += "\n" + tr("The file is missing. Use File > Relink Missing Media.");
        it->setToolTip(tip);
        m_list->addItem(it);
        if (mi.id == keep) m_list->setCurrentItem(it);
    }
}

void MediaLibrary::dragEnterEvent(QDragEnterEvent *e) { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
void MediaLibrary::dropEvent(QDropEvent *e) {
    QStringList paths;
    for (auto &u : e->mimeData()->urls()) if (u.isLocalFile()) paths << u.toLocalFile();
    if (!paths.isEmpty()) emit filesDropped(paths);
}

} // namespace mv
