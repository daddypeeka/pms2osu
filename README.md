# pms2osu-v2

A self-contained, cross-platform **PMS → osu! beatmap (.osz)** converter with a
Dear ImGui + GLFW GUI, written from scratch in C++17.

## Features

- **Drag & drop** a big folder (containing many PMS folders), a single PMS
  folder, or a `.pms` file.
- Scans the imported folder and turns every folder that contains `.pms` files
  into one **.osz**.
- Each `.osz` contains:
  - one `.osu` (osu!mania) per `.pms` chart in the folder,
  - an `audio.ogg` synthesised from the PMS (BGM + key sounds),
  - referenced background / banner images.
- **One-click** set **OD / HP / AR / Creator** (Creator defaults to `PMS`).
- Audio rendering is a complete, self-written BMS/PMS renderer (fixes the
  "PMS only converts 3/4" bug found in the old reference tooling) and encodes
  with the bundled libvorbis at **192 kbps** (managed bitrate).
- `.osz` is a standard (stored) ZIP written by our own minimal zip writer.

## Build (Windows, w64devkit)

The repository ships prebuilt GLFW for MinGW-w64 and the full libogg/libvorbis
sources under `third_party/`.

```
build.bat
# or:
make
```

Output: a single self-contained `dist/pms2osu-v2.exe`. GLFW is linked
statically, so **no `glfw3.dll` (or any DLL) is needed** next to the exe.

## Build (Linux / macOS)

GLFW is not bundled as a source, so install it first:

```
# Debian/Ubuntu
sudo apt install libglfw3-dev

# macOS
brew install glfw
```

Then:

```
cmake -S . -B build && cmake --build build
```

The Ogg Vorbis codec is always built from `third_party`, so no external audio
dependencies are required.

## CI / Releases

`.github/workflows/build.yml` builds and packages the app on all three
platforms:

- **Windows** — MinGW-w64 (`make`), output `dist/pms2osu-v2.exe`
- **macOS** — Homebrew GLFW + CMake, output `dist/pms2osu-v2-macos`
- **Linux** — `libglfw3-dev` + CMake, output `dist/pms2osu-v2-linux`

Every push/PR uploads each binary as a build artifact. Pushing a `v*` tag (e.g.
`v1.0.0`) also creates a GitHub Release with all three binaries attached.

## Notes

- PMS/BMS files are usually Shift-JIS encoded; the parser decodes them to UTF-8
  (on Windows via CP932; elsewhere passed through best-effort).
- All paths (drag & drop, manual input, auto-load) are normalised to UTF-8, so
  folders with Japanese / non-ASCII names work on Windows.
- Long notes are paired (LNOBJ / visible & invisible long channels) and emitted
  as mania holds.
- Key layouts supported: 3-key, 5-key, 9-key (and 9-key battle).
- Timing follows bmx2wav semantics: channel 03 = direct BPM, channel 08 =
  extended (#BPMxx), channel 09 = STOP (`value/192 * 4` beats), channel 02 =
  measure ratio. The .osu timing section emits the base point plus every BPM
  change.
- Completed tasks are automatically removed from the task list when the
  conversion finishes.

## Audio renderer fixes (from the pms2ogg192 test tool)

The parser/renderer fixes validated in the standalone `pms2ogg192` tool are
integrated here too:

- **Base-36 sample indices** — BMS `#WAVxx/#BPMxx/#STOPxx` indices are base-36
  (`0G`, `I5`, `7K` …), not hex; previously most samples were silently dropped.
- **`.wav` → `.ogg` fallback** — charts that reference `foo.wav` while the real
  file is `foo.ogg` now load correctly (no more silent audio).
- **Per-bar time signature** — channel 02 (bar length ratio) applies to one bar
  only, matching bmx2wav (previously it leaked to all following bars).
- **Full sample tail** — the render runs to the end of the last bar plus the
  longest triggered sample tail (e.g. a 13 s fade), instead of clipping at
  `last note + 1 s`.
- **192 kbps managed bitrate** — `writeOgg` now encodes with min/nominal/max
  pinned to 192 kbps instead of a VBR quality of 0.5.
