# M Veditor – User Manual

M Veditor is a video and MP3 editor for Windows 10/11. It works fully offline. Your original files are **never modified**: the project only remembers *which part* of each file you use and *which effects* you applied. Everything is applied to the real media when you export.

## 1. The window

| Area | What it does |
|---|---|
| **Media library** (left) | Your imported video, audio and images. Buttons: **Import | Remove** on the first row, **Rename | All** (type filter) on the second; the search box is under the list. Drag items onto the timeline, double-click to preview. |
| **Preview monitor** (top) | Play, pause, stop, seek, step one frame back/forward. Buttons **Source** / **Timeline** switch what you watch. |
| **Timeline** (bottom) | Tracks: Titles, Video 1, Video 2, Audio 1, Music, Voice-over. Clips live here. The **＋ Add track ▾** button above the track names adds more. |
| **Properties** (right) | Settings of the selected clip: timing, speed, picture, audio, transition, title text. |
| **Export** (right, second tab) | Format, resolution, frame rate, quality, bitrate, audio settings, output folder. |
| **Status bar** | Messages, export progress, estimated time left and errors. **View ▸ Activity log** shows technical details. |

The controls under the picture are on one line (play controls and time on the left; In/Out/Add appear in Source mode; Source, Timeline, Live, Render on the right). Long file names in Properties wrap onto several lines. Hover any button for a dark tip with white text. The Properties panel is laid out so that every control (number boxes with their arrows, the Speed button, the Transition box, long names and option texts) stays fully visible even at its narrowest width; long texts wrap onto several lines. The Media library and Properties panels start at their narrowest width so the picture area is as large as possible; drag their edge if you want them wider. Panels can be resized, moved and closed (**View** menu). **View ▸ Dark / Light theme** switches the look.

## 2. Quick start: trim a video and export it

1. **File ▸ Import media** (Ctrl+I) – or drag files into the library.
2. Drag the video from the library onto **Video 1**.
3. Move the red playhead (click the ruler) to where the part you want ends, press **S** to split, select the unwanted piece and press **Delete**.
4. Open the **Export** tab, pick **MP4 (H.264)**, choose a folder and a file name, click **Export**.
5. When the green "Export complete" message appears, the file has been checked and is playable.

## 3. Editing video

* **Select** a clip by clicking it. **Select several**: Ctrl+click (or Shift+click) adds/removes clips, drag a box on an empty part of a track to select everything it touches, or press Ctrl+A for all. Moving, deleting, duplicating, copying, splitting, speed, transitions and every value in Properties then apply to **all selected clips** (the one with the yellow outline is the main one shown in Properties).
* **Move** a clip by dragging (it snaps to other clips, markers and the playhead; switch snapping in **Edit ▸ Snapping**). Clips on one track never overlap – a clip dropped on another is moved to the nearest free place.
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
* **Titles, subtitles, captions** – **Video ▸ Add title / subtitle / caption** (key **T** for title), or **right-click an empty spot on a title track** and choose *Add title / subtitle / caption here* – the new text starts exactly where you clicked. Edit the text (full-width box), size, colours, background box and position in Properties. **Font…** chooses the font type (family, bold, italic from your installed Windows fonts) and **Effects…** adds outline, shadow, ALL CAPITALS and text opacity. Right-to-left languages such as Arabic work.
* **Watermark / logo** – **Video ▸ Add watermark** places a PNG in the top-right corner of the top video track; change size, position and opacity in Properties.
* **Picture-in-picture** – select a clip and use **Video ▸ Make picture-in-picture**; it is shrunk into a corner of the top video track and shown over the footage below.

### Tracks
* **Add** tracks with the **＋ Add track ▾** button above the track names (video, audio, music, voice-over, titles). New video tracks are numbered in ascending order downwards (Video 1, Video 2, Video 3 …).
* **Reorder** tracks by dragging a track header up or down (a yellow line shows where it will land), or right-click ▸ **Move track up / down**.
* **Layering:** video tracks *lower in the list are drawn on top of* those above them – so Video 2 covers Video 1. Title tracks are always drawn on top of all video. Use this for overlays, picture-in-picture and watermarks.
* **Clips can move between tracks** by dragging up or down: video clips between video tracks (or onto an audio track to use only their sound), audio clips between audio/music/voice-over tracks, titles between title tracks.
* **M (Mute)** silences the track. **S (Solo)** means *"listen to this track only"*: clicking S on a track soloes it and **switches off the S of every other track**, so only one track is solo at a time (Ctrl+click S to solo several tracks). While a track is soloed every other track is silent (preview and export). On a video or title track Solo also shows only the soloed picture tracks. Click S again to turn it off – the track header says "silent (solo)" for tracks that are being skipped. **L (Lock)** protects a track's clips. **H (Hide)** hides a track's picture only – its sound keeps playing in the preview and the export. In live playback M and S also silence the video's own sound immediately.
* Hover any button for a tip. Right-click a header to rename, set track volume, change height, move or delete. Drag the lower edge of a header to resize the track.

### Gaps (empty spaces on a track)
When you cut segments and move them, empty spaces ("gaps") stay behind – before the first clip of a track and between clips. Like in Clipchamp:
* **Hover a gap** with the mouse: it is shown hatched with a red **bin button** in the middle. **Click the bin** to delete the gap – the clips after it on that track slide back to close it (other tracks are not touched). Ctrl+Z undoes it.
* **Right-click a gap** for **Delete this gap** or **Delete all gaps in <track>**, which packs all clips of that track one after another from the start of the track.

### Markers and colour between them
* **Markers belong to one track.** The *active track* is the one you clicked last (its header gets a blue bar). Press **M** to drop a marker at the playhead on the active track, or right-click the ruler / an empty spot of a track ▸ **Add marker here**. You can have markers on as many tracks as you like; each marker's line and tint appear **only on its own track**.
* **Pairs:** on each track the markers are used in pairs in time order: the 1st and 2nd (**Ma**, **Mp**), the 3rd and 4th (**Ma**, **Mp**), and so on. The area **between the two markers of a pair** is tinted with a very transparent colour. Nothing is tinted between pairs (between Mp and the next Ma), before the first marker, or after a last unpaired Ma – that one waits for its Mp.
* **Names** are automatic (Ma, Mp, Ma, Mp …) and follow the time order, so they always make sense even if you add a marker earlier on the track. Use *Rename…* for a custom name.
* The colour is drawn as a clearly visible but see-through layer **on top of the clips** (the clips' own background colour is very transparent) between Ma and Mp (only on that marker's track), so you can see it over the video too.
* **Right-click a marker head** (the little flag on the track or on the ruler) to rename or delete it or to pick the colour of its pair: **bright green, bright yellow, bright orange, bright light blue**, pink, violet, red, *Other colour…* or *Automatic*. A new colour replaces the old one completely – colours are never mixed.
* Turn the tint off in **View ▸ Colour regions between markers** (the marker lines stay).

## 4. Speed controller (slow motion and fast motion)

Select a clip and choose **Video ▸ Speed controller** (Ctrl+R) or the **Speed** toolbar button.

* Slow motion **0.1× – 0.9×**, normal **1×**, fast motion **1.1× – 16×** (and up to 100×).
* Use the preset buttons or type a custom value.
* The dialog shows the new length. On the timeline the clip becomes shorter or longer; following clips on the track are pushed if needed.
* **Preserve audio pitch** keeps voices natural – in the export **and** in live playback (the sound of a faster or slower clip is time-stretched, so a commentator does not turn into a child's voice). Turn it off for the "chipmunk" effect.
* **Audio**: keep it, mute it, or **detach** it to an audio track at its original speed.
* **Play in reverse** is available in the same dialog.

**Keyboard:** select a clip and press **]** (**د** on the Arabic keyboard) to make it 0.1× faster or **[** (**ج**) to make it 0.1× slower. Each press changes the speed by 0.1× (holding the key repeats gently; the sound is muted while the speed is changing and comes back once it settles), many presses count as **one** undo step, and the picture stays on the same frame (the red playhead moves along the timeline instead, because the whole clip becomes shorter or longer). If several clips are selected they all change; the frame is kept for the selected clip that is under the playhead. The keys do nothing while you are typing in a text or number box (press Enter in a box to leave it so the keys work again). The arrow, comma/period and bracket keys work with both the English and the Arabic keyboard layout.

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

* **Full screen:** **double-click the picture** (or press **F11**, or View ▸ Full screen preview). The picture fills the screen. The control bar (play, volume, time, seek bar, **A B**, exit) appears when you move the mouse and hides again, together with the mouse pointer, after about 2.5 seconds of stillness. Click or drag the bar to seek, **Space** plays/pauses, the arrow and comma/period keys work as usual, **Esc** or another double-click leaves full screen.
* **Volume:** the speaker button next to the time mutes/unmutes and the little slider sets the preview volume (also in full screen). It affects everything you hear in the preview – timeline, source and rendered preview – but never the exported file.
* **A–B repeat (like VLC):** the **A B** button next to the time. Click once to mark the start **A** at the current position, click again to mark the end **B** – the part between them now repeats (a yellow band shows it on the seek bar). Click the button once more to stop repeating. Works for the timeline and for a source; seeking elsewhere does not cancel it.
* **Speed while playing the source:** open a library item (double-click) and the **Speed** icon / Ctrl+R / **]** and **[** (**د** / **ج**) change the *preview speed* of the source from 0.1× to 16×, with the voice kept natural. The current speed is shown at the top right of the picture and resets to 1× when you open another item.


* **Live playback (default, no rendering):** press **Space** and the timeline plays immediately while you edit. Cuts between clips are gap-free (the next clip is loaded in advance), the red playhead follows the picture, and **audio, music and voice-over tracks play too** (with their volume, mute and solo). Live playback is *approximate*: colour effects, transitions, rotation/crop and picture-in-picture overlays are not drawn (a small note appears on the picture when your project uses them), the sound of lower video layers is not played, and effects such as EQ or noise reduction are not applied. Titles are shown, including font, outline and shadow.
* **Render** (button under the picture, or F5): optional. Renders an *exact* low-resolution preview of the whole timeline – every effect, transition, title and the full mix – and plays it. For long projects this takes time, so use it only when you want to check the final look; it never runs by itself in Live mode. **Stop** cancels a render in progress.
* **Live off:** if you switch the **Live** button off, Play always plays the exact rendered preview (it renders first when the edit has changed). A single unmodified video clip is played directly from the file with no render.
* **Click or drag anywhere on the blue seek bar** under the picture to jump to that point. Scrubbing the timeline ruler shows the picture at the playhead.
* **Source mode:** double-click a library item. Use **In / Out** to choose a part, then **Add to timeline**.
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
| **Up / Down** | **Next / previous frame** | Ctrl+Shift+S | Save as |
| **Right / Left** | **Jump 5 seconds forward / back** | Ctrl+I | Import media |
| **. (ز) / , (و)** | **Jump 1 second forward / back** | Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z) | Undo / Redo – also on the Arabic keyboard layout |
| F11 | Full screen preview |
| Home / End | Go to the start / end | Ctrl+A | Select all clips |
| **] (د) or =** / **[ (ج) or -** | **Speed +0.1× / −0.1× on the selected clip(s)** | | |
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
* **A title uses a different font than I chose** – some fonts (for example font collections .ttc) cannot be used by the renderer and fall back to the default bold font. Choose another font. **Titles do not render / "font could not be loaded"** – install Arial or set the environment variable `MVEDITOR_FONT` to a .ttf file.
* **H.264/H.265 is greyed out** – this copy uses the LGPL FFmpeg flavor without x264/x265. Use WebM, a hardware encoder, or the full (GPL) edition.
* **Hardware encoder message** – not every PC has NVENC/QSV/AMF; the app falls back to software automatically.
* **Microphone not found** – check *Settings ▸ Privacy & security ▸ Microphone* and that a device is plugged in.
* **Export says the disk is full** – choose another folder; 4K exports need several GB.
* **Where are my settings?** In your Windows profile. The **portable** edition keeps everything in the `data` folder next to the program.
* **Technical details** for support: *Help ▸ Show activity log*.

## 11. Known limits

* Live playback is instant but approximate (see section 6). Use **Render** to see the exact result before exporting.
* **Reverse** is limited to clips of 3 minutes (the clip is held in memory).
* Transitions blend a clip into the footage that is already there. For "cross-dissolve between two clips" the first clip's last frame is held during the transition.
* Subtitles are typed in as caption clips; importing .srt files is not included in this version.
* Video plays back through the operating system's codecs for preview; very unusual formats may export fine but not preview.
