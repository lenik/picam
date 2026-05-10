# picam

`picam` 是基于 Meson 的 Raspberry Pi **CSI / MIPI** 摄像头项目，提供 **`camview`** 工具：类似 GNOME **Cheese** 的实时预览、拍照与定时录像，技术栈为 **GStreamer**（`libcamerasrc`）与 **GTK 4**。

仓库同时构建小型共享库 **`libpicam`**（`src/lib.c`）。

## 功能（`camview`）

- **预览**：图形窗口（`gtk4paintablesink`）。**P** 将当前画面存为 JPEG（默认在用户「图片」目录）；**Q** 退出。
- **无头拍照**：`-s` / `--still`，支持 JPEG / PNG，可选分辨率与对焦等待时间。
- **无头录像**：`-d` / `--duration`，H.264 封装为 **MP4**、**MOV** 或 **MKV**；在树莓派上优先使用硬件编码（`v4l2h264enc`，若可用）。
- **双 CSI**：`-i` / `--device` 为 **从 1 开始的序号**（**2** 表示第二路摄像头）。通过 `rpicam-hello --list-cameras` 或 `libcamera-hello --list-cameras` 解析路径，也可用 `--camera-name` 直接指定 libcamera 设备 id。

## 示例

```bash
# 实时预览（需要图形环境）
camview

# 使用第二路摄像头预览
camview -i 2

# 拍一张 JPEG，默认文件名 camview_YYYYMMDD_HHMMSS.jpg
camview -s

# PNG、指定路径、1920x1080
camview -s -f png -o shot.png -r 1920x1080

# 录制 15 秒 MP4
camview -d 15s -o clip.mp4

# 极短 MOV 样片
camview -d 500ms -f mov -o short.mov
```

## 仓库结构

| 路径 | 说明 |
|------|------|
| `src/camview*.c`, `src/camview*.h` | `camview` 命令行与 GStreamer 逻辑 |
| `src/lib.c` | 共享库 `libpicam` |
| `tests/*_test.c` | 基于 Check 的测试（如 `duration_test`、`lib_test`） |
| `debian/` | Debian 打包 |
| `po/` | gettext 翻译 |
| `meson.build` | 构建定义 |

## 构建依赖

在 Debian / Raspberry Pi OS 上典型依赖：

```bash
sudo apt install meson ninja-build pkg-config gcc \
  libbas-c-dev \
  libglib2.0-dev libgtk-4-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev \
  check
```

运行时已安装的二进制需要：GStreamer **libcamera** 插件、**GTK 4** 的 GStreamer 插件（`gstreamer1.0-gtk4`）、good/bad/base 插件、`libgtk-4-1` 等，详见 `debian/control`。枚举多路摄像头时建议安装 **`rpicam-hello`** 或 **`libcamera-hello`**（打包中为 `Recommends`）。

## 配置与编译

说明文档中可使用绝对构建目录 `/build`；任意可写目录均可：

```bash
meson setup build
ninja -C build
```

## 测试

```bash
meson test -C build
```

## 手册与补全

- **手册**：由 `camview.1.in` 生成 `camview(1)`。
- **Bash**：安装后位于 `share/bash-completion/completions/`；开发时可参考仓库内 `camview.bash`。

## i18n（gettext）

可翻译字符串在 `po/`，由 `POTFILES` 指定源文件；gettext 域名为 **`picam`**。

从源码更新模板并把新增/变更的 `msgid` 合并进各语言 `*.po`：

```bash
ninja -C build picam-pot picam-update-po
ninja -C build picam-gmo
```

随后在每种语言中填写 `msgstr`（或用 PO 编辑器）。可用 `msgfmt -c po/<语言>.po` 做校验。

**说明：** 某些环境下的命令行工具 `poedit`（`poedit --help`）是 **TSV 批量写入工具**，不是 [Poedit](https://poedit.net/) 图形客户端，也不能代替 `msgmerge`；更新模板请用上面的 `ninja` 目标。

```bash
LANGUAGE=zh_CN ./build/camview -h
```

## 安装 / 符号链接

```bash
meson install -C build
ninja -C build install-symlinks
ninja -C build uninstall-symlinks
```

## Debian 打包

```bash
dpkg-buildpackage -us -uc
```

## 故障排除

- **`GST_DEBUG`**：例如 `GST_DEBUG=libcamera:4 camview -s -o test.jpg` 排查流水线。
- **超时 / 无画面**：拍照会等待管道进入 `PLAYING`、对焦稳定，再最多等待 **30s** 取帧；请确认无其他进程占用摄像头。
- **编码器**：非树莓派开发机上可能回退到 **x264enc** / **openh264enc**。

## 许可证

Copyright (C) 2026 Lenik <picam@bodz.net>

采用 **AGPL-3.0-or-later**。本项目明确反对 AI 剥削与 AI 霸权，反对无脑 MIT 式许可证和政治上幼稚的 BSD 式许可证。完整文本及补充条款见 `LICENSE`。
