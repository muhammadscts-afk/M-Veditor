#pragma once
#include "AudioAnalysis.h"
#include "ExportJob.h"
#include "ExportPanel.h"
#include "MediaProbe.h"
#include "MediaLibrary.h"
#include "PreviewMonitor.h"
#include "ProjectModel.h"
#include "PropertiesPanel.h"
#include "TimelineView.h"
#include <QLabel>
#include <QLockFile>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QTimer>
#include <memory>

namespace mv {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    bool openProject(const QString &path);
    void importFiles(const QStringList &files);
    void checkRecovery();
    ProjectModel *model() { return m; }
    TimelineView *timeline() { return m_tl; }
    void startExportWith(const ExportOptions &o);
    bool busy() const { return m_job && m_job->isRunning(); }
    bool startMaximized() const { return m_startMax; }

protected:
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;

private:
    enum class JobKind { Export, Preview, Proxy, Extract };
    // construction
    void buildActions();
    void buildMenus();
    void buildToolbar();
    void buildDocks();
    void updateActions();
    void updateTitle();
    void notify(const QString &text, bool error = false);
    // project
    bool maybeSave();
    void newProject();
    bool saveProject();
    bool saveProjectAs();
    bool writeProject(const QString &path);
    void openDialog();
    void rebuildRecentMenu();
    void relinkMissing();
    void requestAssets(const MediaItem &mi);
    void autosave();
    void clearRecovery();
    // media / clips
    void onProbed(const QString &path, const ProbeResult &r);
    void removeMedia(const QString &id);
    void renameMedia(const QString &id);
    QString defaultTrackFor(const MediaItem &mi, bool audioOnly = false) const;
    void addClipFromMedia(const QString &mediaId, const QString &trackId, double start, double in = 0, double out = -1);
    void splitAtPlayhead();
    void deleteSelected(bool ripple);
    void duplicateSelected();
    void copySelected(bool cut);
    void pasteClip(double at = -1);
    void detachAudio();
    void applySpeedDialog();
    void addTitle(const QString &kind, double at = -1, const QString &trackId = {});
    void addWatermark();
    void applyPip();
    void removeSilence();
    void addTrack(TrackType t);
    void editSelected(const QString &what, const std::function<void(Clip &)> &fn);
    void setTransition(const QString &t);
    void addMarker(double t);
    void jumpEdge(int dir);
    void showClipMenu(const QString &id, const QPoint &gp);
    void showTrackMenu(const QString &id, const QPoint &gp);
    // jobs
    void startExport();
    void renderPreview(bool autoPlay = false);
    void playTimeline();
    QByteArray projectHash() const;
    void tidyTrack(Project &p, const QString &trackId);
    void extractAudio();
    void createProxy(const QString &mediaId);
    bool runJob(const Project &p, const ExportOptions &o, JobKind kind, const QString &label);
    void onJobFinished(bool ok, const QString &msg, const QString &path);
    void showManual();
    void about();

    ProjectModel *m;
    TimelineView *m_tl;
    PreviewMonitor *m_prev;
    MediaLibrary *m_lib;
    PropertiesPanel *m_props;
    ExportPanel *m_exp;
    QPlainTextEdit *m_log;
    QDockWidget *m_dockLib, *m_dockProps, *m_dockExp, *m_dockLog;
    QLabel *m_etaLabel, *m_stateLabel;
    QProgressBar *m_bar;
    QMenu *m_recentMenu, *m_transMenu;
    QHash<QString, QAction *> a;
    QTimer m_autosave;
    std::unique_ptr<ExportJob> m_job;
    JobKind m_kind = JobKind::Export;
    QString m_jobLabel, m_jobProxyId;
    ThumbnailGenerator *m_thumbs;
    int m_pendingImports = 0;
    QStringList m_importErrors;
    QVector<Clip> m_clipboard;
    std::unique_ptr<QLockFile> m_lock;
    bool m_primaryInstance = true;
    QString m_previewFile;
    QByteArray m_previewHash, m_pendingHash;
    bool m_autoPlay = false, m_startMax = false;
    double m_playStart = 0;
    static constexpr double kImageSeconds = 5.0;
};

} // namespace mv
