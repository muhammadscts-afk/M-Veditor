#include "ProjectModel.h"
#include <QDateTime>
#include <QFileInfo>
#include <QUndoCommand>

namespace mv {

namespace {
class SnapshotCommand : public QUndoCommand {
public:
    SnapshotCommand(ProjectModel *m, const QString &text, const QJsonObject &before, const QJsonObject &after, const QString &key)
        : QUndoCommand(text), m_model(m), m_before(before), m_after(after), m_key(key), m_time(QDateTime::currentMSecsSinceEpoch()) {}
    int id() const override { return m_key.isEmpty() ? -1 : int(qHash(m_key) & 0x7fffffff); }
    bool mergeWith(const QUndoCommand *o) override {
        auto *c = dynamic_cast<const SnapshotCommand *>(o);
        if (!c || c->m_key != m_key || c->m_time - m_time > 1500) return false;
        m_after = c->m_after; m_time = c->m_time;
        return true;
    }
    void undo() override { m_model->restoreFrom(m_before); }
    void redo() override { if (m_first) { m_first = false; return; } m_model->restoreFrom(m_after); }
private:
    ProjectModel *m_model;
    QJsonObject m_before, m_after;
    QString m_key;
    qint64 m_time;
    bool m_first = true;
};
} // namespace

ProjectModel::ProjectModel(QObject *parent) : QObject(parent) {
    m_project.ensureDefaultTracks();
    m_undo.setUndoLimit(200);
}

void ProjectModel::setDirty(bool d) { if (m_dirty != d) { m_dirty = d; emit dirtyChanged(d); emit titleChanged(); } }

void ProjectModel::mutate(const QString &text, const std::function<void(Project &)> &fn, const QString &key) {
    const QJsonObject before = m_project.toJson(m_path);
    fn(m_project);
    const QJsonObject after = m_project.toJson(m_path);
    if (before == after) return;
    pruneSelection();
    m_undo.push(new SnapshotCommand(this, text, before, after, key));
    setDirty(true);
    emit changed();
}

void ProjectModel::restoreFrom(const QJsonObject &snap) {
    QString err;
    Project p = Project::fromJson(snap, m_path, &err);
    if (!err.isEmpty()) return;
    m_project = p;
    pruneSelection();
    setDirty(true);
    emit changed();
}

void ProjectModel::reset(const Project &p, const QString &path) {
    m_project = p; m_path = path; m_undo.clear(); m_selClip.clear(); m_sel.clear(); m_selMedia.clear(); m_playhead = 0;
    m_thumbs.clear(); m_proxies.clear(); m_waves.clear();
    m_project.ensureDefaultTracks();
    setDirty(false);
    emit titleChanged(); emit selectionChanged(); emit changed(); emit playheadChanged(0, false); emit assetsChanged();
}

void ProjectModel::pruneSelection() {
    const QStringList before = m_sel;
    QStringList keep;
    for (const QString &id : m_sel) if (m_project.clipById(id)) keep << id;
    m_sel = keep;
    if (!m_sel.contains(m_selClip)) m_selClip = m_sel.isEmpty() ? QString() : m_sel.last();
    if (before != m_sel) emit selectionChanged();
}
void ProjectModel::selectClip(const QString &id) {
    const QStringList n = id.isEmpty() ? QStringList() : QStringList{id};
    if (m_sel != n || m_selClip != id) { m_sel = n; m_selClip = id; emit selectionChanged(); }
}
void ProjectModel::toggleClip(const QString &id) {
    if (m_sel.contains(id)) { m_sel.removeAll(id); if (m_selClip == id) m_selClip = m_sel.isEmpty() ? QString() : m_sel.last(); }
    else { m_sel << id; m_selClip = id; }
    emit selectionChanged();
}
void ProjectModel::addToSelection(const QString &id) { if (!m_sel.contains(id)) m_sel << id; m_selClip = id; emit selectionChanged(); }
void ProjectModel::setSelection(const QStringList &ids) {
    if (m_sel == ids) return;
    m_sel = ids; if (!m_sel.contains(m_selClip)) m_selClip = m_sel.isEmpty() ? QString() : m_sel.last();
    emit selectionChanged();
}
void ProjectModel::makePrimary(const QString &id) { if (m_sel.contains(id) && m_selClip != id) { m_selClip = id; emit selectionChanged(); } }
void ProjectModel::selectMedia(const QString &id) { if (m_selMedia != id) { m_selMedia = id; emit mediaSelectionChanged(); } }
void ProjectModel::setPlayhead(double t, bool fromPlayback) {
    t = std::max(0.0, t);
    if (qFuzzyCompare(t + 1, m_playhead + 1) && !fromPlayback) return;
    m_playhead = t;
    emit playheadChanged(t, fromPlayback);
}
void ProjectModel::setThumbnail(const QString &id, const QString &p) { m_thumbs[id] = p; emit assetsChanged(); }
const QVector<float> *ProjectModel::waveform(const QString &mediaId) const {
    const MediaItem *m = m_project.mediaById(mediaId);
    if (!m) return nullptr;
    auto it = m_waves.constFind(m->path);
    return it == m_waves.constEnd() ? nullptr : &it.value();
}
void ProjectModel::setWaveform(const QString &path, const QVector<float> &peaks) { m_waves[path] = peaks; emit assetsChanged(); }
QString ProjectModel::playablePath(const QString &mediaId, bool allowProxy) const {
    const MediaItem *m = m_project.mediaById(mediaId);
    if (!m) return {};
    const QString px = m_proxies.value(mediaId);
    if (allowProxy && !px.isEmpty() && QFileInfo::exists(px)) return px;
    return m->path;
}

} // namespace mv
