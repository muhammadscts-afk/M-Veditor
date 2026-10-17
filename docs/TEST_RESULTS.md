# Test results

Be clear about what has and has not been verified. The development container is Linux, so the **Windows executable has not been compiled or run by the author of this source drop**. Everything that does not depend on Windows itself was tested for real.

## Verified (Linux build of the same source, Qt 6.4.2, FFmpeg 6.1.1, real media)

| Suite | Result | What it proves |
|---|---|---|
| `engine_tests` | **123 checks, 0 failures** | The editing engine produces correct, playable media (details below) |
| `gui_smoke` | **59 checks, 0 failures** | The real main window works: import via probe pipeline, drag-drop to timeline with overlap handling, split, undo/redo, effects, speed, titles, markers, detach audio, copy/paste, **multi-selection (select, toggle, delete, duplicate, select all)**, **Add-track button and track order/reordering**, **click-to-seek bar**, **Play renders and plays a full preview and becomes stale when the project is edited**, **marker colour regions**, **markers/track renames never invalidate a preview**, **direct play of a single clip without rendering**, **tooltip look (dark grey, translucent, white text)**, **live playback without any render job and a monotonic, smooth playhead**, **docks start at their content-limited minimum width**, save/reopen, export through the UI job runner, dialogs shown |
| `M Veditor --selftest` | **PASS** | Headless self-test (bundled-FFmpeg discovery, probe, trim + 2× speed export, MP3 extraction, WAV export) – this is also run by the Windows packaging script on the packaged app |

### Engine test coverage (all outputs re-opened with ffprobe and analysed)
* **Trim** – duration, stream presence, A/V sync (≤0.15 s), output not black (luma), audio audible (volume analysis)
* **Speed** 0.1×, 0.25×, 0.5×, 2×, 4×, 8×, 16× – output duration matches `source/speed`, A/V sync within 0.25 s, picture and sound present
* **Merge** two clips of different resolution/frame rate/sample rate; sync checked
* **Track layering, solo and hide** – pixel-level: a later video track covers an earlier one, hiding the top track reveals the one below, soloing a video track shows only that track, soloing an audio track keeps the picture
* **Transitions** – pixel-level checks with flat dark/light clips: dissolve mid-frame is a blend (≈126), wipe-left / wipe-right reveal the correct side, fade dips to black; all six types render
* **Effects chain** (rotate, flip, crop, brightness/contrast/saturation, sharpen, colour balance, fades), **picture-in-picture + watermark with opacity**, **reverse**, **image slideshow**
* **Title fonts and effects** – font file lookup by family/bold/italic, outline, shadow, capitals and opacity render and are visible in the picture; styles survive save/load
* **Titles** with quotes, colons, `%{pts}`, backslashes, brackets, commas, multi-line and Arabic text (no filter-injection)
* **Audio** – 3-track mix with voice-over delayed to 1.0 s (timed level checks), **solo** isolates the voice track, **mute** silences tracks, normalize/compressor/denoise/EQ, mono + 44.1 kHz honoured, independent **pitch shift** keeps length, speed without pitch preservation, fades
* **Extract audio** to MP3 / WAV / AAC / FLAC; **formats**: H.264, H.265, WebM (VP9/Opus), MOV, 480p/60 fps/fixed bitrate
* **Lossless stream copy** (video and MP3), correct "re-encode required" reasons
* **Safety** – refuses to overwrite a source file (file verified untouched), missing media reported, corrupt file rejected with a readable message, empty timeline rejected, oversized reverse rejected
* **Paths** – source and output with spaces and Arabic characters
* **Cancel** – export stopped, no partial or final file left behind
* **Hardware acceleration** – `auto` on a machine without GPU encoders falls back to software and still produces a verified file
* **Silence detection**, **waveform peaks**, **thumbnails**; **project JSON** round-trip, relative-path relink, missing media flagged, foreign files rejected, overlap resolution

## Not verified here (must be done on Windows)

* Compilation with MSVC and linking against the Windows Qt kit (the code is plain C++17/Qt 6; Windows-only pieces are the `CREATE_NO_WINDOW` flag, the "start with Windows" registry key and `windeployqt` packaging).
* The Inno Setup installer, uninstall and file association.
* Behaviour on a clean Windows 10/11 machine/VM, offline.
* Real hardware encoders (NVENC / Quick Sync / AMF) – only the automatic fallback path was exercised.
* Microphone recording (no audio device in the test environment) and audible live playback.
* Visual layout at other screen resolutions / DPI scaling (the window now fits the available screen and maximizes when no valid saved size exists, but this was only verified in the offscreen test environment).
* **Gap-free cuts and audio-track playback in live mode** (second preloaded player + audio voices) – the logic runs in the tests but real decoders/audio devices were not available; judge smoothness on your PC.
* **Crisp menu text on Windows** (menus now use solid styling and no fade effects; the original haze could only be seen on your PC).
* **Smoothness of playback and the playhead on real hardware** – the preview player is a standard video file played by Qt Multimedia and the playhead is interpolated at 60 Hz with a cached timeline background, but actual frame pacing could only be measured on your PC.

## Stage-5 checklist for a clean Windows 10/11 VM

Install from `M Veditor Setup.exe` (or unzip the portable build) on a VM with **no** Python, Node.js, FFmpeg, Visual Studio or Qt installed, and disconnect the network. Tick each item:

- [ ] Double-click starts the app; no console window flashes; icon and version info show in Explorer
- [ ] `M Veditor.exe --selftest` returns exit code 0 (`selftest.log` shows all PASS)
- [ ] Import an MP4, MOV, MKV, WebM, AVI and an MP3; metadata (Tools ▸ Media information) is correct
- [ ] Trim an MP4, add a title, speed a section to 0.5× and another to 4×, export MP4 – plays in Windows Media Player / Movies & TV; picture and sound stay in sync
- [ ] Trim an MP3, fade in/out, export MP3 – plays correctly; extract audio from an MP4 as MP3
- [ ] Record a voice-over (microphone permission prompt handled), mix with music, export
- [ ] Save project, close, reopen; move a media file and use Relink missing media
- [ ] Kill the process during editing, restart – recovery prompt appears and restores the project
- [ ] Start an export and press Cancel – no partial file remains; interrupt an export by disconnecting a USB drive – readable error shown
- [ ] File names with spaces and non-English letters (e.g. Arabic) for sources, outputs and the project file
- [ ] Rename a source file after import, export → "Source file is missing" message (no crash)
- [ ] Hardware acceleration "Automatic" on a PC with a GPU encoder, and on one without (falls back)
- [ ] Uninstall removes the program; portable build leaves nothing outside its folder
