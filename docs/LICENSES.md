# Licenses and bundled components

*This text is informational and is not legal advice. If you distribute M Veditor commercially, have your own legal counsel review codec, patent and licensing obligations.*

## M Veditor
The M Veditor source code is released under the **MIT License** (see `LICENSE`).

## Qt 6 (LGPL v3)
M Veditor uses **Qt 6** (Core, Gui, Widgets, Multimedia, MultimediaWidgets) under the **GNU LGPL v3**. Qt is linked **dynamically**: the Qt DLLs shipped in the application folder can be replaced by your own build of the same Qt version. Qt source code and license texts: <https://www.qt.io/download-open-source> and <https://code.qt.io>. The Qt Multimedia FFmpeg backend DLLs are part of the Qt distribution.

## FFmpeg (separate program, GPL or LGPL flavor)
Editing, decoding, encoding and rendering are performed by **FFmpeg** (`ffmpeg.exe`, `ffprobe.exe` in the `ffmpeg` folder). M Veditor starts FFmpeg as a **separate process**; it is not linked into the M Veditor executable. FFmpeg homepage and source: <https://ffmpeg.org>.

Two packaging flavors are supported by `scripts/package-windows.ps1`:

| Flavor | Includes | License of the bundled FFmpeg | Consequence |
|---|---|---|---|
| `gpl` (default) | libx264 (H.264), libx265 (H.265), libvpx (VP9), libopus, libmp3lame, … | **GPL v3** | You must pass on the GPL text and offer the corresponding FFmpeg/x264/x265 source code (see below). |
| `lgpl` | native AAC, libvpx, libopus, libmp3lame, Windows hardware encoders (NVENC/QSV/AMF/Media Foundation) – **no x264/x265** | **LGPL v3** | No source-offer obligation beyond LGPL terms; H.264 export requires a hardware encoder. |

The binaries are downloaded from the community build project **BtbN/FFmpeg-Builds** (<https://github.com/BtbN/FFmpeg-Builds>), which publishes the exact build scripts and links to the sources of FFmpeg and every library in the build. The build used is recorded in `ffmpeg\FFMPEG-SOURCE-INFO.txt` and the license text is stored as `ffmpeg\FFMPEG-LICENSE.txt` inside the application folder.

**Written offer (GPL flavor):** for three years after you obtained this program, the distributor will provide, on request, the complete corresponding source code of the bundled FFmpeg, x264 and x265 builds on a durable medium or download link. The distributor's contact details must be added here when you publish a release.

## Component list

| Component | Version (release build) | License | Used for |
|---|---|---|---|
| Qt 6 | 6.7.x | LGPL v3 (GPL v2/v3 / commercial available) | User interface, media preview, application framework |
| FFmpeg | 7.1.x (BtbN build) | GPL v3 or LGPL v3 (flavor-dependent) | Decoding, effects, mixing, encoding, probing |
| x264 | in FFmpeg GPL build | GPL v2+ | H.264 encoding |
| x265 | in FFmpeg GPL build | GPL v2+ | H.265/HEVC encoding |
| libvpx | in FFmpeg build | BSD-3-Clause | VP9 / WebM encoding |
| Opus (libopus) | in FFmpeg build | BSD-3-Clause | WebM audio |
| LAME (libmp3lame) | in FFmpeg build | LGPL v2+ | MP3 encoding |
| FreeType, HarfBuzz, FriBidi | in FFmpeg build | FTL/BSD/MIT/LGPL | Title rendering (incl. right-to-left text) |
| Microsoft Visual C++ Runtime | 14.x | Microsoft redistributable terms | Required by the compiled program (installed by windeployqt) |
| Inno Setup | 6.x | Inno Setup License | Creates the installer (build-time only) |

## Codec patents
H.264/AVC, H.265/HEVC, AAC and MP3 are covered by patent pools in some jurisdictions. Software encoders such as x264/x265 are provided without patent licenses. The distributor of the application is responsible for obtaining any licenses required in the countries where it is distributed or sold. The `lgpl` flavor combined with hardware encoders delegates H.264/HEVC encoding to the user's GPU vendor drivers.

## What is *not* included
No proprietary codecs, no telemetry, no network update checker and no third-party online services are included.
