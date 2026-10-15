// Preview monitor: source viewing, live timeline preview (top video clip + titles), rendered full preview.
#pragma once
#include "ProjectModel.h"
#include <QAudioOutput>
#include <QElapsedTimer>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsVideoItem>
#include <QGraphicsView>
#include <QLabel>
#include <QMediaPlayer>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

namespace mv {

class PreviewMonitor : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Source, Timeline, Rendered };
    explicit PreviewMonitor(ProjectModel *model, QWidget *parent = nullptr);
    void playSource(const QString &mediaId);
    void playRendered(const QString &path);
    void togglePlay();
    void pause();
    void stop();
    void stepFrame(int dir);
    bool isPlaying() const;
    Mode mode() const { return m_mode; }
    void releaseMedia();                       // free file handles (project closing)
signals:
    void renderPreviewRequested();
    void addToTimelineRequested(const QString &mediaId, double in, double out);
    void message(const QString &text);
protected:
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
private:
    struct Pending { bool valid = false; qint64 posMs = 0; bool play = false; };
    void setMode(Mode m);
    void setCanvas(int w, int h);
    void fit();
    void syncTo(double t, bool play);
    void applyToPlayer(qint64 posMs, bool play, double rate, double vol);
    void updateTitles(double t);
    void tick();
    void updateTimeLabel();
    void onPlayheadChanged(double t, bool fromPlayback);
    const Clip *activeVideoClip(double t) const;

    ProjectModel *m;
    QGraphicsScene *m_scene;
    QGraphicsView *m_view;
    QGraphicsRectItem *m_canvas;
    QGraphicsVideoItem *m_video;
    QGraphicsPixmapItem *m_image;
    QVector<QGraphicsItem *> m_titleItems;
    QMediaPlayer *m_player;
    QAudioOutput *m_audio;
    QSlider *m_slider;
    QLabel *m_time, *m_modeLabel;
    QToolButton *m_play, *m_srcBtn, *m_tlBtn;
    QTimer m_timer;
    QElapsedTimer m_clock;
    Mode m_mode = Mode::Timeline;
    bool m_tlPlaying = false, m_sliderDrag = false;
    double m_t0 = 0;
    QString m_loadedPath, m_sourceId, m_renderedPath;
    Pending m_pending;
    double m_markIn = -1, m_markOut = -1, m_srcFps = 30;
    QString m_activeImageId;
    int m_cw = 1920, m_ch = 1080;
};

} // namespace mv
