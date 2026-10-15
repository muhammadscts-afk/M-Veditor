# M Veditor – standalone video & MP3 editor for Windows

C++17 / Qt 6 desktop application. All editing is executed on the **real media** by a bundled FFmpeg, so the result of Export is what you get – speed changes, trims, effects, transitions, titles and the audio mix are rendered, not simulated. No Python, Node.js or installed FFmpeg is needed by end users; the app works offline and sends no data.

**Features**: multitrack timeline (video, overlay, titles, audio, music, voice-over) with snapping, trim/split/move/duplicate, undo/redo, waveforms · speed controller 0.1×–16× (pitch preserving) · transitions (fade, dissolve, 4 wipes) · titles/subtitles/captions, watermark, picture-in-picture · colour/sharpness/crop/rotate/flip · audio: normalize, EQ, compressor, noise reduction, pitch, silence removal, mixing, mute/solo, microphone recording · export MP4 (H.264/H.265), WebM, MOV, MP3, WAV, AAC, FLAC with verification, cancel, ETA, hardware acceleration with automatic software fallback, lossless "fast export" · batch audio/video conversion · autosave & crash recovery · relinking · dark/light theme · portable mode.

Docs: [`docs/USER_MANUAL.md`](docs/USER_MANUAL.md) · [`docs/BUILD.md`](docs/BUILD.md) · [`docs/PROJECT_FORMAT.md`](docs/PROJECT_FORMAT.md) · [`docs/LICENSES.md`](docs/LICENSES.md) · [`docs/TEST_RESULTS.md`](docs/TEST_RESULTS.md)

Windows binary: `scripts/package-windows.ps1` (or the GitHub Actions workflow) produces `M Veditor.exe` (app folder), `M Veditor Portable.zip` and `M Veditor Setup.exe`.
