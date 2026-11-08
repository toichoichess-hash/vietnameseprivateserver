# 🚀 Roblox Fast Launcher

[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-blue.svg)](https://www.microsoft.com/windows)
[![C++](https://img.shields.io/badge/Language-C%2B%2B17-00599C.svg?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![GUI](https://img.shields.io/badge/GUI-Native%20Win32-purple.svg)](https://learn.microsoft.com/en-us/windows/win32/)
[![Binary Size](https://img.shields.io/badge/Binary%20Size-~50%20KB-success.svg)](#)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

A blisteringly fast, ultra-lightweight native Windows utility that instantly launches Roblox private servers and share links directly via the `roblox://` protocol—completely bypassing browser redirect lag, slow web pages, and browser prompts.

---

## ✨ Features

- ⚡ **Zero-Latency Launch**: Converts web URLs into native `roblox://` protocol URIs and dispatches via Windows Shell instantly.
- 🎨 **Immersive Dark Theme**: Native Windows 10/11 DWM dark titlebar and sleek custom-rendered dark UI components.
- 🪶 **Ultra-Lightweight**: Built with pure C++ and Win32 API. No Electron, no WebView2, no bloated runtimes. Standalone binary is only **~50 KB**.
- 📋 **One-Click Convenience**: Includes `Paste`, `Clear`, and `Launch` actions, with full keyboard support (`Enter` key launches immediately).
- 🔒 **Zero External Dependencies**: Compiles against standard Windows system libraries (`user32`, `gdi32`, `comctl32`, `dwmapi`, `shell32`).

---

## 🔗 Supported URL Formats

Roblox Fast Launcher automatically parses and translates:

| Link Type | Web Link Format | Translated Protocol URI |
| :--- | :--- | :--- |
| **Private Server Link** | `https://www.roblox.com/games/123456789/Name?privateServerLinkCode=abcdef...` | `roblox://placeId=123456789&linkCode=abcdef...` |
| **New Share Link** | `https://www.roblox.com/share?code=abcdef123456&type=Server` | `roblox://navigation/share_links?code=abcdef123456&type=Server` |

---

## 🖥️ Preview & Usage

1. Copy any Roblox private server link or share link to your clipboard.
2. Open **RobloxLauncher.exe**.
3. Click **Paste** (or press `Ctrl + V`).
4. Press **Enter** or click **Launch**.
5. Roblox boots up immediately into the target private server.

---

## 🛠️ Building from Source

### Prerequisites
* Windows 10 / 11 (64-bit)
* Visual Studio 2019, 2022, or 2026 with the **Desktop development with C++** workload installed, or any standard MSVC / Clang-cl environment.

### Option 1: 1-Click Batch Build (Recommended)
Simply double-click:
```cmd
build.bat
```
This automatically initializes `vcvarsall.bat x64`, compiles the Win32 resources, builds `src/main.cpp`, and outputs `bin/RobloxLauncher.exe`.

### Option 2: Build with CMake
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Option 3: Compile via Command Line (MSVC)
From a **x64 Native Tools Command Prompt for VS**:
```cmd
rc.exe /fo resource.res src/resource.rc
cl.exe /O2 /W3 /DNDEBUG /EHsc /std:c++17 src/main.cpp resource.res /Fe:RobloxLauncher.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib dwmapi.lib shell32.lib
```

---

## 📂 Project Structure

```text
roblox-launcher/
├── .gitignore          # Git ignore rules for build artifacts
├── CMakeLists.txt      # Modern CMake configuration
├── LICENSE             # MIT License
├── README.md           # Documentation & guides
├── build.bat           # 1-click MSVC compile script
└── src/
    ├── main.cpp        # Main application source code
    └── resource.rc     # Application manifest & version info
```

---

## 📄 License

Distributed under the **MIT License**. See [`LICENSE`](LICENSE) for more information.
