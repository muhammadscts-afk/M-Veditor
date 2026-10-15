# M Veditor – User Manual

M Veditor is a video and MP3 editor for Windows 10/11. It works fully offline. Your original files are **never modified**: the project only remembers *which part* of each file you use and *which effects* you applied. Everything is applied to the real media when you export.

## 1. The window

| Area | What it does |
|---|---|
| **Media library** (left) | Your imported video, audio and images. Drag items onto the timeline, double-click to preview. |
| **Preview monitor** (top) | Play, pause, stop, seek, step one frame back/forward. Buttons **Source** / **Timeline** switch what you watch. |
| **Timeline** (bottom) | Tracks: Titles, Video 2, Video 1, Audio 1, Music, Voice-over. Clips live here. |
| **Properties** (right) | Settings of the selected clip: timing, speed, picture, audio, transition, title text. |
| **Export** (right, second tab) | Format, resolution, frame rate, quality, bitrate, audio settings, output folder. |
| **Status bar** | Messages, export progress, estimated time left and errors. **View ▸ Activity log** shows technical details. |

Panels can be resized, moved and closed (**View** menu). **View ▸ Dark / Light theme** switches the look.

## 2. Quick start: trim a video and export it

1. **File ▸ Import media** (Ctrl+I) – or drag files into the library.
2. Drag the video from the library onto **Video 1**.
3. Move the red playhead (click the ruler) to where the part you want ends, press **S** to split, select the unwanted piece and press **Delete**.
4. Open the **Export** tab, pick **MP4 (H.264)**, choose a folder and a file name, click **Export**.
5. When the green "Export complete" message appears, the file has been checked and is playable.

## 3. Editing video

* **Select** a clip by clicking it. **Move** it by dragging (it snaps to other clips, markers and the playhead; switch snapping in **Edit ▸ Snapping**). Clips on one track never overlap – a clip dropped on another is moved to the nearest free place.
* **Trim**: drag the left or right edge of a clip.
* **Split**: put the playhead on the clip and press **S** (splits the selected clip, or every clip under the playhead if nothing is selected).
* **Delete** removes the clip. **Shift+Delete** (ripple delete) also closes the gap.
* **Duplicate** (Ctrl+D), **Copy** (Ctrl+C), **Cut** (Ctrl+X), **Paste** (Ctrl+V at the playhead).
* **Merge clips**: place clips one after another on the same track – they are joined in the export.
* **Reorder**: drag clips to a new position.
* **Images** can be placed on video tracks and are shown for 5 seconds by default; drag their right edge to change the duration (slideshows).
* **Rotate / flip / crop / size / position / opacity** are in **Properties ▸ Picture** (menu Video has shortcuts for rotate and flip).
* **Brightness, contrast, saturation, sharpness, colour balance** – Properties ▸ Picture.
* **Fade in / fade out** – Properties ▸ Clip (fades picture and sound).
* **Reverse** – Video ▸ Reverse (clips up to 3 minutes).
* **Transitions** – select a clip, then **Effects ▸ Transition in**: *Fade through black*, *Cross dissolve*, or a *Wipe* in four directions. Put the clip directly after the previous clip on the same track. The transition length is set in Properties.
* **Titles, subtitles, captions** – **Video ▸ Add title / subtitle / caption** (key **T** for title). Edit the text, size, colour, background box and position in Properties. Right-to-left languages such as Arabic work.
* **Watermark / logo** – **Video ▸ Add watermark** places a PNG in the top-right corner of the top video track; change size, position and opacity in Properties.
* **Picture-in-picture** – select a clip and use **Video ▸ Make picture-in-picture**; it is shrunk into a corner of the top video track and shown over the footage below.

### Tracks
Click **M** (mute), **S** (solo), **L** (lock), **H** (hide) in a track header. Right-click a header to rename, set track volume, change its height, or delete it. **Video ▸ Add track** adds more tracks. Drag the lower edge of a header to resize the track.

### Markers
Press **M** to drop a marker at the playhead. Right-click the ruler to add, rename or delete markers.

## 4. Speed controller (slow motion and fast motion)

Select a clip and choose **Video ▸ Speed controller** (Ctrl+R) or the **Speed** toolbar button.

* Slow motion **0.1× – 0.9×**, normal **1×**, fast motion **1.1× – 16×** (and up to 100×).
* Use the preset buttons or type a custom value.
* The dialog shows the new length. On the timeline the clip becomes shorter or longer; following clips on the track are pushed if needed.
* **Preserve audio pitch** keeps voices natural. Turn it off for the "chipmunk" effect.
* **Audio**: keep it, mute it, or **detach** it to an audio track at its original speed.
* **Play in reverse** is available in the same dialog.

The speed is applied to the real picture and sound during export – it is not just a preview setting. The original file is untouched.

## 5. Audio and MP3 editing

* Import MP3, WAV, AAC/M4A, FLAC, OGG and more. A **waveform** is drawn on the clip.
* **Cut / trim / split / merge / duplicate / rearrange** audio clips exactly like video clips. Use the **Music** and **Voice-over** tracks for background music and narration.
* **Volume** (clip gain) and **track volume** (right-click the track header).
* **Mute / solo** per track or per clip. **Mix** multiple tracks – the export always mixes everything.
* **Normalize loudness**, **fade in/out**, **Bass/Mid/Treble equalizer**, **Compressor**, **Reduce noise**, **Pitch** (semitones, speed unchanged) – Properties ▸ Audio.
* **Remove silence** (Audio ▸ Remove silence): choose a threshold and a minimum length; the clip is cut into the non-silent parts and the gaps are closed.
* **Extract audio from video** (Audio ▸ Extract audio): saves MP3, WAV, AAC or FLAC from a clip or library item. **Detach audio** instead puts the sound on its own track in your project.
* **Record voice-over** (Audio ▸ Record voice-over, Ctrl+Shift+M): choose the microphone, press Record, then Stop. The recording is added at the playhead on the Voice-over track. If Windows blocks the microphone, open *Settings ▸ Privacy & security ▸ Microphone* and allow desktop apps.
* **Convert** between formats: use the Export panel, or **Tools ▸ Batch audio conversion** for many files. Sample rate, channels (mono/stereo) and bitrate are selectable.
* **Media information** (Tools ▸ Media information) shows duration, sample rate, channels, codec and file size.

Original audio files are never overwritten. Exports never replace an existing file without asking.

## 6. Previewing your edit

* **Timeline mode (live)**: while you play or scrub, the monitor shows the top video clip at the playhead with its speed, plus titles. Fast and responsive, but it does **not** show effects (colour, rotation, transitions, overlays) or mix several audio tracks.
* **Render preview** (F5 or the button below the monitor): renders the *whole* timeline – all tracks, effects, transitions, titles and the audio mix – at low resolution and plays it. This is the faithful way to check your edit before the final export.
* **Source mode**: double-click a library item. Use **Mark In / Mark Out** to choose a part, then **Add to timeline**.
* For very large (4K) files, right-click the item ▸ **Create optimized preview (proxy)**. Previews then use the lighter file; exports always use the original.

## 7. Saving, autosave and recovery

* **Save project** (Ctrl+S) writes a `.mveproj` file (documented JSON). It stores clip positions, effects and links to your media – not the media itself. Keep the media where it is, or use **File ▸ Relink missing media** if files were moved. Projects remember media by relative path too, so moving a project folder together with its media keeps everything linked.
* **Autosave** runs every few minutes (Tools ▸ Settings). After a crash or power loss M Veditor offers to recover the last autosave when you start it again.
* **Undo / Redo**: Ctrl+Z / Ctrl+Y (up to 200 steps).
* **File ▸ Open recent** lists recent projects.

## 8. Exporting

Open the **Export** tab (or **Export ▸ Export video / audio**, Ctrl+E).

| Setting | Notes |
|---|---|
| Format | MP4 (H.264), MP4 (H.265/HEVC), WebM (VP9), MOV, and audio: MP3, WAV, AAC (.m4a), FLAC. Formats missing from your FFmpeg build are greyed out. |
| Resolution | Project size, 480p, 720p, 1080p, 1440p, 4K. |
| Frame rate | Project rate, 24, 25, 30, 50, 60 fps. |
| Quality / bitrate | Quality presets (constant quality) or a fixed video bitrate. |
| Audio | Bitrate, sample rate, mono/stereo. |
| Acceleration | Software (always works), automatic hardware, NVIDIA NVENC, Intel Quick Sync, AMD AMF. If the hardware encoder fails, M Veditor **automatically retries in software**. |
| Fast export | For a single unmodified clip you can export **without re-encoding** (original quality, very fast, cuts at the nearest keyframe). The panel tells you whether re-encoding is needed and why. |

While exporting you see a progress bar and the estimated time remaining; **Cancel** stops safely and leaves no partial file. Before starting, M Veditor checks the folder, the file name, the free disk space and that you are not overwriting one of your source files. After rendering it re-opens the result and verifies that the picture and sound exist and the length is correct – **"Export complete" is only shown for a verified file.**

### Batch conversion
**Tools ▸ Batch audio conversion** and **Batch video conversion** convert many files in one go. Originals stay untouched and existing files are never overwritten (a number is added to the name).

## 9. Keyboard shortcuts

| Key | Action | Key | Action |
|---|---|---|---|
| Space | Play / pause | Ctrl+N / O / S | New / Open / Save |
| Left / Right | Previous / next frame | Ctrl+Shift+S | Save as |
| Home / End | Go to start / end | Ctrl+I | Import media |
| Up / Down | Previous / next clip edge | Ctrl+Z / Ctrl+Y | Undo / Redo |
| S | Split at playhead | Ctrl+C / X / V | Copy / Cut / Paste clip |
| Delete / Shift+Delete | Delete / ripple delete | Ctrl+D | Duplicate |
| M | Add marker | T | Add title |
| Ctrl+R | Speed controller | Ctrl+Shift+R | Rotate 90° |
| Ctrl+Shift+D | Detach audio | Ctrl+Shift+M | Record voice-over |
| F5 | Render preview | Ctrl+E / Ctrl+Shift+E | Export video / audio |
| Ctrl + / Ctrl − / Ctrl+0 | Zoom in / out / fit | Ctrl+mouse wheel | Zoom timeline |
| Shift+mouse wheel | Scroll timeline sideways | F1 | This manual |

## 10. Troubleshooting

* **"This file is not supported or is damaged"** – the file is incomplete or in a format FFmpeg cannot read. Try opening it in another player; if it also fails, the file is damaged.
* **A clip shows "(missing)"** – the file was moved or deleted. Use **File ▸ Relink missing media**.
* **Titles do not render / "font could not be loaded"** – the title font (Arial Bold) is missing from Windows; install it or set the environment variable `MVEDITOR_FONT` to a .ttf file.
* **H.264/H.265 is greyed out** – this copy uses the LGPL FFmpeg flavor without x264/x265. Use WebM, a hardware encoder, or the full (GPL) edition.
* **Hardware encoder message** – not every PC has NVENC/QSV/AMF; the app falls back to software automatically.
* **Microphone not found** – check *Settings ▸ Privacy & security ▸ Microphone* and that a device is plugged in.
* **Export says the disk is full** – choose another folder; 4K exports need several GB.
* **Where are my settings?** In your Windows profile. The **portable** edition keeps everything in the `data` folder next to the program.
* **Technical details** for support: *Help ▸ Show activity log*.

## 11. Known limits

* Live timeline preview shows the top video clip only (no effects, no multi-track audio mix). Use **Render preview** to see the full result.
* **Reverse** is limited to clips of 3 minutes (the clip is held in memory).
* Transitions blend a clip into the footage that is already there. For "cross-dissolve between two clips" the first clip's last frame is held during the transition.
* Subtitles are typed in as caption clips; importing .srt files is not included in this version.
* Video plays back through the operating system's codecs for preview; very unusual formats may export fine but not preview.
