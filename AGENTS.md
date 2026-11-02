# Repository Guidelines

## Project Structure & Module Organization

This VLC 4 fork implements Juxtopposed’s interface redesign.

- `src/`, `include/`, and `lib/`: playback core, headers, and libVLC API.
- `modules/`: plugins; `modules/gui/qt/` contains the Qt interface. Redesign components live in `player/qml/redesign/`, with assets in `pixmaps/` and `fonts/` under the Qt directory.
- `test/`: core/plugin/libVLC tests; `modules/gui/qt/tests/`: interface tests.
- `share/`, `po/`, `doc/`: shared assets, translations, and documentation. `contrib/` handles dependencies; `extras/package/` contains packaging scripts.

## Build, Test, and Development Commands

Follow `doc/BUILD-linux.md` for prerequisites and isolated XDG configuration. Meson support remains experimental. From the repository root:

```sh
meson setup ../build-linux . --buildtype=debugoptimized --wrap-mode=nofallback \
    --prefix="$HOME/.local/vlc-dev" -Dqt=enabled -Dmedialibrary=disabled
meson compile -C ../build-linux -j2
../build-linux/bin/vlc-static --intf=qt
```

These commands configure, build, and launch the development player. This baseline disables the media library. Keep builds outside the source tree and use a private install prefix. `INSTALL` covers Autotools; platform and release guides live in `doc/`.

## Coding Style & Naming Conventions

Use GNU C17/C++17 and follow `modules/gui/qt/HACKING`: four-space indentation, no tabs or trailing whitespace, and spaces around operators. Match surrounding brace and naming conventions. Name redesign QML components `Redesign*.qml`; use camelCase properties. No repository-wide formatter is configured. Edit source QML, not generated QML-cache C++ files.

## Testing Guidelines

Core tests use C executables; interface tests use Qt Test and Qt Quick Test. Follow `test_*.cpp` and `tst_*.qml` naming in the Qt test directory.

```sh
ninja -C ../build-linux -j2 meson-test-prereq
QT_QPA_PLATFORM=offscreen meson test -C ../build-linux --no-rebuild \
    --suite VLC:qt --num-processes 2 --print-errorlogs
```

The first command builds test prerequisites with bounded parallelism. Omit the suite filter for all registered tests. Add focused regressions for behavior changes; no numeric coverage threshold is documented. Check UI changes interactively across compact layouts, scaling, and light/dark themes.

## Commit & Pull Request Guidelines

Use `<area>: <imperative summary>`, e.g. `qt: preserve library navigation`. Describe the problem, link related issues, report verification and limitations, and include screenshots for interface changes. Follow `doc/VideoLAN_AI_Policy.md`: disclose coding assistance in merge requests, not commits; contributors must personally submit upstream proposals.

## Configuration Hygiene

Use temporary XDG directories for launches/tests. Keep OpenSubtitles keys and generated `subtitle_consumer_key.hpp` out of Git; see `modules/gui/qt/HACKING`.
