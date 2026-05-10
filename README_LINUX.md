# Winamp for Linux — Qt5/Qt6 Native Port

A native Linux port of Winamp using Qt as the UI foundation. Supports both **Qt5 (≥ 5.12)** and **Qt6**, with automatic detection at build time. The goal is to maintain the classic Winamp look and feel while running natively on Linux.

## Project Status

🚧 **Early Development** — Fully playable with classic skins

### Completed
- ✅ CMake build system with Qt5/Qt6 dual support
- ✅ Qt Multimedia integration for audio playback
- ✅ Classic skin loading (.wsz/.zip archives and raw BMP directories)
- ✅ 10-band EQ with DSP processing (Qt 6.8+)
- ✅ Real-time spectrum analyzer, oscilloscope, and VU meter (Qt 6.8+)
- ✅ Playlist editor with drag-and-drop reordering
- ✅ Gapless playback via player preloading
- ✅ MPRIS2 D-Bus integration (media keys, KDE/GNOME control)
- ✅ System tray with playback controls
- ✅ Milkdrop-compatible visualization via libprojectM
- ✅ Video playback window
- ✅ Media Library browser
- ✅ Drag-and-drop file/folder support
- ✅ Bookmark manager and recent files
- ✅ Multi-language support (translation system)
- ✅ Command-line file/directory playback

### Qt5 vs Qt6 Feature Matrix

| Feature | Qt5 (≥ 5.12) | Qt6 | Qt 6.8+ |
|---------|:---:|:---:|:-------:|
| Audio playback | ✅ | ✅ | ✅ |
| Classic skins | ✅ | ✅ | ✅ |
| Playlist / gapless | ✅ | ✅ | ✅ |
| MPRIS2 media keys | ✅ | ✅ | ✅ |
| System tray | ✅ | ✅ | ✅ |
| Milkdrop (projectM) | ✅ | ✅ | ✅ |
| Video window | ✅ | ✅ | ✅ |
| 10-band EQ DSP | ❌ | ❌ | ✅ |
| Live spectrum/VU | ❌ | ❌ | ✅ |

### Not Yet Working
- ❌ **Modern skins (XML/Wasabi/Bento)** — The Wasabi XML skin engine has not been ported.
  Modern skin selection is disabled in Preferences. Use classic skins (.wsz) instead.

## Building

### Prerequisites

#### Ubuntu/Debian (Qt6 — recommended):
```bash
sudo apt-get install -y \
    build-essential cmake ninja-build \
    qt6-base-dev qt6-multimedia-dev libqt6multimedia6 \
    qt6-tools-dev libgl1-mesa-dev
```

#### Ubuntu/Debian (Qt5):
```bash
sudo apt-get install -y \
    build-essential cmake ninja-build \
    qtbase5-dev qtmultimedia5-dev libqt5multimedia5-plugins \
    libqt5opengl5-dev libgl1-mesa-dev \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-base
```

#### Fedora/RHEL (Qt6):
```bash
sudo dnf install -y \
    gcc gcc-c++ cmake ninja-build \
    qt6-qtbase-devel qt6-qtmultimedia-devel \
    mesa-libGL-devel
```

#### Fedora/RHEL (Qt5):
```bash
sudo dnf install -y \
    gcc gcc-c++ cmake ninja-build \
    qt5-qtbase-devel qt5-qtmultimedia-devel \
    mesa-libGL-devel \
    gstreamer1-plugins-good gstreamer1-plugins-base-tools
```

#### Arch Linux (Qt6):
```bash
sudo pacman -S base-devel cmake ninja qt6-base qt6-multimedia mesa
```

#### Arch Linux (Qt5):
```bash
sudo pacman -S base-devel cmake ninja qt5-base qt5-multimedia mesa gst-plugins-good
```

#### Optional dependencies (all distros):
```bash
# MPRIS2 media key support (usually installed by default)
# Qt6: qt6-dbus / Qt5: libqt5dbus5

# Milkdrop visualization
# libprojectm-dev (Debian/Ubuntu) / projectM-devel (Fedora) / projectm (Arch)
```

### Build Instructions

```bash
# Configure — CMake auto-detects Qt6, falls back to Qt5
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Force Qt5 (if both are installed)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON

# Build
cmake --build build -j$(nproc)

# Install (optional)
sudo cmake --install build
```

### Running

```bash
# From build directory
./build/winamp

# With files
./build/winamp ~/Music/*.mp3

# With a directory
./build/winamp ~/Music/

# Enqueue instead of replacing playlist
./build/winamp --enqueue ~/Music/album/
```

## Architecture

### Qt5/Qt6 Compatibility Layer

The codebase uses a preprocessor-guard strategy (`#if QT_VERSION >= QT_VERSION_CHECK(...)`) to support both Qt versions from a single source tree. A central compatibility header `src/qt5compat.h` provides macros for the most common API differences:

| Qt6 API | Qt5 API | Macro |
|---------|---------|-------|
| `event->globalPosition().toPoint()` | `event->globalPos()` | `GLOBAL_POS(event)` |
| `event->position().toPoint()` | `event->pos()` | `EVENT_POS(event)` |
| `player->playbackState()` | `player->state()` | `PLAYBACK_STATE(player)` |
| `player->setSource(QUrl)` | `player->setMedia(QMediaContent(QUrl))` | Inline `#if` guards |
| `QMediaMetaData::value()` | `player->metaData(QString)` | Inline `#if` guards |
| `QAudioOutput` (routing object) | `QMediaPlayer::setVolume()` | Inline `#if` guards |

### Key Differences by Version

- **Qt5**: Volume via `QMediaPlayer::setVolume(int 0-100)`, metadata via string keys, GStreamer backend
- **Qt6**: Volume via `QAudioOutput::setVolume(float 0.0-1.0)`, metadata via `QMediaMetaData` enum keys, FFmpeg backend
- **Qt 6.8+**: `QAudioBufferOutput` enables real-time audio buffer tap for EQ DSP, spectrum analyzer, and VU meters

## Features

### Keyboard Shortcuts
- **Space**: Play/Pause
- **S**: Stop
- **Z**: Previous track
- **B**: Next track
- **V**: Stop after current
- **C**: Pause
- **L**: Open file
- **Ctrl+L**: Open URL/location
- **J**: Jump to file
- **Alt+W**: Toggle main window
- **Alt+E**: Toggle equalizer
- **Alt+A**: Toggle playlist
- **Ctrl+P**: Preferences
- **Alt+F4**: Exit

### MPRIS2 Integration
When built with Qt DBus support, Winamp registers on the session bus as `org.mpris.MediaPlayer2.winamp`, providing:
- Play/Pause/Stop/Next/Previous via media keys
- Track metadata to desktop environments (KDE, GNOME, etc.)
- Volume control via system mixer

## Differences from Windows Version

### By Design
- Uses Qt Multimedia instead of DirectSound
- Uses Qt's event loop instead of Win32 message loop
- Native Linux window management
- XDG-compliant file paths (`~/.config/winamp/`)
- MPRIS2 instead of Windows global hotkeys

### Temporary Limitations
- Modern skins not supported (classic skins work)
- Some plugins not yet ported
- EQ DSP requires Qt 6.8+ (renders silently on older versions)

## Contributing

This is a massive undertaking. Contributions welcome!

Priority areas:
1. Modern skin engine (XML/Wasabi)
2. Plugin framework adaptation
3. Testing across Qt5 distributions
4. Input format plugins
5. Output backend optimization

## License

Same as original Winamp source code. See LICENSE.md.

## Credits

- Original Winamp by Nullsoft/AOL/Radionomy
- Linux port by Kristopher Craig
- Qt5/Qt6 compatibility layer
- Community contributors

## Resources

- [Qt6 Documentation](https://doc.qt.io/qt-6/)
- [Qt5 Documentation](https://doc.qt.io/qt-5/)
- [Original Winamp Plugin SDK](https://github.com/WinampDesktop/winamp)
