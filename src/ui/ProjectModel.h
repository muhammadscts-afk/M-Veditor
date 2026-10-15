// Owns the Project, undo/redo history, selection, playhead and cached media assets (thumbnails, waveforms).
#pragma once
#include "Project.h"
#include <QHash>
#include <QObject>
#include <QUndoStack>
#include <QVector>
#include <functional>

namespace mv {

class ProjectModel : public QObject {
    Q_OBJECT
public:
    explicit ProjectModel(QObject *parent = nullptr);

    const Project &project() const { return m_project; }
    QUndoStack *undoStack() { return &m_undo; }
    const QString &filePath() const { return m_path; }
    bool isDirty() const { return m_dirty; }
    void setDirty(bool d);

    // Every user-visible edit goes through mutate(): one undo step; consecutive edits sharing a mergeKey collapse.
    void mutate(const QString &text, const std::function<void(Project &)> &fn, const QString &mergeKey = {});
    void reset(const Project &p, const QString &path);
    void setFilePath(const QString &p) { m_path = p; emit titleChanged(); }
    void restoreFrom(const QJsonObject &snapshot);   // used by undo/redo

    // selection / playhead
    QString selectedClip() const { return m_selClip; }
    void selectClip(const QString &id);
    QString selectedMedia() const { return m_selMedia; }
    void selectMedia(const QString &id);
    double playhead() const { return m_playhead; }
    void setPlayhead(double t, bool fromPlayback = false);

    // runtime-only assets
    QString thumbnail(const QString &mediaId) const { return m_thumbs.value(mediaId); }
    void setThumbnail(const QString &mediaId, const QString &path);
    const QVector<float> *waveform(const QString &mediaId) const;
    void setWaveform(const QString &path, const QVector<float> &peaks);
    QString proxyFor(const QString &mediaId) const { return m_proxies.value(mediaId); }
    void setProxy(const QString &mediaId, const QString &path) { m_proxies[mediaId] = path; emit assetsChanged(); }
    QString playablePath(const QString &mediaId, bool allowProxy) const;

signals:
    void changed();                  // project content changed (also after undo/redo)
    void selectionChanged();
    void mediaSelectionChanged();
    void playheadChanged(double t, bool fromPlayback);
    void assetsChanged();
    void titleChanged();
    void dirtyChanged(bool);

private:
    Project m_project;
    QUndoStack m_undo;
    QString m_path, m_selClip, m_selMedia;
    bool m_dirty = false;
    double m_playhead = 0;
    QHash<QString, QString> m_thumbs, m_proxies;
    QHash<QString, QVector<float>> m_waves;   // keyed by file path
};

} // namespace mv
