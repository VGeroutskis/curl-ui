# curl UI

A native desktop front end for [curl](https://curl.se), written in C11 with
SDL2 and Nuklear. All 278 command line options, each with curl's own
documentation one mouse movement away. It runs your real `curl` binary and
re-implements nothing.

## Install

Download from the [releases page](../../releases), or the artifacts of the
`curl-ui` workflow run:

| Platform | File | Install |
|---|---|---|
| Windows x64 | `curl-ui-*-windows-x64.exe` | run the installer (`.zip` is the portable form) |
| Debian, Ubuntu | `curl-ui-*.deb` | `sudo apt install ./curl-ui_*.deb` |
| Fedora, RHEL | `curl-ui-*.rpm` | `sudo dnf install ./curl-ui-*.rpm` |
| macOS (arm64 + x86_64) | `curl-ui-*-macos-universal.dmg` | drag to Applications |

The builds are not code signed. Windows SmartScreen and macOS Gatekeeper warn
the first time; on macOS use right click, Open.

curl itself must be installed. Windows 10 and later and macOS include it. If
it is not on `PATH`, click the version pill in the header and enter its path.

## Using it

- **Library, left:** search names and descriptions, or filter with the chips.
  Click a row to add that option.
- **Centre:** method and URL on top, a card per added option, the exact command
  below (Copy gives the same text), and the output with Response and Log tabs.
  Press Enter in the URL, or Run. Recipes are starting points.
- **Docs, right:** hover a library row, a card or a flag in the command and that
  option's full documentation appears, and stays while you move over to read.
  Every `--option` in it is a link.

To fill the Log tab and the status chips, a run also passes `--verbose`,
`--no-progress-meter` and `--write-out` to curl, when you did not set them
yourself. The Log says so on its first line, and they are not in the command you
copy. The rest of the command is exactly what is executed, as an argument array
with no shell.

## Build

Requires CMake 3.16 or newer and a C11 compiler.

```sh
# Fedora: sudo dnf install cmake gcc sdl2-compat-devel rpm-build
# Debian: sudo apt install cmake gcc libsdl2-dev dpkg-dev
cmake -S ui -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/curl-ui
```

Windows and macOS download SDL 2.30 and link it statically. To make installers
run `cpack` in the build directory: DEB and RPM on Linux, NSIS and ZIP on Windows
(NSIS must be installed), a DMG on macOS. For a universal macOS build add
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.

`-DCURLUI_SANITIZE=ON` builds with ASan and UBSan. `-DCURLUI_BUILD_GUI=OFF`
builds only the core and its tests, with no SDL needed.

Generated files are committed, so building needs no Python:

| Regenerate | With | Needs |
|---|---|---|
| `src/core/options_data.c` | `tools/gen_options.py` | Python |
| `src/ui/fonts_data.{c,h}` | `tools/build_fonts.py` | `pip install fonttools brotli` |
| `packaging/curl-ui.{png,ico,icns}` | `tools/build_icons.py` | `pip install pillow` |

## Command line

These double as the test hooks. With `--screenshot` the app runs the script,
saves the image and exits; `SDL_VIDEODRIVER=dummy` makes that work headless.

| Option | Effect |
|---|---|
| `--url URL`, `--method NAME` | fill the request bar |
| `--add NAME[=VALUE]` | add an option, repeatable |
| `--run` | run at startup |
| `--light` | light theme |
| `--docs NAME` | open the docs of an option |
| `--search TEXT` | fill the library search |
| `--log` | show the Log tab |
| `--size WxH` | window size |
| `--mouse X,Y`, `--click X,Y` | scripted mouse, in order given |
| `--wheel X,Y,DELTA` | scripted wheel |
| `--type TEXT` | scripted typing; a newline is Enter |
| `--screenshot FILE.bmp` | save the result and exit |
| `--stats` | print UI time per frame |

## Layout

```
ui/
  src/core/       no SDL, unit tested
    options*      the option table and search
    builder       options -> argv, shell-quoted preview
    procrun       posix_spawn / CreateProcessW runner
    outbuf        thread-safe, line-indexed, sanitizing output buffer
    json_fmt      strict JSON validator and pretty printer
    curlinfo      detects the installed curl and what it supports
    docs          curl's markdown-lite -> styled blocks
    osthread.h    portable thread and mutex
  src/ui/         SDL + Nuklear: main loop, theme, views
  tests/          ctest: test_core and the echoargs helper
  tools/          generators
  third_party/    vendored Nuklear (commit in NUKLEAR_COMMIT)
  assets/fonts/   font sources and their licenses
  packaging/      icons, .desktop, Info.plist, .rc
```

## Known limitations

- No native file picker: type paths into the value fields.
- An option with an empty value is left out of the command, so a deliberately
  empty argument such as `-A ""` cannot be expressed.
- Options keep the order they were added, with the URL last. That is enough for
  most uses; `--next` sequences that need several URLs are not modelled, though
  `--url` can be added as an option.
- Output is held up to 8 MB; use `--output` for more. Text cannot be selected in
  the output, Copy takes the whole tab.
- Settings such as the theme and curl path are not remembered between runs.
- Text fields scroll sideways rather than wrap; long bodies are easier with
  `--data @file`.
- Line editing has no right-to-left or complex script shaping, and glyphs outside
  Latin, Greek and Cyrillic show as missing.

## License

See [LICENSE](LICENSE) for curl UI and its third-party notices.
