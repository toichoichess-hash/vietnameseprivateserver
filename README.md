# 🚀 VNG PS Launcher

[![Release](https://img.shields.io/badge/Release-v1.0.0-blue.svg)](https://github.com/toichoichess-hash/vietnameseprivateserver/releases)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-blue.svg)](https://www.microsoft.com/windows)
[![C++](https://img.shields.io/badge/Language-C%2B%2B17-00599C.svg?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![GUI](https://img.shields.io/badge/GUI-Native%20Win32-purple.svg)](https://learn.microsoft.com/en-us/windows/win32/)
[![Binary Size](https://img.shields.io/badge/Binary%20Size-~50%20KB-success.svg)](#)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

A lightning-fast, ultra-lightweight native Windows launcher for **Vietnamese Private Servers (VNG)** and Roblox Share links. It directly translates web links into the native `roblox://` protocol, bypassing browser redirect lag, CAPTCHAs, and loading screens.

---

## ⬇️ Download

Download the portable standalone executable (no installation needed, zero runtime dependencies):

### 📥 **[Download VngPsLauncher.exe (Latest Release)](https://github.com/toichoichess-hash/vietnameseprivateserver/releases/latest/download/VngPsLauncher.exe)**

*(You can also find all versions and changelogs on the [Releases Page](https://github.com/toichoichess-hash/vietnameseprivateserver/releases).)*

---

## ✨ Features

- ⚡ **Direct Protocol Dispatch**: Converts links to `roblox://` URIs and launches them immediately through Windows Shell.
- 🎨 **Immersive Dark Mode**: Windows 11 / 10 DWM dark titlebar and custom dark palette styling.
- 🪶 **Zero Bloat (~50 KB)**: Written in pure C++ and Win32. No Electron, no WebView2, no external dependencies.
- 📋 **1-Click Convenience**: Includes `Paste`, `Clear`, and `Launch` buttons, plus instant keyboard support (press `Enter` to launch).
- 🔒 **Safe & Open Source**: Clean, transparent C++ code that only launches the registered system protocol.

---

## 🔗 Supported URL Formats

VNG PS Launcher automatically parses and handles:

| Link Type | Web Link Format | Translated Protocol URI |
| :--- | :--- | :--- |
| **Private Server Link** | `https://www.roblox.com/games/123456789/Name?privateServerLinkCode=abcdef...` | `roblox://placeId=123456789&linkCode=abcdef...` |
| **New Share Link** | `https://www.roblox.com/share?code=abcdef123456&type=Server` | `roblox://navigation/share_links?code=abcdef123456&type=Server` |

---

## 🖥️ How to Use

1. Copy any Roblox private server link or share link to your clipboard.
2. Open **`VngPsLauncher.exe`**.
3. Click **Paste** (or press `Ctrl + V`).
4. Press **Enter** or click **Launch**.
5. The game boots up directly into the target private server.

---

## 🛠️ Building from Source

### Prerequisites
* Windows 10 or 11 (64-bit)
* Visual Studio with **Desktop development with C++** (MSVC compiler).

### Option 1: 1-Click Build Script
Double-click:
```cmd
build.bat
```
This automatically compiles `src/main.cpp` and `src/resource.rc` into `bin/VngPsLauncher.exe`.

### Option 2: Build with CMake
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

---

## 📂 Project Structure

```text
vietnameseprivateserver/
├── .gitignore             # Git ignore rules for build artifacts
├── CMakeLists.txt         # Modern CMake configuration
├── LICENSE                # MIT License
├── README.md              # Documentation & download links
├── build.bat              # 1-click MSVC compile script
├── push_to_github.bat     # Fast git push helper
├── roblox_launcher.cpp    # Root single-file source code
└── src/
    ├── main.cpp           # Main application source code
    └── resource.rc        # Application manifest & version metadata
```

---

## 📄 License

Distributed under the **MIT License**. See [`LICENSE`](LICENSE) for more information.
