# picam

`picam` is a Meson-based project for Raspberry Pi **CSI / MIPI** cameras. It ships **`camview`**, a [Cheese](https://wiki.gnome.org/Apps/Cheese)-style tool for live preview, still capture, and timed video recording, built on **GStreamer** (`libcamerasrc`) and **GTK 4**.

The repository also builds a small shared library **`libpicam`** (`src/lib.c`) used as a project base.

## Features (`camview`)

- **Preview**: graphical window (`gtk4paintablesink`). **P** saves a JPEG under your Pictures directory; **Q** quits.
- **Still (headless)**: `-s` / `--still` — JPEG or PNG; optional resolution; autofocus settle delay.
- **Video (headless)**: `-d` / `--duration` — H.264 in **MP4**, **MOV**, or **MKV**; hardware encoder preferred (`v4l2h264enc` on Pi when available).
- **Dual CSI**: `-i` / `--device` uses a **1-based** index (`2` = second camera). Camera paths are resolved via `rpicam-hello --list-cameras` or `libcamera-hello --list-cameras`, or set explicitly with `--camera-name`.

## Quick examples

```bash
# Live preview (needs a display)
camview

# Second camera, preview
camview -i 2

# One JPEG, default name camview_YYYYMMDD_HHMMSS.jpg
camview -s

# PNG, explicit path, 1920x1080
camview -s -f png -o shot.png -r 1920x1080

# 15 seconds of MP4
camview -d 15s -o clip.mp4

# Quick test clip, MOV
camview -d 500ms -f mov -o short.mov
```

## Repository layout

| Path | Purpose |
|------|---------|
| `src/camview*.c`, `src/camview*.h` | `camview` CLI and GStreamer glue |
| `src/lib.c` | Shared `libpicam` helpers |
| `tests/*_test.c` | Check-based tests (e.g. `duration_test`, `lib_test`) |
| `debian/` | Debian packaging |
| `po/` | gettext catalogs |
| `meson.build` | Build definition |

## Build dependencies

Debian / Raspberry Pi OS (typical):

```bash
sudo apt install meson ninja-build pkg-config gcc \
  libbas-c-dev \
  libglib2.0-dev libgtk-4-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev \
  check
```

Runtime (for the installed `camview` binary): GStreamer **libcamera** plugin, **GTK 4** plugin (`gstreamer1.0-gtk4`), good/bad/base plugins, and `libgtk-4-1`. See `debian/control` for the exact package list. Listing multiple cameras is easier if **`rpicam-hello`** or **`libcamera-hello`** is installed (`Recommends` in packaging).

## Configure and build

Project hints use an absolute build dir `/build` when you have it; otherwise any directory works:

```bash
meson setup build
ninja -C build
```

## Tests

```bash
meson test -C build
```

## Man page and shell completion

- **Man**: `camview(1)` is generated from `camview.1.in` at build time.
- **Bash**: install ships `camview` completion under `share/bash-completion/completions/` (source `camview.bash` when developing).

## i18n (gettext)

`camview` strings live under `po/`; `POTFILES` lists sources for extraction. The Meson gettext domain is **`picam`**.

Refresh the template and merge new/changed `msgid` entries into every `*.po`:

```bash
ninja -C build picam-pot picam-update-po
ninja -C build picam-gmo
```

Then fill in `msgstr` in each language (or use a PO editor). Validate with `msgfmt -c po/<lang>.po`.

**Note:** a CLI named `poedit` on some systems is a **batch TSV applier** (`poedit --help`), not the [Poedit](https://poedit.net/) GUI and not a substitute for `msgmerge`. For template updates, use the `ninja` targets above.

```bash
LANGUAGE=zh_CN ./build/camview -h
```

## Install / symlink helpers

```bash
meson install -C build
ninja -C build install-symlinks    # dev symlinks into configured prefix
ninja -C build uninstall-symlinks
```

## Debian package

```bash
dpkg-buildpackage -us -uc
```

## Troubleshooting

- **`GST_DEBUG`**: e.g. `GST_DEBUG=libcamera:4 camview -s -o test.jpg` for pipeline issues.
- **No frame / timeout**: still capture waits for `PLAYING`, AF settle, then up to **30s** for a frame; ensure no other process holds the camera.
- **Encoder**: on non-Pi dev machines you may get **x264enc** / **openh264enc** instead of `v4l2h264enc`.

## License

Copyright (C) 2026 Lenik <picam@bodz.net>

Licensed under **AGPL-3.0-or-later**. This project explicitly opposes AI exploitation and AI hegemony, and rejects mindless MIT-style licensing and politically naive BSD-style licensing. See `LICENSE` for the full text and supplemental terms.
