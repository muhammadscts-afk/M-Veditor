# M Veditor project format (`.mveproj`)

UTF-8 JSON. All times are seconds (floating point). The file never contains media data.

```jsonc
{
  "format": "mveditor-project",      // required marker
  "version": 1,                      // files from a newer version are refused
  "name": "Holiday",
  "settings": { "width": 1920, "height": 1080, "fps": 30, "sampleRate": 48000, "channels": 2 },
  "media": [
    { "id": "m1a2b3c4d5", "name": "beach.mp4", "kind": "video",   // video | audio | image
      "path": "C:/Videos/beach.mp4",            // absolute path
      "relPath": "../Videos/beach.mp4",         // path relative to the project file (used when 'path' is missing)
      "duration": 12.48, "width": 1920, "height": 1080, "fps": 29.97,
      "hasVideo": true, "hasAudio": true, "sampleRate": 48000, "channels": 2,
      "sizeBytes": 23456789, "bitrate": 15000000,
      "videoCodec": "h264", "audioCodec": "aac", "container": "mov,mp4,m4a,3gp,3g2,mj2" }
  ],
  "tracks": [                         // listed top to bottom. Video tracks later in the list are composited ON TOP of earlier ones;
                                      // title tracks are always composited above all video tracks
    { "id": "t1", "name": "Video 1", "type": "video",   // video | audio | music | voice | title
      "muted": false, "solo": false, "locked": false, "hidden": false, "volume": 1.0, "height": 56 }
  ],
  "clips": [
    { "id": "c1", "trackId": "t1", "mediaId": "m1a2b3c4d5",   // mediaId empty => title clip
      "start": 0.0,                   // position on the timeline
      "in": 2.0, "out": 8.0,          // used part of the source; timeline length = (out-in)/fx.speed
      "label": "",
      "fx": {
        "speed": 1.0, "preservePitch": true, "reverse": false,
        "brightness": 0, "contrast": 1, "saturation": 1, "sharpness": 0,
        "balR": 0, "balG": 0, "balB": 0,
        "rotation": 0, "flipH": false, "flipV": false,
        "cropL": 0, "cropT": 0, "cropR": 0, "cropB": 0,          // fractions 0..0.45
        "scale": 1.0, "posX": 0.5, "posY": 0.5, "opacity": 1.0,  // scale 1 = fit frame; <1 = picture-in-picture
        "fadeIn": 0, "fadeOut": 0,
        "volume": 1.0, "mute": false, "normalize": false, "compressor": false, "denoise": false,
        "eqBass": 0, "eqMid": 0, "eqTreble": 0, "pitchSemitones": 0,
        "transition": "none",         // none | fade | dissolve | wipeleft | wiperight | wipeup | wipedown
        "transitionDur": 1.0
      },
      "title": { "text": "Hello", "fontSize": 64, "color": "#ffffff", "box": true, "boxColor": "#000000",
                 "boxOpacity": 0.5, "posX": 0.5, "posY": 0.85, "kind": "title" }   // only for title clips
    }
  ],
  "markers": [ { "time": 4.2, "name": "M1", "color": "#4285f4" } ]   // color optional: tint of the region starting at the marker
}
```

**Rendering rules**
* Video tracks are composited in list order: Video 1 first, later tracks (lower in the list) overlay earlier ones. Title tracks are drawn on top of everything. Projects saved by version 1.0.0 of M Veditor used the opposite order; re-arrange the tracks if overlays look reversed.
* Audio of every audible clip on every track (including the sound of video clips) is mixed. If any track has `solo`, only solo tracks are audible; if a video/title track has `solo`, only soloed video/title tracks are shown. `muted` tracks are silent; `hidden` hides picture only.
* Clips on one track must not overlap; the editor enforces this.
* A project must contain a track for each clip's `trackId`; missing files are flagged and can be relinked.

**Autosave** files use the same format with an extra `_originalPath` key (ignored by the loader).
