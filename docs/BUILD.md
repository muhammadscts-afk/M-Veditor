# Building M Veditor (developers)

M Veditor is a C++17 / Qt 6 application. Editing and rendering are performed by FFmpeg; the Windows package bundles `ffmpeg.exe` and `ffprobe.exe` so end users need nothing installed.

## Project layout

```
src/core/   Qt-Core-only engine: project model (JSON), ffprobe reader, RenderGraph (project -> FFmpeg filter graph),
            ExportJob (background render, progress, cancel, verification), waveform/silence/thumbnail helpers
src/ui/     Qt Widgets interface: main window, timeline, preview monitor, media library, properties, export, dialogs
tests/      engine_tests (real FFmpeg renders + content analysis), gui_smoke (drives the real window headlessly)
scripts/    package-windows.ps1 (build + bundle + self-test + portable zip + installer)
installer/  Inno Setup script
```

## Windows build (release package)

Prerequisites
* Windows 10/11 x64
* Visual Studio 2022 Build Tools (C++ workload) – open a "x64 Native Tools" prompt
* Qt 6.5 or newer for MSVC 2019/2022 64-bit incl. **Qt Multimedia** (e.g. `C:\Qt\6.7.3\msvc2019_64`)
* CMake ≥ 3.21 and Ninja
* Inno Setup 6 (only for the installer; `choco install innosetup`)

```powershell
# from the repository root, in an x64 Native Tools prompt
.\scripts\package-windows.ps1 -QtDir C:\Qt\6.7.3\msvc2019_64            # GPL FFmpeg (H.264/H.265 via x264/x265)
.\scripts\package-windows.ps1 -QtDir C:\Qt\6.7.3\msvc2019_64 -Flavor lgpl   # LGPL FFmpeg, no x264/x265
```

Output in `dist\`:
* `M Veditor\M Veditor.exe` – the application folder (exe, Qt DLLs, `ffmpeg\`, `docs\`)
* `M Veditor Portable.zip` – same folder plus `portable.flag` (settings/cache stay in `data\`)
* `M Veditor Setup.exe` – installer with uninstaller, Start-menu/desktop shortcuts and optional `.mveproj` association

The script also runs `"M Veditor.exe" --selftest` with a minimal PATH to prove that the packaged app works offline using only its bundled files, and fails the build otherwise. Pin `-FfmpegUrl` to a specific BtbN release for reproducible builds.

### GitHub Actions
`.github/workflows/build-windows.yml` runs the Linux test-suite and produces the Windows artifacts (`Setup.exe` + portable zip) on every push; tags `v*` publish a release.

## Manual CMake build (any platform)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64
cmake --build build
```
For development you need `ffmpeg` and `ffprobe` either next to the executable (`./ffmpeg/`), in `MVEDITOR_FFMPEG_DIR`, or on `PATH`. FFmpeg **7.0 or newer** is recommended (older builds work except for very long filter graphs, which need `-filter_complex_script`; this is handled automatically).

On Ubuntu 24.04: `sudo apt install qt6-base-dev qt6-multimedia-dev cmake ninja-build ffmpeg fonts-dejavu-core libgl1-mesa-dev`.

## Tests

```bash
./build/engine_tests                          # ~123 checks, renders real media with FFmpeg and inspects the output
QT_QPA_PLATFORM=offscreen ./build/gui_smoke   # ~61 checks driving the real window; writes /tmp/mveditor_ui.png
"./build/M Veditor" --selftest                # headless packaged-app check (exit code 0 = OK, log in selftest.log)
```

The engine tests verify, among others: trim, 0.1×–16× speed (duration **and** A/V sync **and** picture/sound present), merge, transitions (pixel-level), titles with special characters, PiP/watermark, reverse, mixing/solo/mute with timed audio checks, pitch, EQ/normalize, all export formats, stream copy, cancel (no partial file), hardware-encoder fallback, refusal to overwrite sources, Arabic/space paths, project save/load/relink.

## Design notes

* **Non-destructive**: the project (`.mveproj`, JSON, see `docs/PROJECT_FORMAT.md`) only references media.
* **One render path**: `RenderGraph::build()` produces one FFmpeg command from the project. The same code is used for the final export, the low-resolution preview, proxies, audio extraction and batch conversion.
* **Safe process handling**: FFmpeg is always started with an argument list (never a shell string); title text goes through temporary text files with `expansion=none`; filter values are escaped; inputs are absolute paths.
* **Safe output**: FFmpeg writes `name.mveditor-part.ext`; only after ffprobe verification (streams present, duration within tolerance) is it renamed to the final name. Cancel/failure removes the part file.
* **Console windows**: all subprocesses use `CREATE_NO_WINDOW` on Windows.
* **Threading**: no media work runs on the UI thread; all FFmpeg work is asynchronous `QProcess`.
