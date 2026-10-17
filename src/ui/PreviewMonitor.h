// Preview monitor: source viewing, smooth LIVE timeline playback (double-buffered video + audio tracks),
// optional exact rendered preview and direct play of a single clip.
#pragma once
#include "ProjectModel.h"
#include <QAudioOutput>
#include <QElapsedTimer>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsVideoItem>
#include <QGraphicsView>
#include <QLabel>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

namespace mv {

// A slider that jumps to the clicked position (a normal QSlider only moves by one page step).
class SeekSlider : public QSlider {
    Q_OBJECT
public:
    explicit SeekSlider(Qt::Orientation o, QWidget *parent = nullptr) : QSlider(o, parent) {}
signals:
    void seekRequested(double fraction);
    void dragStateChanged(bool down);
protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton) return QSlider::mousePressEvent(e);
        m_down = true; emit dragStateChanged(true); seekTo(e->position().x()); e->accept();
    }
    void mouseMoveEvent(QMouseEvent *e) override { if (m_down) { seekTo(e->position().x()); e->accept(); } }
    void mouseReleaseEvent(QMouseEvent *e) override { if (m_down) { m_down = false; emit dragStateChanged(false); e->accept(); } }
private:
    void seekTo(double x) {
        const int pad = 9;
        const int v = QStyle::sliderValueFromPosition(minimum(), maximum(), int(x) - pad, std::max(1, width() - 2 * pad));
        setValue(v);
        emit seekRequested(maximum() > 0 ? double(v) / maximum() : 0.0);
    }
    bool m_down = false;
};

class PreviewMonitor : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Source, Timeline, Rendered };
    explicit PreviewMonitor(ProjectModel *model, QWidget *parent = nullptr);
    void playSource(const QString &mediaId);
    void playRendered(const QString &path, double startSeconds, bool autoplay);
    void playDirect(const QString &path, double in, double out, double startOnTimeline, bool autoplay);
    void togglePlay();
    void pause();
    void stop();
    void stepFrame(int dir);
    bool isPlaying() const;
    Mode mode() const { return m_mode; }
    bool liveEnabled() const { return m_live->isChecked(); }
    void releaseMedia();
    void showBusy(const QString &text);
signals:
    void renderPreviewRequested();
    void timelinePlayRequested();      // Live is off: the main window renders / plays an exact preview
    void stopPressed();
    void addToTimelineRequested(const QString &mediaId, double in, double out);
    void message(const QString &text);
protected:
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
private:
    struct Pending { bool valid = false; qint64 posMs = 0; bool play = false; double rate = 1.0, vol = 1.0; };
    struct Preload { QMediaPlayer *pl = nullptr; QAudioOutput *ao = nullptr; QGraphicsVideoItem *vi = nullptr;
                     QString clipId, path; Pending pend; bool loaded = false; qint64 lastPos = 0; };
    struct Voice { QMediaPlayer *pl = nullptr; QAudioOutput *ao = nullptr; QString clipId, path; Pending pend; bool loaded = false; };

    void wire(QMediaPlayer *pl);
    void onPosition(QMediaPlayer *pl, qint64 pos);
    void onState(QMediaPlayer *pl, QMediaPlayer::PlaybackState s);
    void onStatus(QMediaPlayer *pl, QMediaPlayer::MediaStatus st);
    void setMode(Mode m);
    void setCanvas(int w, int h);
    void fit();
    // live engine
    void syncTo(double t, bool play);
    double liveTime();
    void loadActive(const Clip &c, const MediaItem &mi, qint64 target, bool play, double rate, double vol);
    void keepPlaying(qint64 target);
    void preloadDeck(const Clip &c);
    void swapDecks();
    void syncVoices(double t, bool play);
    void ensureVoices();
    void releaseLive();
    const Clip *nextVideoClip(double after) const;
    void updateLiveNote();
    void applyToPlayer(qint64 posMs, bool play, double rate, double vol);
    void updateTitles(double t);
    void tick();
    void smoothTick();
    void updateTimeLabel();
    void onPlayheadChanged(double t, bool fromPlayback);
    void onProjectChanged();
    void onSeek(double fraction);
    qint64 rangeMs() const;
    const Clip *activeVideoClip(double t) const;

    ProjectModel *m;
    QGraphicsScene *m_scene;
    QGraphicsView *m_view;
    QGraphicsRectItem *m_canvas;
    QGraphicsVideoItem *m_video;                 // the ACTIVE deck's picture
    QGraphicsPixmapItem *m_image;
    QGraphicsSimpleTextItem *m_busy, *m_note;
    QVector<QGraphicsItem *> m_titleItems;
    QString m_titleKey;
    QMediaPlayer *m_player;                      // the ACTIVE deck's player (swapped with m_pre on gap-free cuts)
    QAudioOutput *m_audio;
    Preload m_pre;                               // the second deck: preloads the next clip
    QVector<Voice> m_voices;                     // audio-track players (audio, music, voice-over)
    QString m_activeClipId;
    SeekSlider *m_slider;
    QLabel *m_time, *m_modeLabel;
    QToolButton *m_play, *m_srcBtn, *m_tlBtn, *m_live;
    QWidget *m_srcTools;
    QTimer m_timer, m_smooth;
    QElapsedTimer m_clock, m_lastClock;
    Mode m_mode = Mode::Timeline;
    bool m_tlPlaying = false, m_sliderDrag = false;
    double m_t0 = 0;
    qint64 m_lastReported = 0, m_offsetMs = 0, m_endMs = 0, m_lastLiveMs = 0;
    QByteArray m_renderedSig;
    QString m_loadedPath, m_sourceId;
    Pending m_pending;
    double m_markIn = -1, m_markOut = -1, m_srcFps = 30;
    QString m_activeImageId;
    int m_cw = 1920, m_ch = 1080;
};

} // namespace mv
