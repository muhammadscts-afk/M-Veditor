#include "Project.h"
#include <QDir>
#include <QFileInfo>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace mv {

QString newId(const QString &prefix) { return prefix + QUuid::createUuid().toString(QUuid::Id128).left(10); }

QString toString(TrackType t) {
    switch (t) {
    case TrackType::Video: return "video";
    case TrackType::Audio: return "audio";
    case TrackType::Music: return "music";
    case TrackType::Voice: return "voice";
    case TrackType::Title: return "title";
    }
    return "video";
}
TrackType trackTypeFromString(const QString &s) {
    if (s == "audio") return TrackType::Audio;
    if (s == "music") return TrackType::Music;
    if (s == "voice") return TrackType::Voice;
    if (s == "title") return TrackType::Title;
    return TrackType::Video;
}

QJsonObject MediaItem::toJson() const {
    QJsonObject o;
    o["id"] = id; o["path"] = path; o["relPath"] = relPath; o["name"] = name; o["kind"] = kind;
    o["duration"] = duration; o["width"] = width; o["height"] = height; o["fps"] = fps;
    o["hasVideo"] = hasVideo; o["hasAudio"] = hasAudio; o["sampleRate"] = sampleRate; o["channels"] = channels;
    o["sizeBytes"] = double(sizeBytes); o["bitrate"] = double(bitrate);
    o["videoCodec"] = videoCodec; o["audioCodec"] = audioCodec; o["container"] = container;
    return o;
}
MediaItem MediaItem::fromJson(const QJsonObject &o) {
    MediaItem m;
    m.id = o["id"].toString(); m.path = o["path"].toString(); m.relPath = o["relPath"].toString();
    m.name = o["name"].toString(); m.kind = o["kind"].toString("video");
    m.duration = o["duration"].toDouble(); m.width = o["width"].toInt(); m.height = o["height"].toInt();
    m.fps = o["fps"].toDouble(); m.hasVideo = o["hasVideo"].toBool(); m.hasAudio = o["hasAudio"].toBool();
    m.sampleRate = o["sampleRate"].toInt(); m.channels = o["channels"].toInt();
    m.sizeBytes = qint64(o["sizeBytes"].toDouble()); m.bitrate = qint64(o["bitrate"].toDouble());
    m.videoCodec = o["videoCodec"].toString(); m.audioCodec = o["audioCodec"].toString();
    m.container = o["container"].toString();
    return m;
}

QJsonObject TitleSpec::toJson() const {
    QJsonObject o;
    o["text"] = text; o["fontSize"] = fontSize; o["color"] = color; o["box"] = box;
    o["boxColor"] = boxColor; o["boxOpacity"] = boxOpacity; o["posX"] = posX; o["posY"] = posY; o["kind"] = kind;
    return o;
}
TitleSpec TitleSpec::fromJson(const QJsonObject &o) {
    TitleSpec t;
    t.text = o["text"].toString(t.text); t.fontSize = o["fontSize"].toDouble(t.fontSize);
    t.color = o["color"].toString(t.color); t.box = o["box"].toBool(t.box);
    t.boxColor = o["boxColor"].toString(t.boxColor); t.boxOpacity = o["boxOpacity"].toDouble(t.boxOpacity);
    t.posX = o["posX"].toDouble(t.posX); t.posY = o["posY"].toDouble(t.posY); t.kind = o["kind"].toString(t.kind);
    return t;
}

static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

bool ClipEffects::isDefaultForCopy() const {
    return near(speed, 1) && !reverse && near(brightness, 0) && near(contrast, 1) && near(saturation, 1) &&
           near(sharpness, 0) && near(balR, 0) && near(balG, 0) && near(balB, 0) && rotation == 0 && !flipH &&
           !flipV && near(cropL, 0) && near(cropT, 0) && near(cropR, 0) && near(cropB, 0) && near(scale, 1) &&
           near(opacity, 1) && near(fadeIn, 0) && near(fadeOut, 0) && near(volume, 1) && !mute && !normalize &&
           !compressor && !denoise && near(eqBass, 0) && near(eqMid, 0) && near(eqTreble, 0) &&
           near(pitchSemitones, 0) && transition == "none";
}

QJsonObject ClipEffects::toJson() const {
    QJsonObject o;
    o["speed"] = speed; o["preservePitch"] = preservePitch; o["reverse"] = reverse;
    o["brightness"] = brightness; o["contrast"] = contrast; o["saturation"] = saturation; o["sharpness"] = sharpness;
    o["balR"] = balR; o["balG"] = balG; o["balB"] = balB; o["rotation"] = rotation;
    o["flipH"] = flipH; o["flipV"] = flipV;
    o["cropL"] = cropL; o["cropT"] = cropT; o["cropR"] = cropR; o["cropB"] = cropB;
    o["scale"] = scale; o["posX"] = posX; o["posY"] = posY; o["opacity"] = opacity;
    o["fadeIn"] = fadeIn; o["fadeOut"] = fadeOut; o["volume"] = volume; o["mute"] = mute;
    o["normalize"] = normalize; o["compressor"] = compressor; o["denoise"] = denoise;
    o["eqBass"] = eqBass; o["eqMid"] = eqMid; o["eqTreble"] = eqTreble; o["pitchSemitones"] = pitchSemitones;
    o["transition"] = transition; o["transitionDur"] = transitionDur;
    return o;
}
ClipEffects ClipEffects::fromJson(const QJsonObject &o) {
    ClipEffects f;
    f.speed = o["speed"].toDouble(1); f.preservePitch = o["preservePitch"].toBool(true); f.reverse = o["reverse"].toBool();
    f.brightness = o["brightness"].toDouble(0); f.contrast = o["contrast"].toDouble(1);
    f.saturation = o["saturation"].toDouble(1); f.sharpness = o["sharpness"].toDouble(0);
    f.balR = o["balR"].toDouble(); f.balG = o["balG"].toDouble(); f.balB = o["balB"].toDouble();
    f.rotation = o["rotation"].toInt(); f.flipH = o["flipH"].toBool(); f.flipV = o["flipV"].toBool();
    f.cropL = o["cropL"].toDouble(); f.cropT = o["cropT"].toDouble(); f.cropR = o["cropR"].toDouble(); f.cropB = o["cropB"].toDouble();
    f.scale = o["scale"].toDouble(1); f.posX = o["posX"].toDouble(0.5); f.posY = o["posY"].toDouble(0.5);
    f.opacity = o["opacity"].toDouble(1); f.fadeIn = o["fadeIn"].toDouble(); f.fadeOut = o["fadeOut"].toDouble();
    f.volume = o["volume"].toDouble(1); f.mute = o["mute"].toBool(); f.normalize = o["normalize"].toBool();
    f.compressor = o["compressor"].toBool(); f.denoise = o["denoise"].toBool();
    f.eqBass = o["eqBass"].toDouble(); f.eqMid = o["eqMid"].toDouble(); f.eqTreble = o["eqTreble"].toDouble();
    f.pitchSemitones = o["pitchSemitones"].toDouble();
    f.transition = o["transition"].toString("none"); f.transitionDur = o["transitionDur"].toDouble(1);
    if (f.speed < 0.01) f.speed = 1;
    return f;
}

double Clip::duration() const { return std::max(0.0, out - in) / std::max(fx.speed, 0.01); }

QJsonObject Clip::toJson() const {
    QJsonObject o;
    o["id"] = id; o["mediaId"] = mediaId; o["trackId"] = trackId; o["label"] = label;
    o["start"] = start; o["in"] = in; o["out"] = out; o["fx"] = fx.toJson();
    if (isTitle()) o["title"] = title.toJson();
    return o;
}
Clip Clip::fromJson(const QJsonObject &o) {
    Clip c;
    c.id = o["id"].toString(); c.mediaId = o["mediaId"].toString(); c.trackId = o["trackId"].toString();
    c.label = o["label"].toString(); c.start = o["start"].toDouble(); c.in = o["in"].toDouble();
    c.out = o["out"].toDouble(); c.fx = ClipEffects::fromJson(o["fx"].toObject());
    if (o.contains("title")) c.title = TitleSpec::fromJson(o["title"].toObject());
    return c;
}
QJsonObject Track::toJson() const {
    QJsonObject o;
    o["id"] = id; o["name"] = name; o["type"] = mv::toString(type); o["muted"] = muted; o["solo"] = solo;
    o["locked"] = locked; o["hidden"] = hidden; o["volume"] = volume; o["height"] = height;
    return o;
}
Track Track::fromJson(const QJsonObject &o) {
    Track t;
    t.id = o["id"].toString(); t.name = o["name"].toString(); t.type = trackTypeFromString(o["type"].toString());
    t.muted = o["muted"].toBool(); t.solo = o["solo"].toBool(); t.locked = o["locked"].toBool();
    t.hidden = o["hidden"].toBool(); t.volume = o["volume"].toDouble(1); t.height = o["height"].toInt(56);
    return t;
}

const MediaItem *Project::mediaById(const QString &id) const { for (auto &m : media) if (m.id == id) return &m; return nullptr; }
MediaItem *Project::mediaById(const QString &id) { for (auto &m : media) if (m.id == id) return &m; return nullptr; }
const Clip *Project::clipById(const QString &id) const { for (auto &c : clips) if (c.id == id) return &c; return nullptr; }
Clip *Project::clipById(const QString &id) { for (auto &c : clips) if (c.id == id) return &c; return nullptr; }
const Track *Project::trackById(const QString &id) const { for (auto &t : tracks) if (t.id == id) return &t; return nullptr; }
Track *Project::trackById(const QString &id) { for (auto &t : tracks) if (t.id == id) return &t; return nullptr; }
int Project::trackIndex(const QString &id) const { for (int i = 0; i < tracks.size(); ++i) if (tracks[i].id == id) return i; return -1; }

QVector<const Clip *> Project::clipsOnTrack(const QString &trackId) const {
    QVector<const Clip *> v;
    for (auto &c : clips) if (c.trackId == trackId) v.push_back(&c);
    std::sort(v.begin(), v.end(), [](const Clip *a, const Clip *b) { return a->start < b->start; });
    return v;
}
double Project::duration() const {
    double d = 0;
    for (auto &c : clips) d = std::max(d, c.end());
    return d;
}
int Project::usageCount(const QString &mediaId) const {
    int n = 0;
    for (auto &c : clips) if (c.mediaId == mediaId) ++n;
    return n;
}
QString Project::firstTrackOfType(TrackType t) const {
    for (auto &tr : tracks) if (tr.type == t) return tr.id;
    return {};
}
void Project::ensureDefaultTracks() {
    if (!tracks.isEmpty()) return;
    auto add = [&](const QString &n, TrackType t) { Track tr; tr.id = newId("t"); tr.name = n; tr.type = t; tracks.push_back(tr); };
    add("Titles", TrackType::Title);
    add("Video 2", TrackType::Video);
    add("Video 1", TrackType::Video);
    add("Audio 1", TrackType::Audio);
    add("Music", TrackType::Music);
    add("Voice-over", TrackType::Voice);
}
double Project::resolveOverlap(const QString &trackId, double start, double dur, const QString &ignoreClip) const {
    start = std::max(0.0, start);
    QVector<const Clip *> others;
    for (auto *c : clipsOnTrack(trackId)) if (c->id != ignoreClip) others.push_back(c);
    auto free = [&](double s) {
        for (auto *c : others) if (s < c->end() - 1e-6 && s + dur > c->start + 1e-6) return false;
        return true;
    };
    if (free(start)) return start;
    QVector<double> cand;
    for (auto *c : others) { cand.push_back(c->end()); if (c->start - dur >= 0) cand.push_back(c->start - dur); }
    double best = -1;
    for (double s : cand) if (free(s) && (best < 0 || std::fabs(s - start) < std::fabs(best - start))) best = s;
    return best < 0 ? start : best;
}

QJsonObject Project::toJson(const QString &projectFilePath) const {
    QJsonObject o;
    o["format"] = "mveditor-project"; o["version"] = kFormatVersion; o["name"] = name;
    QJsonObject s;
    s["width"] = settings.width; s["height"] = settings.height; s["fps"] = settings.fps;
    s["sampleRate"] = settings.sampleRate; s["channels"] = settings.channels;
    o["settings"] = s;
    QDir base = projectFilePath.isEmpty() ? QDir() : QFileInfo(projectFilePath).absoluteDir();
    QJsonArray m;
    for (auto mi : media) {
        if (!projectFilePath.isEmpty()) mi.relPath = base.relativeFilePath(mi.path);
        m.push_back(mi.toJson());
    }
    o["media"] = m;
    QJsonArray t; for (auto &x : tracks) t.push_back(x.toJson());
    o["tracks"] = t;
    QJsonArray c; for (auto &x : clips) c.push_back(x.toJson());
    o["clips"] = c;
    QJsonArray mk;
    for (auto &x : markers) { QJsonObject j; j["time"] = x.time; j["name"] = x.name; mk.push_back(j); }
    o["markers"] = mk;
    return o;
}

Project Project::fromJson(const QJsonObject &o, const QString &projectFilePath, QString *error) {
    Project p;
    if (o["format"].toString() != "mveditor-project") { if (error) *error = "Not an M Veditor project file."; return p; }
    if (o["version"].toInt() > kFormatVersion) { if (error) *error = "This project was created by a newer version of M Veditor."; return p; }
    p.name = o["name"].toString(p.name);
    auto s = o["settings"].toObject();
    p.settings.width = s["width"].toInt(1920); p.settings.height = s["height"].toInt(1080);
    p.settings.fps = s["fps"].toDouble(30); p.settings.sampleRate = s["sampleRate"].toInt(48000);
    p.settings.channels = s["channels"].toInt(2);
    QDir base = QFileInfo(projectFilePath).absoluteDir();
    for (auto v : o["media"].toArray()) {
        MediaItem m = MediaItem::fromJson(v.toObject());
        if (!QFileInfo::exists(m.path) && !m.relPath.isEmpty()) {
            QString alt = QDir::cleanPath(base.absoluteFilePath(m.relPath));
            if (QFileInfo::exists(alt)) m.path = alt;
        }
        m.missing = !QFileInfo::exists(m.path);
        p.media.push_back(m);
    }
    for (auto v : o["tracks"].toArray()) p.tracks.push_back(Track::fromJson(v.toObject()));
    for (auto v : o["clips"].toArray()) p.clips.push_back(Clip::fromJson(v.toObject()));
    for (auto v : o["markers"].toArray()) { Marker m; m.time = v.toObject()["time"].toDouble(); m.name = v.toObject()["name"].toString(); p.markers.push_back(m); }
    p.ensureDefaultTracks();
    return p;
}

Project Project::singleClip(const MediaItem &m, double in, double out) {
    Project p;
    p.name = m.name;
    p.media.push_back(m);
    p.settings.width = m.width > 0 ? (m.width & ~1) : 1920;
    p.settings.height = m.height > 0 ? (m.height & ~1) : 1080;
    p.settings.fps = m.fps > 0 ? m.fps : 30;
    p.settings.sampleRate = m.sampleRate > 0 ? m.sampleRate : 48000;
    p.settings.channels = m.channels == 1 ? 1 : 2;
    Track t; t.id = "t1"; t.name = "Track"; t.type = (m.kind == "audio") ? TrackType::Audio : TrackType::Video;
    p.tracks.push_back(t);
    Clip c; c.id = "c1"; c.mediaId = m.id; c.trackId = t.id; c.start = 0; c.in = std::max(0.0, in);
    c.out = out < 0 ? m.duration : out;
    p.clips.push_back(c);
    return p;
}

} // namespace mv
