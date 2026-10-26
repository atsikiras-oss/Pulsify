# Pulsify 🎵

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![Qt 6](https://img.shields.io/badge/Qt-6.x-green.svg)](https://www.qt.io/)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**Pulsify** is a lightweight, modern desktop music player built in C++ using Qt 6. It merges your local music collection with YouTube audio streaming into a unified, fluid interface.

---

## 🔥 Features

- **Local & Streaming Playback:** Native local playback for MP3, FLAC, WAV, OGG, M4A, AAC, OPUS, and direct audio streaming from YouTube/YouTube Music.
- **Dynamic Accent Tinting:** Extracts palette data from track cover art to dynamically update interface gradients and panel backdrops per song.
- **Synchronized Lyrics Engine:** Combines Genius text scraping with LRCLIB timestamp data to display interactive, synchronized line-by-line lyrics with custom time-sync offset controls.
- **Detailed Metadata & Media Info:** Dedicated collapsible sidebar panel displaying stream quality, container/codec formats, sample rates, bitrates, channel information, and live "Next in Queue" previews.
- **Playlist Management & Import:** Create custom local playlists, drag-and-drop items, or import existing YouTube / YouTube Music playlists.
- **Discord Rich Presence Integration:** Displays current playback state, track details, cover art, and time remaining on Discord status profiles.
- **Custom Smooth Canvas Rendering:** Custom QAbstractItemView delegates, smooth pixel-per-step kinetic scrolling, elided labels, and animated transitions.

---

## 🛠 Tech Stack & Dependencies

- **Language:** C++20
- **UI Framework:** Qt 6 (Widgets, Multimedia, Network, Concurrent)
- **External Tools/APIs:**
  - [yt-dlp](https://github.com/yt-dlp/yt-dlp) for YouTube media stream resolution.
  - [LRCLIB](https://lrclib.net/) API for synced `.lrc` lyrics.
  - [Genius API](https://genius.com/) for scrapable text lyrics.
  - Discord RPC C++ SDK.

---

## 🏗 Building from Source

### Prerequisites
- CMake 3.16+
- C++20 compatible compiler (GCC, Clang, or MSVC)
- Qt 6.x SDK (Multimedia and Network modules enabled)

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/your-org/pulsify.git
cd pulsify

# Configure CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build executable
cmake --build build --config Release
```

---

## ⚙️ Configuration

- Optional **Genius API Token**: Add your Genius token in the settings menu or `.ini` configuration under `lyrics/geniusToken` for enhanced Genius rate limits.
- Ensure `yt-dlp` is available in your system path or bundled relative to the binary folder for YouTube streaming functionality.

---

## 📜 License

Distributed under the MIT License. See `LICENSE` for details.
