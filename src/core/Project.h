// M Veditor - project data model (non-destructive: only references media files).
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace mv {

enum class TrackType { Video, Audio, Music, Voice, Title };
QString toString(TrackType t);
TrackType trackTypeFromString(const QString &s);
inline bool isAudioType(TrackType t) { return t == TrackType::Audio || t == TrackType::Music || t == TrackType::Voice; }
QString newId(const QString &prefix);

struct MediaItem {
    QString id, path, relPath, name;
    QString kind;                       // "video" | "audio" | "image"
    double duration = 0;
    int width = 0, height = 0;
    double fps = 0;
    bool hasVideo = false, hasAudio = false;
    int sampleRate = 0, channels = 0;
    qint64 sizeBytes = 0, bitrate = 0;
    QString videoCodec, audioCodec, container;
    bool missing = false;
    QString proxyPath;                  // optional optimized preview file (never used for export)

    QJsonObject toJson() const;
    static MediaItem fromJson(const QJsonObject &o);
};

struct TitleSpec {
    QString text = QStringLiteral("Title");
    double fontSize = 64;               // pixels at 1080p; scaled with output height
    QString color = QStringLiteral("#ffffff");
    bool box = true;
    QString boxColor = QStringLiteral("#000000");
    double boxOpacity = 0.5;
    double posX = 0.5, posY = 0.85;     // 0..1 position of the text box
    QString kind = QStringLiteral("title"); // title | subtitle | caption
    QJsonObject toJson() const;
    static TitleSpec fromJson(const QJsonObject &o);
};

struct ClipEffects {
    double speed = 1.0;                 // 0.1 .. 32
    bool preservePitch = true;
    bool reverse = false;
    double brightness = 0, contrast = 1, saturation = 1, sharpness = 0;
    double balR = 0, balG = 0, balB = 0; // -1..1
    int rotation = 0;                   // 0/90/180/270
    bool flipH = false, flipV = false;
    double cropL = 0, cropT = 0, cropR = 0, cropB = 0; // fractions
    double scale = 1.0;                 // 1 = fit canvas, <1 picture-in-picture
    double posX = 0.5, posY = 0.5;
    double opacity = 1.0;
    double fadeIn = 0, fadeOut = 0;     // seconds, video and audio
    double volume = 1.0;
    bool mute = false, normalize = false, compressor = false, denoise = false;
    double eqBass = 0, eqMid = 0, eqTreble = 0; // dB
    double pitchSemitones = 0;
    QString transition = QStringLiteral("none"); // none|fade|dissolve|wipeleft|wiperight|wipeup|wipedown
    double transitionDur = 1.0;

    bool isDefaultForCopy() const;      // true when stream copy would be lossless-correct
    QJsonObject toJson() const;
    static ClipEffects fromJson(const QJsonObject &o);
};

struct Clip {
    QString id, mediaId, trackId, label;
    double start = 0;                   // timeline position (s)
    double in = 0, out = 0;             // source range (s); titles: 0..duration
    ClipEffects fx;
    TitleSpec title;

    bool isTitle() const { return mediaId.isEmpty(); }
    double duration() const;            // timeline duration (accounts for speed)
    double end() const { return start + duration(); }
    QJsonObject toJson() const;
    static Clip fromJson(const QJsonObject &o);
};

struct Track {
    QString id, name;
    TrackType type = TrackType::Video;
    bool muted = false, solo = false, locked = false, hidden = false;
    double volume = 1.0;
    int height = 56;
    QJsonObject toJson() const;
    static Track fromJson(const QJsonObject &o);
};

struct Marker {
    double time = 0;
    QString name;
    QString color;                      // optional "#rrggbb" tint of the region that starts at this marker
};

struct ProjectSettings {
    int width = 1920, height = 1080;
    double fps = 30;
    int sampleRate = 48000, channels = 2;
};

class Project {
public:
    static constexpr int kFormatVersion = 1;
    QString name = QStringLiteral("Untitled project");
    ProjectSettings settings;
    QVector<MediaItem> media;
    QVector<Track> tracks;             // index 0 = top of the timeline
    QVector<Clip> clips;
    QVector<Marker> markers;

    const MediaItem *mediaById(const QString &id) const;
    MediaItem *mediaById(const QString &id);
    const Clip *clipById(const QString &id) const;
    Clip *clipById(const QString &id);
    const Track *trackById(const QString &id) const;
    Track *trackById(const QString &id);
    int trackIndex(const QString &id) const;
    QVector<const Clip *> clipsOnTrack(const QString &trackId) const; // sorted by start
    double duration() const;
    // Solo rules: sound: if any track is soloed only soloed tracks are heard.
    // picture: if any video/title track is soloed only soloed video/title tracks are shown.
    bool trackAudible(const Track &t) const;
    // Everything that influences the rendered result - deliberately excludes markers, names, track heights, locks.
    QByteArray renderSignature() const;
    bool trackVisible(const Track &t) const;
    int usageCount(const QString &mediaId) const;

    void ensureDefaultTracks();
    // Nearest free start so [start, start+dur) doesn't overlap other clips on the track.
    double resolveOverlap(const QString &trackId, double start, double dur, const QString &ignoreClip = {}) const;
    QString firstTrackOfType(TrackType t) const;

    QJsonObject toJson(const QString &projectFilePath = {}) const;
    static Project fromJson(const QJsonObject &o, const QString &projectFilePath, QString *error = nullptr);

    // A throw-away project containing one clip (used for conversion, extraction and proxies).
    static Project singleClip(const MediaItem &m, double in = 0, double out = -1);
};

} // namespace mv
