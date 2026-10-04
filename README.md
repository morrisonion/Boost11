<div align="center">

<img src="icon.png" alt="Boost11 icon" width="128" height="128">

# Boost11

**Switch Windows between *Balanced* and *High performance* in one click.**
A tiny, dependency-free power mode switcher written in C++.

[![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-0078D4?logo=windows11&logoColor=white)](#requirements)
[![License](https://img.shields.io/github/license/morrisonion/Boost11)](LICENSE)
[![Stars](https://img.shields.io/github/stars/morrisonion/Boost11?style=flat)](https://github.com/morrisonion/Boost11/stargazers)
[![Last commit](https://img.shields.io/github/last-commit/morrisonion/Boost11)](https://github.com/morrisonion/Boost11/commits)

</div>

---

## Screenshots
<img src="screenshots/menu.png" alt="Boost11 settings menu" width="512" height="512" style="border-radius: 12px;">
<img src="screenshots/tray.png" alt="Boost11 tray menu" width="256" height="256" style="border-radius: 12px;">


## Requirements

- Windows 11/Windows 10


## Information
The settings menu lets you choose the language and whether Boost11 should start with Windows (tray mode only).

Settings are stored in `%APPDATA%\Boost11\config.ini`.

> If you move `Boost11.exe`, run the exe again so the desktop shortcut points to the new location.

## Building

Any MinGW-w64 toolchain works (e.g. [w64devkit](https://github.com/skeeto/w64devkit) or [WinLibs](https://winlibs.com/)).

```bat
git clone https://github.com/morrisonion/Boost11.git
cd Boost11
build.bat
```

`build.bat` compiles the resources (`windres`) and links a static `Boost11.exe` with `-lgdiplus -lgdi32 -luser32 -lshell32 -lshlwapi -lole32 -luuid -lpowrprof -ldwmapi -ladvapi32 -lshcore`.


## How it works

- Plans are discovered by running `powercfg -l` (hidden) and matching the well-known GUIDs, with name matching as fallback. The result is cached in `config.ini`.
- Modes are switched with `PowerSetActiveScheme`; the active plan is read with `PowerGetActiveScheme`.
- The overlay is a click-through layered window updated with `UpdateLayeredWindow` for smooth per-pixel alpha fading.
- All UI (wizard, tray menu, language dropdown) is custom-drawn with GDI+ and animated with a simple easing timer.

## License

This project is licensed under the GNUgplv3
