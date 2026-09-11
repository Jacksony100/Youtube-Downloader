# Third-party components

Video Downloader Pro uses Qt dynamically and runs yt-dlp, Deno, FFmpeg and ffprobe
as separate processes. Those components retain their respective copyrights and
license terms. This notice does not assign a license to Video Downloader Pro.

## Components and source locations

| Component | Local release preparation inspected | License / upstream source |
| --- | --- | --- |
| Qt Core, Gui, Widgets, Network and deployed plugins | Qt 6.8.3 | LGPL-3.0 / GPL-3.0 or a valid commercial Qt license, depending on the component and distribution; [Qt licensing](https://doc.qt.io/qt-6/licensing.html), [6.8.3 source](https://download.qt.io/archive/qt/6.8/6.8.3/submodules/) |
| yt-dlp standalone executable | 2026.08.19 | yt-dlp source is Unlicense; bundled standalone executables contain GPL-3.0-or-later components. [Release-specific third-party notices](https://github.com/yt-dlp/yt-dlp/blob/2026.08.19/THIRD_PARTY_LICENSES.txt), [source](https://github.com/yt-dlp/yt-dlp/tree/2026.08.19) |
| Deno | 2.9.6 | MIT for Deno; dependencies have their own terms. [License](https://github.com/denoland/deno/blob/v2.9.6/LICENSE.md), [source](https://github.com/denoland/deno/tree/v2.9.6) |
| FFmpeg and ffprobe, Windows Gyan essentials build | 9.0.1 | GPL enabled in this distributor's build; [distributor](https://www.gyan.dev/ffmpeg/builds/), [FFmpeg licensing](https://ffmpeg.org/legal.html), [source](https://ffmpeg.org/releases/ffmpeg-9.0.1.tar.xz) |
| FFmpeg and ffprobe, macOS Evermeet Intel build | 9.0.1 | Build-specific FFmpeg and linked-library terms apply; [distributor and library inventory](https://evermeet.cx/ffmpeg/), [FFmpeg source](https://ffmpeg.org/releases/ffmpeg-9.0.1.tar.xz) |
| Microsoft Visual C++ runtime (Windows MSVC packages) | As selected by windeployqt from the build toolchain | [Microsoft redistribution terms and files](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution) |
| Inno Setup installer engine | 6.7.3 in local packaging | [Inno Setup license](https://github.com/jrsoftware/issrc/blob/is-6_7_3/license.txt) |

`licenses/` includes the inspected upstream license texts and yt-dlp dependency
notices. Packaging also includes the installed Qt SDK's SPDX inventory when
available. This inventory may include Qt modules not deployed into this package;
the deployed DLL/framework list determines the actual subset.

## Runtime provenance

The package's `toolchain/manifest.json` records each executable's actual version,
binary SHA256, verified archive SHA256 and source URL. The shared
`runtime/toolchain-lock.json` records the integrity policy used by both runtime
updates and packaging. Runtime updates can change installed versions, so the
application's diagnostics and active runtime manifest are authoritative afterward.

Windows FFmpeg archives are verified against the distributor's SHA256 endpoint.
The macOS FFmpeg 9.0.1 ZIP pins were calculated from downloaded bytes after
verification of the distributor's detached signatures against published primary
fingerprint `20F6 EA3E 0CFD 6B4C 5344 7A73 476C 4B61 1A66 0874`.
The Evermeet binaries target Intel; an Apple Silicon build needs Rosetta to run
these particular runtime binaries.

## Distribution responsibilities

Before public distribution, the release maintainer must retain applicable notices
and provide corresponding source, build configuration and dependency notices as
required by the exact bundled builds. In particular, a generic FFmpeg source
tarball alone is not evidence that the complete distributor build, its patches
and linked dependencies are covered. The links and collected texts here are a
component inventory, not a certification of all redistribution obligations.
No public release has been made as part of this implementation.
